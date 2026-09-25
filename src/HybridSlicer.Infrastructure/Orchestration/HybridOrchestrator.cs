using System.Text;
using System.Text.RegularExpressions;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Orchestration;

/// <summary>
/// Builds the single hybrid G-code output by:
///  1. Parsing print G-code into per-layer segments (;LAYER:N markers)
///  2. For each machining layer: printing through layer N first, then injecting
///     BeforeMachining custom blocks → CNC toolpath → AfterMachining blocks
///  3. Flushing any remaining print layers after the last machining event
///  4. Wrapping with JobStart / JobEnd custom blocks
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
        var segments = SplitByLayer(printGCode);

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

        // JobStart blocks
        AppendCustomBlocks(output, request.EnabledCustomBlocks, GCodeTrigger.JobStart, plan, ref stepIndex);

        int printStart = -1; // last layer that has been flushed into output (-1 = include layer 0)

        // Use the actual machined layers from the parsed toolpath (sorted ascending).
        // Filter out comment-only layers (skipped due to SpindleCollision, ToolTooWide, etc.)
        // to avoid unnecessary tool changes and spindle on/off for empty passes.
        var sortedLayers = request.CncGCodeByLayer
            .Where(kv => kv.Value.Split('\n').Any(l => {
                var t = l.TrimStart();
                return t.Length > 0 && !t.StartsWith(';');
            }))
            .Select(kv => kv.Key)
            .OrderBy(x => x)
            .ToList();

        for (var i = 0; i < sortedLayers.Count; i++)
        {
            var layer = sortedLayers[i];

            // Flush ALL print layers from (printStart+1) through (layer) inclusive.
            // Layer N is printed BEFORE the CNC operation that machines its top surface.
            var printFrag = ConcatLayers(segments, printStart + 1, layer, request.EnabledCustomBlocks);
            if (!string.IsNullOrWhiteSpace(printFrag))
            {
                output.AppendLine($"; --- Print layers {printStart + 1}–{layer} ---");
                // Insert a rapid travel to the first print position so the nozzle
                // doesn't extrude across the bed from the CNC park position.
                if (i > 0) // after a CNC pass, nozzle is at CNC position
                {
                    var travel = ExtractFirstXY(printFrag);
                    if (travel is not null)
                        output.AppendLine(travel);
                }
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

        // Flush any remaining print layers after the last machining event.
        // CuraEngine layers are 0-indexed: LAYER_COUNT=100 means layers 0–99.
        var lastLayer = request.TotalPrintLayers - 1;
        if (printStart < lastLayer)
        {
            var lastFrag = ConcatLayers(segments, printStart + 1, lastLayer,
                request.EnabledCustomBlocks);
            if (!string.IsNullOrWhiteSpace(lastFrag))
            {
                output.AppendLine($"; --- Print layers {printStart + 1}–{lastLayer} ---");
                // Travel to first print position after last CNC pass
                if (sortedLayers.Count > 0)
                {
                    var travel = ExtractFirstXY(lastFrag);
                    if (travel is not null)
                        output.AppendLine(travel);
                }
                output.Append(lastFrag);
                output.AppendLine();

                plan.AddStep(ProcessStep.CreatePrintStep(
                    plan.Id, stepIndex++,
                    printStart + 1, lastLayer, lastFrag));
            }
        }

        // JobEnd blocks
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
    /// Splits the raw print G-code into a dictionary keyed by layer index.
    /// Content for each layer includes the ;LAYER:N header line itself.
    /// The preamble (everything before the first ;LAYER:N) is stored at key -1.
    /// </summary>
    private static Dictionary<int, string> SplitByLayer(string printGCode)
    {
        var result = new Dictionary<int, string>();
        var lines = printGCode.Split('\n');
        // Start at -1 so the preamble (before any ;LAYER: marker) is stored
        // at key -1, keeping it separate from layer 0's actual content.
        var currentLayer = -1;
        var current = new StringBuilder();

        foreach (var rawLine in lines)
        {
            var match = LayerMarkerRegex().Match(rawLine);
            if (match.Success)
            {
                // Store accumulated lines for the layer we just finished
                if (current.Length > 0)
                    result[currentLayer] = current.ToString();

                currentLayer = int.Parse(match.Groups[1].Value);
                current.Clear();
            }
            current.AppendLine(rawLine);
        }

        // Store the final layer
        if (current.Length > 0)
            result[currentLayer] = current.ToString();

        return result;
    }

    /// <summary>
    /// Concatenates layer G-code fragments from 'from' to 'to' inclusive, appending
    /// any layer-interval blocks (<see cref="GCodeTrigger.EveryNLayers"/>) that fire
    /// on each layer. The block is emitted *after* the layer it fires on, so
    /// "every 10 layers" means "once layer 10 has finished printing".
    /// </summary>
    private static string ConcatLayers(
        Dictionary<int, string> segments, int from, int to,
        IReadOnlyList<CustomGCodeBlock>? blocks = null)
    {
        var periodic = blocks?
            .Where(b => b.IsEnabled && b.Trigger == GCodeTrigger.EveryNLayers)
            .OrderBy(b => b.SortOrder)
            .ToList();

        var sb = new StringBuilder();
        for (var l = from; l <= to; l++)
        {
            if (segments.TryGetValue(l, out var frag))
                sb.Append(frag);

            if (periodic is null || periodic.Count == 0) continue;

            foreach (var block in periodic.Where(b => b.FiresOnLayer(l)))
            {
                sb.AppendLine($"; --- Custom block: '{block.Name}' every {block.RepeatEveryNLayers} layers @ layer {l} ---");
                sb.AppendLine(block.GCodeContent);
                sb.AppendLine($"; --- End block: '{block.Name}' ---");
            }
        }
        return sb.ToString();
    }

    /// <summary>
    /// Scans a G-code fragment for the first G0 or G1 move with X/Y coordinates
    /// and returns a G0 rapid-travel command to that position. Used to insert
    /// a safe travel move before each print batch so the nozzle doesn't extrude
    /// across the bed from the CNC park position.
    /// </summary>
    private static string? ExtractFirstXY(string gcode)
    {
        foreach (var rawLine in gcode.Split('\n'))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line[0] == ';') continue;
            var upper = line.ToUpperInvariant();
            if (!(upper.StartsWith("G0 ") || upper.StartsWith("G1 ") ||
                  upper.StartsWith("G00 ") || upper.StartsWith("G01 "))) continue;
            var xm = System.Text.RegularExpressions.Regex.Match(upper, @"X([+-]?[\d.]+)");
            var ym = System.Text.RegularExpressions.Regex.Match(upper, @"Y([+-]?[\d.]+)");
            if (xm.Success || ym.Success)
            {
                var coords = "";
                if (xm.Success) coords += $" X{xm.Groups[1].Value}";
                if (ym.Success) coords += $" Y{ym.Groups[1].Value}";
                // Also extract Z if present (first layer move often includes Z)
                var zm = System.Text.RegularExpressions.Regex.Match(upper, @"Z([+-]?[\d.]+)");
                if (zm.Success) coords += $" Z{zm.Groups[1].Value}";
                return $"G0{coords} F6000 ; travel to print position";
            }
        }
        return null;
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
