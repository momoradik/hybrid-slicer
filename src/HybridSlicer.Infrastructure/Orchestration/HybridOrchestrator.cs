using System.Text;
using System.Text.RegularExpressions;
using HybridSlicer.Application.Common;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Orchestration;

/// <summary>
/// Builds the single hybrid G-code output by:
///  1. Stripping any wrapper custom blocks previously baked into print.gcode
///     (those are re-applied here so they appear once at the right place).
///  2. Parsing print G-code into a header (lines before the first <c>;LAYER:N</c>
///     marker) and per-layer segments.
///  3. Emitting JobStart + BeforePrinting custom blocks at the top.
///  4. Emitting Cura's startup G-code header.
///  5. For each machining layer: printing through layer N first, then injecting
///     BeforeMachining custom blocks → CNC toolpath → AfterMachining blocks.
///  6. Flushing any remaining print layers (which carry Cura's footer) after the
///     last machining event.
///  7. Emitting AfterPrinting + JobEnd custom blocks at the bottom.
/// </summary>
public sealed partial class HybridOrchestrator : IHybridOrchestrator
{
    // Matches ";LAYER:42" or ";LAYER_COUNT:200" — use only the single-layer variant
    [GeneratedRegex(@"^;LAYER:(\d+)", RegexOptions.Multiline)]
    private static partial Regex LayerMarkerRegex();

    private readonly ILogger<HybridOrchestrator> _logger;

    public HybridOrchestrator(ILogger<HybridOrchestrator> logger) => _logger = logger;

    public async Task<HybridPlanResult> BuildPlanAsync(
        HybridPlanRequest request,
        CancellationToken cancellationToken = default)
    {
        _logger.LogInformation(
            "Building hybrid plan for job {JobId}: {Total} layers, machine every {N}",
            request.JobId, request.TotalPrintLayers, request.MachineEveryNLayers);

        var printGCode = await File.ReadAllTextAsync(request.PrintGCodePath, cancellationToken);
        // Strip wrapper blocks previously baked into print.gcode by SlicePrintJobHandler —
        // we re-emit them at the proper place in this hybrid output so each appears once.
        printGCode = CustomGCodeBlockApplier.Strip(printGCode);
        var (header, segments) = SplitByLayer(printGCode);

        var plan = HybridProcessPlan.Create(
            request.JobId,
            request.MachineEveryNLayers,
            request.TotalPrintLayers);

        var output = new StringBuilder();
        output.AppendLine("; ============================================================");
        output.AppendLine("; HybridSlicer — Hybrid Manufacturing G-code");
        output.AppendLine($"; Generated : {DateTime.UtcNow:O}");
        output.AppendLine($"; Total layers     : {request.TotalPrintLayers}");
        output.AppendLine($"; Machine every N  : {request.MachineEveryNLayers}");
        output.AppendLine("; ============================================================");
        output.AppendLine();

        int stepIndex = 0;

        // Bookend top: JobStart → BeforePrinting (HSCB-wrapped, deduped if upstream stripped them)
        AppendCustomBlocks(output, request.EnabledCustomBlocks, GCodeTrigger.JobStart, plan, ref stepIndex);
        AppendCustomBlocks(output, request.EnabledCustomBlocks, GCodeTrigger.BeforePrinting, plan, ref stepIndex);

        // Cura's startup G-code (heating, homing, prime tower) lives before the first ;LAYER:N marker.
        // Previously this header was silently overwritten in SplitByLayer; now it's preserved.
        if (!string.IsNullOrWhiteSpace(header))
        {
            output.AppendLine("; --- Print startup (Cura header) ---");
            output.Append(header);
            if (!header.EndsWith('\n')) output.AppendLine();
        }

        int printStart = -1; // -1 so the first flush includes layer 0

        // Use the actual machined layers from the parsed toolpath (sorted ascending).
        // This correctly handles both manual-interval and auto-machining-frequency scheduling.
        var sortedLayers = request.CncGCodeByLayer.Keys.OrderBy(x => x).ToList();

        for (var i = 0; i < sortedLayers.Count; i++)
        {
            var layer = sortedLayers[i];

            // Flush ALL print layers from (printStart+1) through (layer) inclusive.
            // Layer N is printed BEFORE the CNC operation that machines its top surface.
            var printFrag = ConcatLayers(segments, printStart + 1, layer);
            if (!string.IsNullOrWhiteSpace(printFrag))
            {
                output.AppendLine($"; --- Print layers {printStart + 1}–{layer} ---");
                output.Append(printFrag);
                output.AppendLine();

                plan.AddStep(ProcessStep.CreatePrintStep(
                    plan.Id, stepIndex++, printStart + 1, layer, printFrag));
            }

            printStart = layer;

            // BeforeMachining blocks
            AppendCustomBlocks(output, request.EnabledCustomBlocks,
                GCodeTrigger.BeforeMachining, plan, ref stepIndex);

            // Emit CNC preamble (spindle positioning) before the very first machining block
            if (i == 0 && !string.IsNullOrWhiteSpace(request.CncPreamble))
            {
                output.AppendLine("; --- CNC Preamble (spindle start position) ---");
                output.AppendLine(request.CncPreamble.TrimEnd());
                output.AppendLine();
            }

            // CNC toolpath for this layer
            var cncGCode = request.CncGCodeByLayer[layer];
            output.AppendLine($"; --- CNC Machining @ Layer {layer} ---");
            output.AppendLine(cncGCode.TrimEnd());
            output.AppendLine($"; --- End CNC @ Layer {layer} ---");
            output.AppendLine();

            var cncStep = ProcessStep.CreateMachiningStep(
                plan.Id, stepIndex++, layer, cncGCode, request.CncToolId);
            cncStep.SetSafetyResult(SafetyStatus.Clear); // validated in GenerateToolpaths
            plan.AddStep(cncStep);

            // Emit CNC postamble (spindle park + M5) after the very last machining block
            if (i == sortedLayers.Count - 1 && !string.IsNullOrWhiteSpace(request.CncPostamble))
            {
                output.AppendLine("; --- CNC Postamble (spindle end / park) ---");
                output.AppendLine(request.CncPostamble.TrimEnd());
                output.AppendLine();
            }

            // AfterMachining blocks
            AppendCustomBlocks(output, request.EnabledCustomBlocks,
                GCodeTrigger.AfterMachining, plan, ref stepIndex);
        }

        // Flush any remaining print layers after the last machining event
        if (printStart < request.TotalPrintLayers)
        {
            var lastFrag = ConcatLayers(segments, printStart + 1, request.TotalPrintLayers);
            if (!string.IsNullOrWhiteSpace(lastFrag))
            {
                output.AppendLine($"; --- Print layers {printStart + 1}–{request.TotalPrintLayers} ---");
                output.Append(lastFrag);
                output.AppendLine();

                plan.AddStep(ProcessStep.CreatePrintStep(
                    plan.Id, stepIndex++,
                    printStart + 1, request.TotalPrintLayers, lastFrag));
            }
        }

        // Bookend bottom: AfterPrinting → JobEnd
        AppendCustomBlocks(output, request.EnabledCustomBlocks, GCodeTrigger.AfterPrinting, plan, ref stepIndex);
        AppendCustomBlocks(output, request.EnabledCustomBlocks, GCodeTrigger.JobEnd, plan, ref stepIndex);

        output.AppendLine("; ============================================================");
        output.AppendLine("; End of HybridSlicer G-code");
        output.AppendLine("; ============================================================");

        plan.SetOverallSafety(SafetyStatus.Clear);

        // Ensure output directory exists
        var outDir = Path.GetDirectoryName(request.OutputGCodePath);
        if (!string.IsNullOrWhiteSpace(outDir)) Directory.CreateDirectory(outDir);

        await File.WriteAllTextAsync(request.OutputGCodePath, output.ToString(), cancellationToken);

        _logger.LogInformation(
            "Hybrid G-code written: {Path} ({Steps} steps, {Chars} chars)",
            request.OutputGCodePath, plan.Steps.Count, output.Length);

        return new HybridPlanResult(plan, request.OutputGCodePath);
    }

    // ── Private helpers ───────────────────────────────────────────────────────

    /// <summary>
    /// Splits the raw print G-code into a header (lines before the first <c>;LAYER:N</c>
    /// marker — Cura's startup G-code) and a dictionary keyed by layer index.
    /// Content for each layer includes the <c>;LAYER:N</c> marker line itself.
    /// </summary>
    private static (string Header, Dictionary<int, string> Segments) SplitByLayer(string printGCode)
    {
        var segments = new Dictionary<int, string>();
        var lines = printGCode.Split('\n');
        int? currentLayer = null;
        var header = new StringBuilder();
        var current = new StringBuilder();

        foreach (var rawLine in lines)
        {
            var match = LayerMarkerRegex().Match(rawLine);
            if (match.Success)
            {
                // Flush whatever was being accumulated. Before the first marker that's
                // the print startup header; after that it's the previous layer's content.
                if (currentLayer is null)
                    header.Append(current.ToString());
                else
                    segments[currentLayer.Value] = current.ToString();

                currentLayer = int.Parse(match.Groups[1].Value);
                current.Clear();
            }
            current.AppendLine(rawLine);
        }

        // Flush whatever is still buffered: either pure header (no layer markers in file),
        // or the final layer's content (which carries Cura's end G-code as a tail).
        if (currentLayer is null)
            header.Append(current.ToString());
        else if (current.Length > 0)
            segments[currentLayer.Value] = current.ToString();

        return (header.ToString(), segments);
    }

    /// <summary>Concatenates layer G-code fragments from 'from' to 'to' inclusive.</summary>
    private static string ConcatLayers(Dictionary<int, string> segments, int from, int to)
    {
        var sb = new StringBuilder();
        for (var l = from; l <= to; l++)
        {
            if (segments.TryGetValue(l, out var frag))
                sb.Append(frag);
        }
        return sb.ToString();
    }

    /// <summary>
    /// Appends all enabled G-code blocks for the given trigger into the output.
    /// Increments stepIndex for each block added to the plan.
    /// </summary>
    private static void AppendCustomBlocks(
        StringBuilder output,
        IReadOnlyList<CustomGCodeBlock> blocks,
        GCodeTrigger trigger,
        HybridProcessPlan plan,
        ref int stepIndex)
    {
        foreach (var block in blocks
                     .Where(b => b.IsEnabled && b.Trigger == trigger)
                     .OrderBy(b => b.SortOrder))
        {
            output.AppendLine($"; --- Custom block: '{block.Name}' trigger={trigger} ---");
            output.AppendLine(block.GCodeContent);
            output.AppendLine($"; --- End block: '{block.Name}' ---");
            output.AppendLine();

            plan.AddStep(ProcessStep.CreateCustomGCodeStep(plan.Id, stepIndex++, block.Id));
        }
    }
}
