using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Slicing;

/// <summary>
/// Merges separately-sliced per-bed G-code files into one final print G-code,
/// interleaving layers across beds according to a configurable step size.
///
/// Example with step=5 and 2 beds:
///   Bed1 layers 0–4, Bed2 layers 0–4, Bed1 layers 5–9, Bed2 layers 5–9, ...
///
/// Each bed's G-code is parsed into layer blocks via ;LAYER:N markers.
/// The merge preserves the header from bed 1 and the footer from the last bed.
/// Between bed switches, a comment marker is inserted for traceability.
/// </summary>
public sealed class MultiBedMerger
{
    private readonly ILogger<MultiBedMerger> _logger;

    public MultiBedMerger(ILogger<MultiBedMerger> logger) => _logger = logger;

    /// <summary>
    /// Marks a bed switch boundary. Resets E and inserts a non-extrusion travel
    /// to the next bed's first position so the nozzle doesn't extrude while
    /// crossing between beds. Scans the next layer block to find the first
    /// G0/G1 with coordinates and inserts a G0 travel there.
    /// </summary>
    private static void AppendBedSwitchMarker(StringBuilder sb, int fromBed, int toBed, string? nextLayerBlock = null)
    {
        sb.AppendLine($"; >>> BED SWITCH: Bed {fromBed + 1} → Bed {toBed + 1} <<<");
        sb.AppendLine("G92 E0 ; reset extruder position for bed switch");

        // Find the first XY position in the next bed's layer block and travel there
        if (nextLayerBlock is not null)
        {
            foreach (var rawLine in nextLayerBlock.Split('\n'))
            {
                var line = rawLine.Trim();
                if (line.Length == 0 || line[0] == ';') continue;
                var upper = line.ToUpperInvariant();
                if (!(upper.StartsWith("G0 ") || upper.StartsWith("G1 ") || upper.StartsWith("G00 ") || upper.StartsWith("G01 "))) continue;
                var xMatch = System.Text.RegularExpressions.Regex.Match(upper, @"X([+-]?[\d.]+)");
                var yMatch = System.Text.RegularExpressions.Regex.Match(upper, @"Y([+-]?[\d.]+)");
                if (xMatch.Success || yMatch.Success)
                {
                    var coords = "";
                    if (xMatch.Success) coords += $" X{xMatch.Groups[1].Value}";
                    if (yMatch.Success) coords += $" Y{yMatch.Groups[1].Value}";
                    sb.AppendLine($"G0{coords} ; travel to next bed position");
                    break;
                }
            }
        }
    }

    /// <summary>
    /// Merges per-bed G-code files into one output file.
    /// </summary>
    /// <param name="bedGCodePaths">Ordered list of per-bed G-code file paths (index = bed number).</param>
    /// <param name="outputPath">Path for the merged output file.</param>
    /// <param name="layerStep">How many layers to print on each bed before switching.</param>
    /// <param name="ct">Cancellation token.</param>
    public async Task MergeAsync(
        IReadOnlyList<string> bedGCodePaths,
        string outputPath,
        int layerStep,
        CancellationToken ct = default)
    {
        if (bedGCodePaths.Count == 0) return;
        if (bedGCodePaths.Count == 1)
        {
            // Single bed: just copy the file
            File.Copy(bedGCodePaths[0], outputPath, overwrite: true);
            return;
        }

        _logger.LogInformation("Merging {Count} beds with layer step {Step}", bedGCodePaths.Count, layerStep);

        // Parse each bed's G-code into header, layers, footer
        var beds = new List<ParsedBedGCode>();
        for (var i = 0; i < bedGCodePaths.Count; i++)
        {
            var text = await File.ReadAllTextAsync(bedGCodePaths[i], ct);
            beds.Add(ParseBedGCode(text, i));
            _logger.LogDebug("Bed {Bed}: {Layers} layers parsed", i + 1, beds[i].Layers.Count);
        }

        // Find the max layer count across all beds
        var maxLayers = beds.Max(b => b.Layers.Count);

        var sb = new StringBuilder();

        // ── Header from bed 1 ──────────────────────────────────────────────
        sb.AppendLine("; === Multi-Bed Merged G-code ===");
        sb.AppendLine($"; Beds: {beds.Count}");
        sb.AppendLine($"; Layer step: {layerStep}");
        sb.AppendLine($"; Max layers: {maxLayers}");
        sb.AppendLine("; ================================");
        sb.AppendLine();
        sb.Append(beds[0].Header);

        // ── Interleaved layers ─────────────────────────────────────────────
        var layerIndex = 0;
        var lastBedWithLayers = -1;
        while (layerIndex < maxLayers)
        {
            var endLayer = Math.Min(layerIndex + layerStep, maxLayers);

            for (var bi = 0; bi < beds.Count; bi++)
            {
                var bed = beds[bi];
                var hasLayers = false;

                for (var l = layerIndex; l < endLayer && l < bed.Layers.Count; l++)
                {
                    if (!hasLayers)
                    {
                        // Insert E reset + travel when switching beds
                        if (lastBedWithLayers >= 0 && lastBedWithLayers != bi)
                        {
                            var firstLayer = layerIndex < bed.Layers.Count ? bed.Layers[layerIndex] : null;
                            AppendBedSwitchMarker(sb, lastBedWithLayers, bi, firstLayer);
                        }
                        sb.AppendLine();
                        sb.AppendLine($"; --- Bed {bi + 1}, layers {layerIndex}–{endLayer - 1} ---");
                        hasLayers = true;
                    }
                    sb.Append(bed.Layers[l]);
                }
                if (hasLayers) lastBedWithLayers = bi;
            }

            layerIndex = endLayer;
        }

        // ── Footer from last bed ───────────────────────────────────────────
        sb.AppendLine();
        sb.Append(beds[^1].Footer);

        // Ensure output directory exists
        var outDir = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrWhiteSpace(outDir)) Directory.CreateDirectory(outDir);

        await File.WriteAllTextAsync(outputPath, sb.ToString(), ct);

        _logger.LogInformation("Multi-bed merge complete: {Path} ({Beds} beds, {Layers} max layers, step {Step})",
            outputPath, beds.Count, maxLayers, layerStep);
    }

    /// <summary>
    /// Merges per-bed CNC toolpath files into one output file.
    /// Uses CNC-specific parsing ("; ── Layer N" markers instead of ";LAYER:N").
    /// </summary>
    public async Task MergeCncAsync(
        IReadOnlyList<string> cncPaths,
        string outputPath,
        int layerStep,
        CancellationToken ct = default)
    {
        if (cncPaths.Count == 0) return;
        if (cncPaths.Count == 1)
        {
            File.Copy(cncPaths[0], outputPath, overwrite: true);
            return;
        }

        _logger.LogInformation("Merging {Count} CNC beds with layer step {Step}", cncPaths.Count, layerStep);

        var beds = new List<ParsedBedGCode>();
        for (var i = 0; i < cncPaths.Count; i++)
        {
            var text = await File.ReadAllTextAsync(cncPaths[i], ct);
            beds.Add(ParseCncGCode(text, i));
            _logger.LogDebug("CNC Bed {Bed}: {Layers} layer blocks", i + 1, beds[i].Layers.Count);
        }

        var maxLayers = beds.Max(b => b.Layers.Count);
        var sb = new StringBuilder();

        sb.AppendLine("; === Multi-Bed CNC Merged Toolpath ===");
        sb.AppendLine($"; Beds: {beds.Count}");
        sb.AppendLine($"; Layer step: {layerStep}");
        sb.AppendLine($"; Max CNC layers: {maxLayers}");
        sb.AppendLine("; =====================================");
        sb.AppendLine();
        sb.Append(beds[0].Header);

        var layerIndex = 0;
        while (layerIndex < maxLayers)
        {
            var endLayer = Math.Min(layerIndex + layerStep, maxLayers);
            for (var bi = 0; bi < beds.Count; bi++)
            {
                var bed = beds[bi];
                var hasLayers = false;
                for (var l = layerIndex; l < endLayer && l < bed.Layers.Count; l++)
                {
                    if (!hasLayers)
                    {
                        sb.AppendLine();
                        sb.AppendLine($"; --- CNC Bed {bi + 1}, layers {layerIndex}–{endLayer - 1} ---");
                        hasLayers = true;
                    }
                    sb.Append(bed.Layers[l]);
                }
            }
            layerIndex = endLayer;
        }

        var outDir = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrWhiteSpace(outDir)) Directory.CreateDirectory(outDir);
        await File.WriteAllTextAsync(outputPath, sb.ToString(), ct);

        _logger.LogInformation("CNC merge complete: {Path} ({Beds} beds, {Layers} max layers)",
            outputPath, beds.Count, maxLayers);
    }

    /// <summary>
    /// Merges per-bed print AND CNC G-code into one hybrid output file using the
    /// correct hybrid sequencing:
    ///   For each N-layer interval:
    ///     1. Print N layers on Bed 1
    ///     2. Print N layers on Bed 2
    ///     3. CNC those N layers on Bed 1
    ///     4. CNC those N layers on Bed 2
    ///   Repeat until complete.
    /// </summary>
    /// <summary>Custom G-code blocks keyed by trigger point.</summary>
    public sealed class CustomBlocks
    {
        public string BeforePrinting { get; init; } = "";
        public string AfterPrinting { get; init; } = "";
        public string BeforeMachining { get; init; } = "";
        public string AfterMachining { get; init; } = "";
        public string JobStart { get; init; } = "";
        public string JobEnd { get; init; } = "";
    }

    public async Task MergeHybridAsync(
        IReadOnlyList<string> printPaths,
        IReadOnlyList<string> cncPaths,
        int machineEveryN,
        string outputPath,
        CancellationToken ct = default)
        => await MergeHybridAsync(printPaths, cncPaths, machineEveryN, outputPath, null, ct);

    public async Task MergeHybridAsync(
        IReadOnlyList<string> printPaths,
        IReadOnlyList<string> cncPaths,
        int machineEveryN,
        string outputPath,
        CustomBlocks? blocks,
        CancellationToken ct = default)
    {
        if (printPaths.Count == 0) return;

        _logger.LogInformation(
            "Hybrid merge: {Beds} beds, machineEveryN={N}, {CncCount} CNC files",
            printPaths.Count, machineEveryN, cncPaths.Count);

        // Parse print G-code per bed
        var printBeds = new List<ParsedBedGCode>();
        for (var i = 0; i < printPaths.Count; i++)
        {
            var text = await File.ReadAllTextAsync(printPaths[i], ct);
            printBeds.Add(ParseBedGCode(text, i));
            _logger.LogDebug("Print Bed {Bed}: {Layers} layers", i + 1, printBeds[i].Layers.Count);
        }

        // Parse CNC G-code per bed (CNC files use ";LAYER:" or "; ── Layer" markers)
        var cncBeds = new List<ParsedBedGCode>();
        for (var i = 0; i < cncPaths.Count; i++)
        {
            var text = await File.ReadAllTextAsync(cncPaths[i], ct);
            cncBeds.Add(ParseCncGCode(text, i));
            _logger.LogDebug("CNC Bed {Bed}: {Layers} layer blocks", i + 1, cncBeds[i].Layers.Count);
        }

        var maxPrintLayers = printBeds.Max(b => b.Layers.Count);
        var maxCncLayers = cncBeds.Count > 0 ? cncBeds.Max(b => b.Layers.Count) : 0;
        var maxLayers = Math.Max(maxPrintLayers, maxCncLayers);

        var sb = new StringBuilder();

        // ── File header ──────────────────────────────────────────────────────
        sb.AppendLine("; ========================================================");
        sb.AppendLine("; HYBRID MULTI-BED G-CODE");
        sb.AppendLine("; ========================================================");
        sb.AppendLine($"; Beds           : {printBeds.Count}");
        sb.AppendLine($"; Machine every N : {machineEveryN} layers");
        sb.AppendLine($"; Print layers   : {maxPrintLayers} (max across beds)");
        sb.AppendLine($"; CNC layers     : {maxCncLayers} (max across beds)");
        sb.AppendLine($"; Total intervals: {(int)Math.Ceiling((double)maxLayers / machineEveryN)}");
        sb.AppendLine(";");
        sb.AppendLine("; Sequence per interval:");
        sb.AppendLine(";   1. PRINT N layers on each bed (Bed 1, Bed 2, …)");
        sb.AppendLine(";   2. CNC   N layers on each bed (Bed 1, Bed 2, …)");
        sb.AppendLine(";   3. Repeat until complete");
        sb.AppendLine($"; Generated      : {DateTime.UtcNow:u}");
        sb.AppendLine("; ========================================================");
        sb.AppendLine();

        // ── Job Start ────────────────────────────────────────────────────
        if (blocks?.JobStart is { Length: > 0 })
        {
            // User-provided Job Start replaces the default Cura header
            sb.AppendLine("; === Job Start (user-defined) ===");
            sb.AppendLine(blocks.JobStart);
            sb.AppendLine("; === End Job Start ===");
            sb.AppendLine();
        }
        else
        {
            // Default: use Cura's header from Bed 1 (temps, homing, etc.)
            sb.AppendLine("; --- PRINT PREAMBLE (from Bed 1) ---");
            sb.Append(printBeds[0].Header);

            // CNC preamble (tool info, spindle start position)
            if (cncBeds.Count > 0 && cncBeds[0].Header.Length > 0)
            {
                sb.AppendLine();
                sb.AppendLine("; --- CNC PREAMBLE (from Bed 1 toolpath) ---");
                sb.Append(cncBeds[0].Header);
            }
        }

        // ── Hybrid interleaved sequence ─────────────────────────────────
        var layerIndex = 0;
        var intervalNum = 0;
        var lastPrintBed = -1;
        while (layerIndex < maxLayers)
        {
            var endLayer = Math.Min(layerIndex + machineEveryN, maxLayers);
            intervalNum++;

            sb.AppendLine();
            sb.AppendLine($"; ============================================================");
            sb.AppendLine($"; INTERVAL {intervalNum}: layers {layerIndex}–{endLayer - 1}");
            sb.AppendLine($"; ============================================================");

            // BeforePrinting custom G-code
            if (blocks?.BeforePrinting is { Length: > 0 })
            {
                sb.AppendLine();
                sb.AppendLine($"; === Custom G-code: Before Printing (interval {intervalNum}) ===");
                sb.AppendLine(blocks.BeforePrinting);
                sb.AppendLine("; === End Custom G-code ===");
            }

            // Phase 1: Print N layers on each bed
            for (var bi = 0; bi < printBeds.Count; bi++)
            {
                var bed = printBeds[bi];
                var hasLayers = false;
                for (var l = layerIndex; l < endLayer && l < bed.Layers.Count; l++)
                {
                    if (!hasLayers)
                    {
                        if (lastPrintBed >= 0 && lastPrintBed != bi)
                        {
                            var firstLayer = layerIndex < bed.Layers.Count ? bed.Layers[layerIndex] : null;
                            AppendBedSwitchMarker(sb, lastPrintBed, bi, firstLayer);
                        }
                        sb.AppendLine();
                        sb.AppendLine($"; --- PRINT Bed {bi + 1}, layers {layerIndex}–{endLayer - 1} ---");
                        hasLayers = true;
                    }
                    sb.Append(bed.Layers[l]);
                }
                if (hasLayers) lastPrintBed = bi;
            }

            // AfterPrinting custom G-code
            if (blocks?.AfterPrinting is { Length: > 0 })
            {
                sb.AppendLine();
                sb.AppendLine($"; === Custom G-code: After Printing (interval {intervalNum}) ===");
                sb.AppendLine(blocks.AfterPrinting);
                sb.AppendLine("; === End Custom G-code ===");
            }

            // BeforeMachining custom G-code
            if (cncBeds.Count > 0 && blocks?.BeforeMachining is { Length: > 0 })
            {
                sb.AppendLine();
                sb.AppendLine($"; === Custom G-code: Before Machining (interval {intervalNum}) ===");
                sb.AppendLine(blocks.BeforeMachining);
                sb.AppendLine("; === End Custom G-code ===");
            }

            // Phase 2: CNC those N layers on each bed
            if (cncBeds.Count > 0)
            {
                for (var bi = 0; bi < cncBeds.Count; bi++)
                {
                    var bed = cncBeds[bi];
                    var hasLayers = false;
                    for (var l = layerIndex; l < endLayer && l < bed.Layers.Count; l++)
                    {
                        if (!hasLayers)
                        {
                            sb.AppendLine();
                            sb.AppendLine($"; --- CNC Bed {bi + 1}, layers {layerIndex}–{endLayer - 1} ---");
                            hasLayers = true;
                        }
                        sb.Append(bed.Layers[l]);
                    }
                }

                // AfterMachining custom G-code
                if (blocks?.AfterMachining is { Length: > 0 })
                {
                    sb.AppendLine();
                    sb.AppendLine($"; === Custom G-code: After Machining (interval {intervalNum}) ===");
                    sb.AppendLine(blocks.AfterMachining);
                    sb.AppendLine("; === End Custom G-code ===");
                }
            }

            layerIndex = endLayer;
        }

        // ── Job End custom G-code ───────────────────────────────────────
        // ── Job End ──────────────────────────────────────────────────────
        sb.AppendLine();
        sb.AppendLine("; ========================================================");
        sb.AppendLine("; END OF HYBRID G-CODE");
        sb.AppendLine($"; {intervalNum} intervals completed across {printBeds.Count} beds");
        sb.AppendLine("; ========================================================");
        sb.AppendLine();

        if (blocks?.JobEnd is { Length: > 0 })
        {
            // User-provided Job End replaces the default Cura footer
            sb.AppendLine("; === Job End (user-defined) ===");
            sb.AppendLine(blocks.JobEnd);
            sb.AppendLine("; === End Job End ===");
        }
        else
        {
            // Default: use Cura's footer from last bed
            sb.Append(printBeds[^1].Footer);
        }

        var outDir = Path.GetDirectoryName(outputPath);
        if (!string.IsNullOrWhiteSpace(outDir)) Directory.CreateDirectory(outDir);
        await File.WriteAllTextAsync(outputPath, sb.ToString(), ct);

        _logger.LogInformation(
            "Hybrid merge complete: {Path} ({Beds} beds, {Intervals} intervals, N={N})",
            outputPath, printBeds.Count, intervalNum, machineEveryN);
    }

    // ── Parsing ─────────────────────────────────────────────────────────────

    private sealed class ParsedBedGCode
    {
        public required string Header { get; init; }
        public required List<string> Layers { get; init; } // each entry = full text of one layer
        public required string Footer { get; init; }
        public required int BedIndex { get; init; }
    }

    private static readonly Regex LayerRx = new(@"^;LAYER:(\d+)", RegexOptions.Multiline);

    /// <summary>
    /// Parses CNC toolpath G-code into layer blocks.
    /// CNC files use "; ── Layer N" markers instead of ";LAYER:N".
    /// Each layer block includes everything from the marker to the next marker.
    /// </summary>
    private static ParsedBedGCode ParseCncGCode(string gcode, int bedIndex)
    {
        var header = new StringBuilder();
        var layers = new List<string>();
        var current = new StringBuilder();
        var inHeader = true;
        // CNC layer marker: "; ── Layer N" or "; ── Layer N ("
        var layerMarkerRx = new Regex(@"^;\s*──\s*Layer\s+\d+", RegexOptions.IgnoreCase);

        foreach (var rawLine in gcode.Split('\n'))
        {
            var line = rawLine.TrimEnd('\r');

            if (layerMarkerRx.IsMatch(line))
            {
                if (inHeader)
                {
                    inHeader = false;
                    header.Append(current.ToString());
                    current.Clear();
                }
                else if (current.Length > 0)
                {
                    layers.Add(current.ToString());
                    current.Clear();
                }
                current.AppendLine(line);
                continue;
            }

            current.AppendLine(line);
        }

        // Flush
        if (inHeader)
        {
            header.Append(current.ToString());
        }
        else if (current.Length > 0)
        {
            layers.Add(current.ToString());
        }

        return new ParsedBedGCode
        {
            Header = header.ToString(),
            Layers = layers,
            Footer = string.Empty,
            BedIndex = bedIndex,
        };
    }

    private static ParsedBedGCode ParseBedGCode(string gcode, int bedIndex)
    {
        var header = new StringBuilder();
        var layers = new List<string>();
        var footer = new StringBuilder();
        var current = new StringBuilder();
        var inHeader = true;
        var inFooter = false;

        foreach (var rawLine in gcode.Split('\n'))
        {
            var line = rawLine.TrimEnd('\r');

            // Detect footer (M84 or ;End of Gcode)
            if (!inHeader && !inFooter)
            {
                var t = line.TrimStart();
                if (t.StartsWith("M84", StringComparison.OrdinalIgnoreCase)
                    || t.StartsWith(";End of Gcode", StringComparison.OrdinalIgnoreCase))
                {
                    // Flush current layer
                    if (current.Length > 0)
                        layers.Add(current.ToString());
                    current.Clear();
                    inFooter = true;
                    footer.AppendLine(line);
                    continue;
                }
            }

            if (inFooter) { footer.AppendLine(line); continue; }

            // Detect first layer
            if (inHeader && line.StartsWith(";LAYER:", StringComparison.Ordinal))
            {
                inHeader = false;
                header.Append(current.ToString());
                current.Clear();
                current.AppendLine(line);
                continue;
            }

            if (inHeader) { current.AppendLine(line); continue; }

            // Detect next layer
            if (line.StartsWith(";LAYER:", StringComparison.Ordinal))
            {
                if (current.Length > 0)
                    layers.Add(current.ToString());
                current.Clear();
                current.AppendLine(line);
                continue;
            }

            current.AppendLine(line);
        }

        // Flush
        if (!inFooter && current.Length > 0)
            layers.Add(current.ToString());

        return new ParsedBedGCode
        {
            Header = header.ToString(),
            Layers = layers,
            Footer = footer.ToString(),
            BedIndex = bedIndex,
        };
    }
}
