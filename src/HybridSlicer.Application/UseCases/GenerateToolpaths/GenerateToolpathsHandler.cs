using System.Globalization;
using System.Text;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain;
using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.Exceptions;
using HybridSlicer.Domain.ValueObjects;
using MediatR;
using Microsoft.Extensions.Logging;
// UnmachinableRegion is defined in IToolpathPlanner.cs (HybridSlicer.Application.Interfaces)

namespace HybridSlicer.Application.UseCases.GenerateToolpaths;

public sealed class GenerateToolpathsHandler : IRequestHandler<GenerateToolpathsCommand, GenerateToolpathsResult>
{
    private readonly IPrintJobRepository     _jobs;
    private readonly IPrintProfileRepository _printProfiles;
    private readonly IMachineProfileRepository _machines;
    private readonly ICncToolRepository      _tools;
    private readonly IToolpathPlanner        _planner;
    private readonly ISafetyValidator        _safety;
    private readonly ICuraGCodeParser        _parser;
    private readonly IMachineCoordinateTranslator _coordTranslator;
    private readonly ILogger<GenerateToolpathsHandler> _logger;

    public GenerateToolpathsHandler(
        IPrintJobRepository jobs,
        IPrintProfileRepository printProfiles,
        IMachineProfileRepository machines,
        ICncToolRepository tools,
        IToolpathPlanner planner,
        ISafetyValidator safety,
        ICuraGCodeParser parser,
        IMachineCoordinateTranslator coordTranslator,
        ILogger<GenerateToolpathsHandler> logger)
    {
        _jobs          = jobs;
        _printProfiles = printProfiles;
        _machines      = machines;
        _tools         = tools;
        _planner       = planner;
        _safety        = safety;
        _parser        = parser;
        _coordTranslator = coordTranslator;
        _logger        = logger;
    }

    public async Task<GenerateToolpathsResult> Handle(
        GenerateToolpathsCommand cmd, CancellationToken ct)
    {
        // ── Load required entities ────────────────────────────────────────────
        var job = await _jobs.GetByIdAsync(cmd.JobId, ct)
            ?? throw new DomainException("JOB_NOT_FOUND", $"Job {cmd.JobId} not found.");

        if (job.TotalPrintLayers is null)
            throw new DomainException("NOT_SLICED", "Job must be sliced before toolpaths can be generated.");

        if (job.PrintGCodePath is null || !File.Exists(job.PrintGCodePath))
            throw new DomainException("NO_GCODE", "Print G-code file not found. Re-slice the job.");

        var machine = await _machines.GetByIdAsync(job.MachineProfileId, ct)
            ?? throw new DomainException("MACHINE_NOT_FOUND", $"Machine profile {job.MachineProfileId} not found.");

        var tool = await _tools.GetByIdAsync(cmd.CncToolId, ct)
            ?? throw new DomainException("TOOL_NOT_FOUND", $"CNC tool {cmd.CncToolId} not found.");

        var profile = await _printProfiles.GetByIdAsync(job.PrintProfileId, ct)
            ?? throw new DomainException("PROFILE_NOT_FOUND", $"Print profile {job.PrintProfileId} not found.");

        // CNC spindle offset is applied directly to every emitted coordinate, so the
        // gantry physically moves to (wall + offset) when the spindle is the active
        // tool. The controller must NOT also apply a tool offset (G10/G43) for the
        // spindle — that would double the shift.
        var cncOffset = machine.CncOffset;

        // ── Depth-of-cut validation ───────────────────────────────────────────
        var warnings = new List<string>();
        var h = profile.LayerHeightMm;
        var N = cmd.MachineEveryNLayers;
        var bandMm = N * h;
        var overlap = cmd.TipOverlapMm > 0 ? cmd.TipOverlapMm : tool.TipOverlapMm;
        var margin = tool.SafetyMarginMm;
        var engaged = bandMm + overlap;

        if (bandMm > tool.MaxDepthOfCutMm && tool.MaxDepthOfCutMm > 0)
        {
            var msg = $"Band {bandMm:F1} mm ({N} layers × {h} mm) exceeds tool max depth of cut {tool.MaxDepthOfCutMm:F1} mm. " +
                "Risk of tool breakage or poor finish.";
            warnings.Add(msg);
            _logger.LogWarning(msg);
        }

        // ── Manual frequency pre-validation ───────────────────────────────────
        // Compute hard limits from tool geometry and block if exceeded.
        if (!cmd.AutoMachiningFrequency)
        {
            var errors = new List<string>();

            // Check 1: Flute length
            var maxBandFlute = tool.FluteLengthMm > 0
                ? tool.FluteLengthMm - overlap - margin : double.MaxValue;
            var nFlute = tool.FluteLengthMm > 0 && h > 0
                ? (int)Math.Floor(maxBandFlute / h) : int.MaxValue;
            if (tool.FluteLengthMm > 0 && bandMm > maxBandFlute)
            {
                errors.Add(
                    $"Flute length exceeded: band {bandMm:F1} mm + overlap {overlap:F1} mm + margin {margin:F1} mm " +
                    $"= {engaged + margin:F1} mm engaged, but flute is only {tool.FluteLengthMm:F1} mm. " +
                    $"The shank would rub against the part.\n" +
                    $"  Max interval for this tool: every {Math.Max(1, nFlute)} layers ({maxBandFlute:F1} mm band).");
            }

            // Check 2: Spindle clearance
            var maxBandSpindle = tool.ToolLengthMm > 0
                ? tool.ToolLengthMm - overlap - margin : double.MaxValue;
            var nSpindle = tool.ToolLengthMm > 0 && h > 0
                ? (int)Math.Floor(maxBandSpindle / h) : int.MaxValue;
            if (tool.ToolLengthMm > 0 && engaged + margin >= tool.ToolLengthMm)
            {
                errors.Add(
                    $"Spindle collision risk: engaged depth {engaged + margin:F1} mm " +
                    $"exceeds tip-to-spindle distance {tool.ToolLengthMm:F1} mm. " +
                    $"The spindle body would hit the part.\n" +
                    $"  Max interval for this tool: every {Math.Max(1, nSpindle)} layers ({maxBandSpindle:F1} mm band).");
            }

            if (errors.Count > 0)
            {
                var nAllowed = Math.Max(1, Math.Min(nFlute, nSpindle));
                var autoPassEst = tool.FluteLengthMm > 0 && h > 0
                    ? (int)Math.Ceiling(job.TotalPrintLayers!.Value * h / maxBandFlute)
                    : job.TotalPrintLayers!.Value;
                var fixedPassEst = job.TotalPrintLayers!.Value / nAllowed;

                var recommendation =
                    $"\nOptions:\n" +
                    $"  1. Fixed interval: every {nAllowed} layer(s) ({nAllowed * h:F1} mm band) — " +
                    $"about {fixedPassEst} machining passes.\n" +
                    $"  2. AutoFreq: automatic interval based on tool limits — " +
                    $"about {autoPassEst} passes, adapts to geometry. (Recommended)";

                var fullMsg = $"Machining every {N} layers ({bandMm:F1} mm) is not achievable with {tool.Name}.\n\n" +
                    string.Join("\n\n", errors) + "\n" + recommendation;

                throw new DomainException("MACHINING_VALIDATION_FAILED", fullMsg);
            }
        }

        // ── Parse Cura G-code for wall paths ─────────────────────────────────
        _logger.LogInformation("Parsing Cura G-code: {Path}", job.PrintGCodePath);
        var gcodeText = await File.ReadAllTextAsync(job.PrintGCodePath, ct);
        var parsed    = await Task.Run(() => _parser.Parse(gcodeText), ct);

        _logger.LogInformation(
            "Parsed {Count} layers from Cura G-code. WALL-OUTER found in {OW} layers.",
            parsed.Layers.Count,
            parsed.Layers.Values.Count(l => l.OuterWallPaths.Count > 0));

        // ── Contour-change validation (checks 3 & 4) ─────────────────────────
        // Compare bounding boxes between the top and bottom of each band.
        // If the contour shifts outward (overhang) or inward (step) by more than
        // the tolerance, the band can't be machined with a single contour.
        if (!cmd.AutoMachiningFrequency && N > 1)
        {
            var tol = 0.05; // mm — max gouge or uncut stock allowed
            var contourWarnings = new List<string>();

            for (var bandStart = 0; bandStart < job.TotalPrintLayers!.Value; bandStart += N)
            {
                var bandEnd = Math.Min(bandStart + N, job.TotalPrintLayers.Value) - 1;
                // Get bounding box of bottom and top of band
                if (!parsed.Layers.TryGetValue(bandStart, out var bottomLayer) ||
                    !parsed.Layers.TryGetValue(bandEnd, out var topLayer))
                    continue;
                if (bottomLayer.OuterWallPaths.Count == 0 || topLayer.OuterWallPaths.Count == 0)
                    continue;

                // Compute bounding box for each
                double bMinX = double.MaxValue, bMaxX = double.MinValue, bMinY = double.MaxValue, bMaxY = double.MinValue;
                foreach (var path in bottomLayer.OuterWallPaths)
                    foreach (var (px, py) in path) { if (px < bMinX) bMinX = px; if (px > bMaxX) bMaxX = px; if (py < bMinY) bMinY = py; if (py > bMaxY) bMaxY = py; }

                double tMinX = double.MaxValue, tMaxX = double.MinValue, tMinY = double.MaxValue, tMaxY = double.MinValue;
                foreach (var path in topLayer.OuterWallPaths)
                    foreach (var (px, py) in path) { if (px < tMinX) tMinX = px; if (px > tMaxX) tMaxX = px; if (py < tMinY) tMinY = py; if (py > tMaxY) tMaxY = py; }

                // Check 3: Outward expansion (overhang) — top is wider than bottom
                var overhangX = Math.Max(tMaxX - bMaxX, bMinX - tMinX);
                var overhangY = Math.Max(tMaxY - bMaxY, bMinY - tMinY);
                var maxOverhang = Math.Max(overhangX, overhangY);
                if (maxOverhang > tol)
                {
                    var zBot = (bandStart + 1) * h;
                    var zTop = (bandEnd + 1) * h;
                    contourWarnings.Add(
                        $"Overhang: layers {bandStart}–{bandEnd} (Z {zBot:F1}–{zTop:F1} mm), " +
                        $"upper layers extend {maxOverhang:F2} mm beyond lower layers. " +
                        $"Machining the lower wall would gouge the overhang (allowed: {tol} mm).");
                }

                // Check 4: Inward contraction (step) — top is narrower, lower layers stick out
                var inwardX = Math.Max(bMaxX - tMaxX, tMinX - bMinX);
                var inwardY = Math.Max(bMaxY - tMaxY, tMinY - bMinY);
                var maxInward = Math.Max(inwardX, inwardY);
                if (maxInward > tol)
                {
                    var zBot = (bandStart + 1) * h;
                    var zTop = (bandEnd + 1) * h;
                    contourWarnings.Add(
                        $"Inward step: layers {bandStart}–{bandEnd} (Z {zBot:F1}–{zTop:F1} mm), " +
                        $"lower layers extend {maxInward:F2} mm beyond upper contour. " +
                        $"A single contour pass would leave {maxInward:F2} mm uncut stock (allowed: {tol} mm).");
                }
            }

            if (contourWarnings.Count > 0)
            {
                foreach (var w in contourWarnings) warnings.Add(w);
                _logger.LogWarning("Contour-change warnings for manual interval {N}: {Count} issues found",
                    N, contourWarnings.Count);
            }
        }

        // ── Mark job and prepare output ───────────────────────────────────────
        job.AssignCncTool(cmd.CncToolId);
        job.MarkGeneratingToolpaths();
        await _jobs.UpdateAsync(job, ct);

        var machinedLayers      = new List<int>();
        var allUnmachinableRegions = new List<UnmachinableRegion>();
        var gcodeBuilder        = new StringBuilder();
        gcodeBuilder.AppendLine($"; HybridSlicer v{Domain.AppVersion.Current}");
        gcodeBuilder.AppendLine($"; CNC Toolpath G-code — Job: {job.Name}");
        var spindleRpm = cmd.SpindleRpmOverride ?? tool.RecommendedRpm;
        gcodeBuilder.AppendLine($"; Tool     : {tool.Name}  Ø{tool.DiameterMm} mm  Flute: {tool.FluteLengthMm} mm  Tool length: {tool.ToolLengthMm} mm  Feed: {tool.RecommendedFeedMmPerMin} mm/min  RPM: {spindleRpm}{(cmd.SpindleRpmOverride.HasValue ? $" (override — tool default: {tool.RecommendedRpm})" : "")}");
        gcodeBuilder.AppendLine($"; Nozzle   : Ø{profile.LineWidthMm} mm  Layer height: {profile.LayerHeightMm} mm");
        gcodeBuilder.AppendLine($"; Interval : {(cmd.AutoMachiningFrequency ? "AUTO (flute-based)" : $"every {cmd.MachineEveryNLayers} part layer(s) (support-only layers excluded)")}  Axial depth: {bandMm:F3} mm");
        gcodeBuilder.AppendLine($"; Options  : MachineInnerWalls={cmd.MachineInnerWalls}  AvoidSupports={cmd.AvoidSupports}  SupportClearance={cmd.SupportClearanceMm:F2} mm  AutoFreq={cmd.AutoMachiningFrequency}");
        gcodeBuilder.AppendLine(cmd.ZSafetyOffsetMm > 0
            ? $"; Z Offset  : +{cmd.ZSafetyOffsetMm:F3} mm — all machining passes raised by this amount above nominal layer height"
            : $"; Z Offset  : none (machining at nominal layer height)");
        gcodeBuilder.AppendLine($"; Spindle   : tip→spindle = {tool.ToolLengthMm:F1} mm  (spindle clears Z+{tool.ToolLengthMm:F1} mm above tip position)");
        gcodeBuilder.AppendLine($"; CRC      : offset = tool_radius({tool.DiameterMm / 2:F3}) + nozzle_radius({profile.LineWidthMm / 2:F3}) = {(tool.DiameterMm + profile.LineWidthMm) / 2:F3} mm outward");
        gcodeBuilder.AppendLine($"; Source   : Cura WALL-OUTER paths (parsed from print.gcode)");
        gcodeBuilder.AppendLine($"; Generated: {DateTime.UtcNow:u}");
        gcodeBuilder.AppendLine();

        // ── Resolve spindle start / end positions ─────────────────────────────
        var resolvedStartZ = cmd.SpindleStartZ ?? machine.SafeClearanceHeightMm;
        var resolvedEndX   = cmd.SpindleEndX;
        var resolvedEndY   = cmd.SpindleEndY;
        var resolvedEndZ   = cmd.SpindleEndZ ?? resolvedStartZ;
        var inv = System.Globalization.CultureInfo.InvariantCulture;

        // No preamble move — each contour pass handles its own safe retract,
        // travel to lead-in position, and plunge. A preamble to (0,0) would cause
        // an unnecessary trip to the machine origin before the first cut.
        gcodeBuilder.AppendLine();

        // ── Compute which layers to machine ───────────────────────────────────
        // Auto mode: true geometry-aware scheduling. Machines when ANY of three conditions:
        //
        //   (1) FLUTE REACH: accumulated uncut height ≥ 80% of flute length.
        //       This is the hard upper bound — after this point the tool shank (above the
        //       flute) would collide with material printed since the last machining event.
        //
        //   (2) LOOK-AHEAD ACCESS BLOCKING: scan forward flute_length / layer_height layers.
        //       If ANY upcoming layer extends outward beyond the current layer + CRC offset
        //       on any side, machine NOW before that layer is printed. Once a wider layer is
        //       printed, the tool can no longer reach back to machine the current layer's wall.
        //       → This is what produces layer-by-layer machining on sphere tops / expanding
        //         geometry and large intervals on cylinders (no outward expansion ahead).
        //
        //   (3) SPINDLE COLLISION: spindle body would exceed machine Z travel limit.
        //
        // Result: cylinder → large regular intervals (no access blocking ahead).
        //         Expanding sphere (base → equator) → fires immediately at each layer.
        //         Shrinking sphere (equator → top) → reverts to flute-based intervals.
        //         Mushroom (narrow stem, wide cap) → dense when cap begins, sparse on stem.
        //
        // Manual mode: every N layers as configured.
        IEnumerable<int> layersToMachine;
        if (cmd.AutoMachiningFrequency)
        {
            // Pre-compute per-layer bounding box extents from outer wall paths
            var layerBounds = new Dictionary<int, (double MinX, double MaxX, double MinY, double MaxY, double Area)>();
            for (var li = 0; li <= job.TotalPrintLayers!.Value; li++)
            {
                if (!parsed.Layers.TryGetValue(li, out var ld) || ld.OuterWallPaths.Count == 0)
                {
                    layerBounds[li] = (0, 0, 0, 0, -1);
                    continue;
                }
                var mnX = double.MaxValue; var mxX = double.MinValue;
                var mnY = double.MaxValue; var mxY = double.MinValue;
                foreach (var path in ld.OuterWallPaths)
                foreach (var (px, py) in path)
                {
                    if (px < mnX) mnX = px; if (px > mxX) mxX = px;
                    if (py < mnY) mnY = py; if (py > mxY) mxY = py;
                }
                var ar = (mxX > mnX && mxY > mnY) ? (mxX - mnX) * (mxY - mnY) : 0;
                layerBounds[li] = (mnX, mxX, mnY, mxY, ar);
            }

            // CRC offset: tool centre sits this far outside the printed wall's nozzle path.
            // Access is blocked when an upcoming layer protrudes further outward than this.
            var crcOffset       = tool.RadiusMm + profile.LineWidthMm / 2.0;
            // How many layers forward the look-ahead scans (1 flute length of height)
            // Auto-frequency safety limits (all in mm, not layers, to support variable layer heights).
            // The effective tip overlap comes from the tool definition (per-tool setting).
            var tipOvl   = tool.TipOverlapMm;
            var autoMargin = tool.SafetyMarginMm;
            // Maximum band the tool can machine in one pass:
            //   usable flute - tip_overlap - safety_margin
            // Subtract one layer height as buffer so the trigger fires BEFORE the limit, not AT it.
            // Without this, discrete layer steps can push engaged 0.1-0.2mm over the flute limit.
            var maxBandMm = tool.FluteLengthMm > 0
                ? Math.Max(h, tool.FluteLengthMm - tipOvl - autoMargin - h)
                : double.MaxValue;
            // Maximum engaged depth before spindle body hits the part:
            //   tip-to-spindle distance - safety_margin
            var maxEngaged = tool.ToolLengthMm > 0
                ? Math.Max(profile.LayerHeightMm, tool.ToolLengthMm - autoMargin)
                : double.MaxValue;

            var fluteLayerCount = tool.FluteLengthMm > 0 && profile.LayerHeightMm > 0
                ? (int)Math.Ceiling(tool.FluteLengthMm / profile.LayerHeightMm)
                : 0;

            var autoLayers    = new List<int>();
            var lastMachinedZ = 0.0;
            var autoPartCount = 0; // track part layers for skip logic

            for (var layerIdx = 1; layerIdx <= job.TotalPrintLayers!.Value; layerIdx++)
            {
                var currentZ     = layerIdx * profile.LayerHeightMm;
                var autoBand     = currentZ - lastMachinedZ;
                var autoEngaged  = autoBand + tipOvl; // total depth the tool must reach
                var curaIdx    = layerIdx - 1;

                // Respect SkipMachiningLayers even in auto mode
                var hasWalls = parsed.Layers.TryGetValue(curaIdx, out var skipLd)
                    && (skipLd.OuterWallPaths.Count > 0 || skipLd.InnerWallPaths.Count > 0);
                if (hasWalls) autoPartCount++;
                if (autoPartCount <= cmd.SkipMachiningLayers) continue;

                // (1) Flute reach: trigger when the NEXT layer would push engaged over flute limit.
                // Check both the current engaged AND predict what the next layer's engaged would be.
                var nextEngaged = (currentZ + h) - lastMachinedZ + tipOvl;
                var fluteTriggered = tool.FluteLengthMm > 0
                    && (nextEngaged + autoMargin >= tool.FluteLengthMm);

                // (2) Look-ahead access blocking: scan upcoming layers within flute reach.
                //     If any future layer extends outward beyond current + crcOffset, the
                //     tool cannot reach back to this layer once that future layer is printed.
                var accessBlocked = false;
                if (fluteLayerCount > 0
                    && layerBounds.TryGetValue(curaIdx, out var curBnd) && curBnd.Area > 0)
                {
                    for (var la = 1; la <= fluteLayerCount && !accessBlocked; la++)
                    {
                        if (!layerBounds.TryGetValue(curaIdx + la, out var futBnd) || futBnd.Area <= 0)
                            continue;
                        var outward = Math.Max(
                            Math.Max(futBnd.MaxX - curBnd.MaxX, curBnd.MinX - futBnd.MinX),
                            Math.Max(futBnd.MaxY - curBnd.MaxY, curBnd.MinY - futBnd.MinY));
                        if (outward > crcOffset) accessBlocked = true;
                    }
                }

                // (3) Spindle clearance: engaged depth must stay below tip-to-spindle distance
                //     so the spindle body never touches the part top
                var spindleTriggered = tool.ToolLengthMm > 0 && autoEngaged >= maxEngaged;

                var hasPartGeometryAuto = layerBounds.TryGetValue(curaIdx, out var autoLayBnd) && autoLayBnd.Area > 0;

                if ((fluteTriggered || accessBlocked || spindleTriggered)
                    && autoBand >= profile.LayerHeightMm
                    && hasPartGeometryAuto)
                {
                    autoLayers.Add(layerIdx);
                    lastMachinedZ = currentZ;
                    _logger.LogDebug(
                        "AUTO layer {L}: flute={FT} access={AT} spindle={ST}  band={B:F2} engaged={E:F2} maxBand={MB:F2} maxEngaged={ME:F2}",
                        layerIdx, fluteTriggered, accessBlocked, spindleTriggered, autoBand, autoEngaged, maxBandMm, maxEngaged);
                }
            }
            layersToMachine = autoLayers;
            gcodeBuilder.AppendLine($"; AUTO machining: flute={tool.FluteLengthMm} mm  tipOverlap={tipOvl:F1} mm  margin={autoMargin:F1} mm  maxBand={maxBandMm:F1} mm  maxEngaged={maxEngaged:F1} mm");
            gcodeBuilder.AppendLine($"; AUTO machining: {autoLayers.Count} layers selected (geometry-driven)");
            gcodeBuilder.AppendLine();
            _logger.LogInformation(
                "Auto machining frequency (geometry-aware look-ahead): {Count} layers selected from {Total}",
                autoLayers.Count, job.TotalPrintLayers!.Value);
        }
        else
        {
            // Manual mode: machine after every N *part* layers.
            //
            // A "part layer" is any layer that contains real printed geometry — i.e. it has
            // at least one outer-wall or inner-wall path in the Cura G-code.
            // Support-only layers, draft-shield-only layers, and any layer that Cura emits
            // with no wall paths (e.g. a pure-SUPPORT or pure-SKIN layer at a bridging height)
            // do NOT count toward the N-layer interval.
            //
            // This ensures that when the user sets N=5, machining happens after every 5 layers
            // of the actual printed part — regardless of how many support-only layers Cura
            // interleaves between them.
            var manualLayers  = new List<int>();
            var partLayerCount = 0;
            for (var li = 1; li <= job.TotalPrintLayers!.Value; li++)
            {
                var ci = li - 1; // Cura uses 0-based layer indices
                if (!parsed.Layers.TryGetValue(ci, out var ld)) continue;
                if (ld.OuterWallPaths.Count == 0 && ld.InnerWallPaths.Count == 0) continue; // not a part layer

                partLayerCount++;
                if (partLayerCount <= cmd.SkipMachiningLayers) continue; // skip first N layers
                if ((partLayerCount - cmd.SkipMachiningLayers) % cmd.MachineEveryNLayers == 0)
                    manualLayers.Add(li);
            }
            // Always machine the last part layer so no band is left uncut at the top
            if (manualLayers.Count > 0 && manualLayers[^1] != job.TotalPrintLayers!.Value)
            {
                // Find the last part layer (might differ from TotalPrintLayers if last layers are support-only)
                for (var li = job.TotalPrintLayers!.Value; li >= 1; li--)
                {
                    var ci = li - 1;
                    if (parsed.Layers.TryGetValue(ci, out var ld) &&
                        (ld.OuterWallPaths.Count > 0 || ld.InnerWallPaths.Count > 0))
                    {
                        if (!manualLayers.Contains(li)) manualLayers.Add(li);
                        break;
                    }
                }
            }
            layersToMachine = manualLayers;
            _logger.LogInformation(
                "Manual machining schedule (every {N} part layers): {Sched} events from {Part} part layers ({Total} total)",
                cmd.MachineEveryNLayers, manualLayers.Count, partLayerCount, job.TotalPrintLayers!.Value);
            gcodeBuilder.AppendLine($"; MANUAL machining: every {cmd.MachineEveryNLayers} part layer(s)  " +
                                    $"Re-machine lower layers: {cmd.RemachineLowerLayers}  " +
                                    $"({partLayerCount} part layers / {job.TotalPrintLayers!.Value} total → {manualLayers.Count} event(s))");
            gcodeBuilder.AppendLine();
        }

        try
        {
            // Track the top-of-part Z from the previous machining pass.
            // The tool tip must go to the BOTTOM of the band (previousTopZ - tipOverlap),
            // not the top, so the flutes machine the full band height.
            //
            // When SkipMachiningLayers > 0, initialise previousTopZ to the Z height of
            // the last skipped layer so the first band doesn't cut into the skip zone.
            double previousTopZ = 0.0;
            int skipBoundaryLayer = 0; // 1-based layer number of the last skipped layer (0 = no skip)
            if (cmd.SkipMachiningLayers > 0)
            {
                var skippedPartCount = 0;
                for (var li = 1; li <= job.TotalPrintLayers!.Value; li++)
                {
                    var ci = li - 1;
                    if (!parsed.Layers.TryGetValue(ci, out var skipLd2)) continue;
                    if (skipLd2.OuterWallPaths.Count == 0 && skipLd2.InnerWallPaths.Count == 0) continue;
                    skippedPartCount++;
                    if (skippedPartCount == cmd.SkipMachiningLayers)
                    {
                        previousTopZ = skipLd2.ZHeightMm;
                        skipBoundaryLayer = li;
                        break;
                    }
                }
            }

            // Build a map from 1-based layer number to the ACTUAL Cura Z height,
            // so we never compute layer*h (which is wrong when layer 0 is at Z=h, not Z=0).
            var layerActualZ = new Dictionary<int, double>();
            for (var li = 1; li <= job.TotalPrintLayers!.Value; li++)
            {
                var ci = li - 1;
                if (parsed.Layers.TryGetValue(ci, out var ld))
                    layerActualZ[li] = ld.ZHeightMm;
                else
                    layerActualZ[li] = li * h; // fallback if layer not parsed
            }

            var effectiveTipOverlap = cmd.TipOverlapMm > 0 ? cmd.TipOverlapMm : tool.TipOverlapMm;
            var reK = Math.Max(0, cmd.RemachineLowerLayers);

            // Handler uses 1-based layer numbers; Cura uses 0-based
            foreach (var layer in layersToMachine)
            {
                var curaLayerIdx = layer - 1;   // convert to Cura 0-based index
                // Top of the printed part at this layer — from the ACTUAL Cura G-code Z.
                var partTopZ = layerActualZ.GetValueOrDefault(layer, layer * h);

                // Band bottom: previous pass top, extended down by re-machine overlap (K layers).
                //
                // With reK=3, machine-every=5, skip=10:
                //   Layer 15 (1st event): band = skip boundary (layer 10) to 15
                //   Layer 20 (2nd event): band = (15 - 3 = layer 12) to 20
                //   Layer 25 (3rd event): band = (20 - 3 = layer 17) to 25
                //
                // The previous-pass top layer is computed by walking back from the
                // current layer to find which 1-based layer corresponds to previousTopZ.
                var bandBottomZ = previousTopZ;

                // Compute the 1-based layer number where the previous pass ended.
                var prevPassTopLayer = skipBoundaryLayer; // default: skip boundary
                if (previousTopZ > 0)
                {
                    // Find the layer whose Z is closest to previousTopZ
                    for (var li = layer - 1; li >= 1; li--)
                    {
                        var lz = layerActualZ.GetValueOrDefault(li, li * h);
                        if (Math.Abs(lz - previousTopZ) < h * 0.5)
                        {
                            prevPassTopLayer = li;
                            break;
                        }
                    }
                }

                // Re-machine overlap: extend the band bottom reK layers below the
                // previous pass's top layer, but never into the skip zone.
                var bandBottomLayer = prevPassTopLayer; // without re-machine: start where previous ended
                if (reK > 0 && prevPassTopLayer > 0)
                {
                    bandBottomLayer = prevPassTopLayer - reK;
                    // Clamp: never go below skip boundary or below layer 1.
                    // The skip boundary IS the bottom of the first band — those layers
                    // were printed but never machined, so their surface must be cut.
                    var minLayer = skipBoundaryLayer > 0 ? skipBoundaryLayer : 1;
                    bandBottomLayer = Math.Max(bandBottomLayer, minLayer);
                }
                // On the very first machining event, bandBottomLayer is the skip boundary
                // (or layer 1 if no skip). The band starts right after the skip zone.
                if (bandBottomLayer < 1) bandBottomLayer = 1;

                bandBottomZ = layerActualZ.GetValueOrDefault(bandBottomLayer, Math.Max(bandBottomLayer * h, 0));

                // Tool tip goes to the bottom of the band.
                var tipZ = Math.Max(bandBottomZ - effectiveTipOverlap, cmd.BedClearanceMm);
                var effectiveZ = tipZ + cmd.ZSafetyOffsetMm;
                var layerBandMm = partTopZ - bandBottomZ;
                var engagedMm = partTopZ - tipZ;
                var zHeight = partTopZ;

                _logger.LogDebug("Processing layer {Layer} (Cura ;LAYER:{CI}) Z={Z:F3} effectiveZ={EZ:F3}",
                    layer, curaLayerIdx, zHeight, effectiveZ);

                // ── Guard: sanity check that Z is rising ─────────────────────────────
                if (layerBandMm <= 0 && previousTopZ > 0)
                {
                    warnings.Add($"Layer {layer}: band height is {layerBandMm:F2} mm (top {partTopZ:F2}, bottom {bandBottomZ:F2}). " +
                        "Band must be positive. Skipping this pass.");
                    gcodeBuilder.AppendLine($"; Layer {layer} — SKIPPED: band height {layerBandMm:F2} mm <= 0 (top {partTopZ:F2}, bottom {bandBottomZ:F2})");
                    previousTopZ = partTopZ;
                    continue;
                }

                // ── Spindle clearance pre-check ────────────────────────────────────────
                // When tool tip is at effectiveZ, the spindle collet is at effectiveZ + toolLengthMm.
                // That must stay within the machine Z travel. If not, the spindle body would
                // crash into the machine frame or gantry — skip and log as SpindleCollision.
                if (tool.ToolLengthMm > 0 && effectiveZ + tool.ToolLengthMm > machine.BedHeightMm)
                {
                    var spindleZ = effectiveZ + tool.ToolLengthMm;
                    _logger.LogWarning(
                        "Layer {L}: SpindleCollision — spindle at {SZ:F3} mm (tip {Z:F3} + tool length {TL:F3}) " +
                        "exceeds machine Z limit {MZ:F3} mm — layer skipped",
                        layer, spindleZ, effectiveZ, tool.ToolLengthMm, machine.BedHeightMm);
                    allUnmachinableRegions.Add(new UnmachinableRegion(effectiveZ, "SpindleCollision",
                        new BoundingBox2D(0, 0, 0, 0)));
                    gcodeBuilder.AppendLine(
                        $"; Layer {layer} Z={effectiveZ:F3} mm — SPINDLE COLLISION " +
                        $"(spindle at {spindleZ:F3} mm > machine Z {machine.BedHeightMm} mm) — skipped");
                    gcodeBuilder.AppendLine();
                    previousTopZ = partTopZ; // advance baseline even when skipped
                    continue;
                }

                // ── Tool definition sanity check ──────────────────────────────────────
                if (tool.ToolLengthMm > 0 && tool.FluteLengthMm > tool.ToolLengthMm)
                    _logger.LogWarning(
                        "Tool {Name}: flute length {FL:F2} mm exceeds tool length {TL:F2} mm — invalid tool definition",
                        tool.Name, tool.FluteLengthMm, tool.ToolLengthMm);

                // Look up parsed layer data (try exact match, then nearest below)
                if (!parsed.Layers.TryGetValue(curaLayerIdx, out var layerData))
                {
                    gcodeBuilder.AppendLine($"; Layer {layer} (Z={zHeight:F3} mm) — no Cura data, skipped");
                    gcodeBuilder.AppendLine();
                    _logger.LogDebug("No parsed data for Cura layer {CI}", curaLayerIdx);
                    previousTopZ = partTopZ; // advance baseline even when skipped
                    continue;
                }

                // Support avoidance: note detected supports; they will be passed as forbidden
                // zones to the planner, which clips toolpaths around them (no layer skip).
                var supportPaths = (cmd.AvoidSupports && layerData.SupportPaths.Count > 0)
                    ? layerData.SupportPaths
                    : null;

                if (supportPaths is not null)
                    _logger.LogInformation(
                        "Layer {L}: {S} support segment(s) detected — will clip toolpaths with {C} mm clearance",
                        layer, supportPaths.Count, cmd.SupportClearanceMm);

                // Split OuterWallPaths by nesting depth using the even-odd
                // rule. Cura puts BOTH the part exterior AND every hole/pocket
                // perimeter into WALL-OUTER. Winding alone isn't a reliable
                // signal — Cura's CCW/CW choice depends on version and the
                // "Wall Ordering" setting, so part exteriors can be either.
                // Containment is robust: a path whose first vertex falls
                // inside another OuterWall path is geometrically nested one
                // level deeper. Depth 0 = exterior (always machine);
                // depth 1 = hole (only machine when MachineInnerWalls);
                // depth 2 = island in a hole (exterior of a sub-part); etc.
                var exteriorPaths = new List<IReadOnlyList<(double X, double Y)>>();
                var holeOuterPaths = new List<IReadOnlyList<(double X, double Y)>>();
                foreach (var p in layerData.OuterWallPaths)
                {
                    if (p.Count < 3) { exteriorPaths.Add(p); continue; }
                    var tx = p[0].X; var ty = p[0].Y;
                    var depth = 0;
                    foreach (var q in layerData.OuterWallPaths)
                    {
                        if (ReferenceEquals(q, p)) continue;
                        if (PointInPolygon(q, tx, ty)) depth++;
                    }
                    if ((depth & 1) == 0) exteriorPaths.Add(p);
                    else                  holeOuterPaths.Add(p);
                }

                var willMachineInner = cmd.MachineInnerWalls;
                var totalWallCount = exteriorPaths.Count
                                   + (willMachineInner ? holeOuterPaths.Count + layerData.InnerWallPaths.Count : 0);

                if (totalWallCount == 0)
                {
                    gcodeBuilder.AppendLine($"; Layer {layer} (Z={zHeight:F3} mm) — no wall paths found, skipped");
                    gcodeBuilder.AppendLine();
                    _logger.LogDebug("Layer {L}: no wall paths", layer);
                    previousTopZ = partTopZ; // advance baseline even when skipped
                    continue;
                }

                // The tool does ONE contour pass per machining event. The band bottom
                // (including re-machine overlap) determines the tip Z so the flutes
                // cover the full band height in a single cut. No per-layer passes needed.
                var passIdx = machinedLayers.Count; // 0-based pass index for start-point rotation
                var outerRequest = new WallPathsRequest(
                    WallPaths:              exteriorPaths,
                    ZHeightMm:              effectiveZ,
                    ToolDiameterMm:         tool.DiameterMm,
                    NozzleDiameterMm:       profile.LineWidthMm,
                    FeedRateMmPerMin:       tool.RecommendedFeedMmPerMin,
                    SpindleRpm:             spindleRpm,
                    MachineOffset:          cncOffset,
                    SafeClearanceHeightMm:  machine.SafeClearanceHeightMm,
                    IsOuterWall:            true,
                    ClimbMilling:           true,
                    PartTopZMm:             partTopZ + cncOffset.Z,
                    SupportPaths:           supportPaths,
                    SupportClearanceMm:     cmd.SupportClearanceMm,
                    PassIndex:              passIdx);

                var toolpath = exteriorPaths.Count > 0
                    ? await _planner.PlanFromWallPathsAsync(outerRequest, ct)
                    : new ToolpathResult(string.Empty, true, [], []);

                // Collect unmachinable regions from the outer toolpath
                if (toolpath.UnmachinableRegions is { Count: > 0 })
                    allUnmachinableRegions.AddRange(toolpath.UnmachinableRegions);

                // Check FluteTooShort for this layer: axial depth must not exceed flute length
                if (tool.FluteLengthMm > 0 && bandMm > tool.FluteLengthMm)
                {
                    var env = new BoundingBox2D(0, 0, 0, 0);
                    allUnmachinableRegions.Add(new UnmachinableRegion(effectiveZ, "FluteTooShort", env));
                }

                // Optionally machine the "inner" surfaces: holes from WALL-OUTER
                // (CW rings) plus Cura's WALL-INNER (additional perimeter lines).
                ToolpathResult? innerToolpath = null;
                if (willMachineInner && (holeOuterPaths.Count > 0 || layerData.InnerWallPaths.Count > 0))
                {
                    var innerPaths = new List<IReadOnlyList<(double X, double Y)>>();
                    innerPaths.AddRange(holeOuterPaths);
                    innerPaths.AddRange(layerData.InnerWallPaths);

                    var innerRequest = outerRequest with
                    {
                        WallPaths   = innerPaths,
                        IsOuterWall = false,
                    };
                    innerToolpath = await _planner.PlanFromWallPathsAsync(innerRequest, ct);

                    // Collect unmachinable regions from inner toolpath
                    if (innerToolpath.UnmachinableRegions is { Count: > 0 })
                        allUnmachinableRegions.AddRange(innerToolpath.UnmachinableRegions);
                }

                if (toolpath.IsEmpty && (innerToolpath is null || innerToolpath.IsEmpty))
                {
                    gcodeBuilder.AppendLine($"; Layer {layer} (Z={zHeight:F3} mm) — planner returned empty, skipped");
                    gcodeBuilder.AppendLine();
                    _logger.LogDebug("Layer {L}: planner returned empty toolpath", layer);
                    previousTopZ = partTopZ; // advance baseline even when skipped
                    continue;
                }

                var combinedGCode = toolpath.GCode
                    + (innerToolpath is { IsEmpty: false } ? "\n" + innerToolpath.GCode : string.Empty);

                var allBounds = toolpath.ToolpathBounds
                    .Concat(innerToolpath?.ToolpathBounds ?? [])
                    .ToList();

                // ── Build printed geometry bounds for safety validation ─────────────────
                // The CNC tool must not enter solid printed material. For each outer wall path
                // at this layer, we compute a contracted 3D bounding box:
                //   • Contracted inward by (toolRadius + nozzleRadius + margin) from each side
                //   • This represents the "deep interior" of the part where the tool cannot
                //     legitimately be: being inside this contracted box = tool inside solid material
                //   • Only the deep interior is flagged to avoid false positives at the surface
                //     (the tool centre at the outer surface is CRC-offset from the wall, so
                //      it lies just OUTSIDE the wall polygon, not inside the contracted box)
                var printedBounds = new List<BoundingBox3D>();
                {
                    var crcContraction = tool.RadiusMm + profile.LineWidthMm / 2.0 + 0.5; // CRC offset + 0.5 mm margin
                    var offX = cncOffset.X;
                    var offY = cncOffset.Y;
                    foreach (var wallPath in layerData.OuterWallPaths)
                    {
                        if (wallPath.Count < 4) continue;
                        double pMinX = double.MaxValue, pMaxX = double.MinValue;
                        double pMinY = double.MaxValue, pMaxY = double.MinValue;
                        foreach (var (px, py) in wallPath)
                        {
                            if (px < pMinX) pMinX = px; if (px > pMaxX) pMaxX = px;
                            if (py < pMinY) pMinY = py; if (py > pMaxY) pMaxY = py;
                        }
                        var iMinX = pMinX + crcContraction;
                        var iMaxX = pMaxX - crcContraction;
                        var iMinY = pMinY + crcContraction;
                        var iMaxY = pMaxY - crcContraction;
                        if (iMinX >= iMaxX || iMinY >= iMaxY) continue; // wall too thin to have an interior box
                        printedBounds.Add(new BoundingBox3D(
                            iMinX + offX, iMinY + offY, 0,
                            iMaxX + offX, iMaxY + offY, effectiveZ + 0.01));
                    }
                }

                // Use full machine travel for envelope check, not just bed dimensions.
                // CNC coordinates have the CNC offset applied, so they extend beyond the bed area.
                // The valid range is the full travel envelope relative to the origin.
                var safetyReq = new SafetyValidationRequest(
                    CncGCode:              combinedGCode,
                    PrintedGeometryBounds: printedBounds,
                    MachineMaxX:           machine.TravelXMm,
                    MachineMaxY:           machine.TravelYMm,
                    MachineMaxZ:           machine.TravelZMm > 0 ? machine.TravelZMm : machine.BedHeightMm,
                    SafeClearanceHeightMm: machine.SafeClearanceHeightMm,
                    ToolRadiusMm:          tool.RadiusMm,
                    ToolLengthMm:          tool.ToolLengthMm);

                var validation = await _safety.ValidateToolpathAsync(safetyReq, ct);

                if (validation.Status == SafetyStatus.Blocked)
                    _logger.LogWarning("Safety BLOCKED (continuing) at layer {L}: {Issues}",
                        layer, string.Join("; ", validation.Issues));
                else if (validation.Status == SafetyStatus.Warning)
                    _logger.LogWarning("Safety WARNING at layer {L}: {Issues}",
                        layer, string.Join("; ", validation.Issues));

                // Per-pass safety report
                var fluteOk   = tool.FluteLengthMm <= 0 || engagedMm + tool.SafetyMarginMm <= tool.FluteLengthMm;
                var spindleOk = tool.ToolLengthMm <= 0 || engagedMm + tool.SafetyMarginMm < tool.ToolLengthMm;
                if (!fluteOk)
                    warnings.Add($"Layer {layer}: engaged {engagedMm:F1} mm exceeds flute length {tool.FluteLengthMm:F1} mm (with margin {tool.SafetyMarginMm:F1})");
                if (!spindleOk)
                    warnings.Add($"Layer {layer}: engaged {engagedMm:F1} mm exceeds tip-to-spindle {tool.ToolLengthMm:F1} mm — collision risk");

                gcodeBuilder.AppendLine($"; ── Pass @ layer {layer} (;LAYER:{curaLayerIdx}): layers {bandBottomLayer}–{layer}, Z {bandBottomZ:F2}–{partTopZ:F2}, tip Z{effectiveZ:F3}, engaged {engagedMm:F2} / flute {tool.FluteLengthMm:F1} {(fluteOk ? "OK" : "WARN")}, spindle clr {(tool.ToolLengthMm > 0 ? tool.ToolLengthMm - engagedMm : 999):F1} {(spindleOk ? "OK" : "WARN")} [{layerData.OuterWallPaths.Count} segs]{(reK > 0 ? $" re-machine:{reK}" : "")} ─");
                gcodeBuilder.AppendLine(combinedGCode.TrimEnd());
                gcodeBuilder.AppendLine();

                machinedLayers.Add(layer);
                previousTopZ = partTopZ; // advance baseline for next pass
                _logger.LogInformation(
                    "Toolpath OK — layer {L} Z={Z:F3} [{Status}]  {OW} outer + {IW} inner segments",
                    layer, zHeight, validation.Status,
                    layerData.OuterWallPaths.Count, layerData.InnerWallPaths.Count);
            }

            // Postamble: lift SafeClearanceHeightMm above the last machined layer, then stop spindle.
            var lastMachinedLayerZ = layersToMachine.Any()
                ? layersToMachine.Last() * profile.LayerHeightMm
                : 0.0;
            var postambleSafeZ = lastMachinedLayerZ + cncOffset.Z + machine.SafeClearanceHeightMm;
            gcodeBuilder.AppendLine();
            gcodeBuilder.AppendLine("; === Postamble: lift above part and stop spindle ===");
            gcodeBuilder.AppendLine($"G0 Z{postambleSafeZ.ToString("F3", inv)} F6000 ; {machine.SafeClearanceHeightMm}mm above last machined layer");
            gcodeBuilder.AppendLine("M5 ; spindle stop");

            // Write toolpath file
            var jobDir          = Path.GetDirectoryName(job.StlFilePath)!;
            var toolpathGCodePath = Path.Combine(jobDir, "toolpath.gcode");
            await File.WriteAllTextAsync(toolpathGCodePath, gcodeBuilder.ToString(), ct);

            // CNC toolpath is already in machine coordinates (wall paths were parsed from
            // the translated print G-code). No additional coordinate translation needed.
            // Only remap axis letters if configured.
            await _coordTranslator.RemapAxesAsync(toolpathGCodePath, machine.CncAxes, ct);

            job.MarkToolpathsComplete(toolpathGCodePath);
            await _jobs.UpdateAsync(job, ct);

            _logger.LogInformation(
                "Toolpath generation complete for {JobId}: {Count} layers machined, {UR} unmachinable regions",
                cmd.JobId, machinedLayers.Count, allUnmachinableRegions.Count);

            return new GenerateToolpathsResult(cmd.JobId, machinedLayers.Count, machinedLayers, allUnmachinableRegions, warnings);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Toolpath generation failed for job {JobId}", cmd.JobId);
            job.MarkFailed(ex.Message);
            await _jobs.UpdateAsync(job, ct);
            throw;
        }
    }

    /// <summary>
    /// Ray-casting point-in-polygon test. Used to determine whether one wall
    /// path is geometrically nested inside another (used for the even-odd
    /// classification of WALL-OUTER into exterior vs hole).
    /// </summary>
    private static bool PointInPolygon(IReadOnlyList<(double X, double Y)> poly, double px, double py)
    {
        var n = poly.Count;
        if (n < 3) return false;
        var inside = false;
        for (var i = 0; i < n; i++)
        {
            var (xi, yi) = poly[i];
            var (xj, yj) = poly[(i + n - 1) % n];
            if (((yi > py) != (yj > py))
                && (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            {
                inside = !inside;
            }
        }
        return inside;
    }
}
