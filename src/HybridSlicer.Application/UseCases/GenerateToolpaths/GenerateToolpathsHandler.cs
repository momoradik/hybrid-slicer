using System.Globalization;
using System.Text;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Application.Interfaces.Repositories;
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
        var axialDepthMm = cmd.MachineEveryNLayers * profile.LayerHeightMm;
        if (axialDepthMm > tool.MaxDepthOfCutMm)
            _logger.LogWarning(
                "Axial depth {D:F3} mm exceeds tool MaxDepthOfCut {M:F3} mm " +
                "(machineEveryN={N} × layerHeight={H}). Proceeding with caution.",
                axialDepthMm, tool.MaxDepthOfCutMm,
                cmd.MachineEveryNLayers, profile.LayerHeightMm);

        // ── FluteTooShort global check ────────────────────────────────────────
        // If the user's axial depth already exceeds the flute length, flag it immediately.
        var globalFluteTooShort = tool.FluteLengthMm > 0
            && axialDepthMm > tool.FluteLengthMm
            && !cmd.AutoMachiningFrequency;

        // ── Parse Cura G-code for wall paths ─────────────────────────────────
        _logger.LogInformation("Parsing Cura G-code: {Path}", job.PrintGCodePath);
        var gcodeText = await File.ReadAllTextAsync(job.PrintGCodePath, ct);
        var parsed    = await Task.Run(() => _parser.Parse(gcodeText), ct);

        _logger.LogInformation(
            "Parsed {Count} layers from Cura G-code. WALL-OUTER found in {OW} layers.",
            parsed.Layers.Count,
            parsed.Layers.Values.Count(l => l.OuterWallPaths.Count > 0));

        // ── Mark job and prepare output ───────────────────────────────────────
        job.AssignCncTool(cmd.CncToolId);
        job.MarkGeneratingToolpaths();
        await _jobs.UpdateAsync(job, ct);

        var machinedLayers      = new List<int>();
        var allUnmachinableRegions = new List<UnmachinableRegion>();
        var gcodeBuilder        = new StringBuilder();
        gcodeBuilder.AppendLine($"; CNC Toolpath G-code — Job: {job.Name}");
        var spindleRpm = cmd.SpindleRpmOverride ?? tool.RecommendedRpm;
        gcodeBuilder.AppendLine($"; Tool     : {tool.Name}  Ø{tool.DiameterMm} mm  Flute: {tool.FluteLengthMm} mm  Tool length: {tool.ToolLengthMm} mm  Feed: {tool.RecommendedFeedMmPerMin} mm/min  RPM: {spindleRpm}{(cmd.SpindleRpmOverride.HasValue ? $" (override — tool default: {tool.RecommendedRpm})" : "")}");
        gcodeBuilder.AppendLine($"; Nozzle   : Ø{profile.LineWidthMm} mm  Layer height: {profile.LayerHeightMm} mm");
        gcodeBuilder.AppendLine($"; Interval : {(cmd.AutoMachiningFrequency ? "AUTO (flute-based)" : $"every {cmd.MachineEveryNLayers} part layer(s) (support-only layers excluded)")}  Axial depth: {axialDepthMm:F3} mm");
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

        gcodeBuilder.AppendLine($"; Spindle start: X={cmd.SpindleStartX.ToString("F3", inv)} Y={cmd.SpindleStartY.ToString("F3", inv)} Z={resolvedStartZ.ToString("F3", inv)}");
        gcodeBuilder.AppendLine($"; Spindle end  : X={resolvedEndX.ToString("F3", inv)} Y={resolvedEndY.ToString("F3", inv)} Z={resolvedEndZ.ToString("F3", inv)}");
        gcodeBuilder.AppendLine();
        gcodeBuilder.AppendLine("; === Preamble: move spindle to start position ===");
        gcodeBuilder.AppendLine($"G0 Z{machine.SafeClearanceHeightMm.ToString("F3", inv)}");
        gcodeBuilder.AppendLine($"G0 X{cmd.SpindleStartX.ToString("F3", inv)} Y{cmd.SpindleStartY.ToString("F3", inv)}");
        gcodeBuilder.AppendLine($"G0 Z{resolvedStartZ.ToString("F3", inv)}");
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
            var fluteLayerCount = tool.FluteLengthMm > 0 && profile.LayerHeightMm > 0
                ? (int)Math.Ceiling(tool.FluteLengthMm / profile.LayerHeightMm)
                : 0;

            var autoLayers    = new List<int>();
            var lastMachinedZ = 0.0;

            for (var layerIdx = 1; layerIdx <= job.TotalPrintLayers!.Value; layerIdx++)
            {
                var currentZ   = layerIdx * profile.LayerHeightMm;
                var effectiveZ = currentZ + cmd.ZSafetyOffsetMm;
                var pending    = currentZ - lastMachinedZ;
                var curaIdx    = layerIdx - 1;

                // (1) Flute reach: accumulated uncut height ≥ 80% of flute length
                var fluteTriggered = tool.FluteLengthMm > 0 && pending >= tool.FluteLengthMm * 0.8;

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
                        // Outward expansion on any side exceeds CRC → access will be blocked
                        var outward = Math.Max(
                            Math.Max(futBnd.MaxX - curBnd.MaxX, curBnd.MinX - futBnd.MinX),
                            Math.Max(futBnd.MaxY - curBnd.MaxY, curBnd.MinY - futBnd.MinY));
                        if (outward > crcOffset) accessBlocked = true;
                    }
                }

                // (3) Spindle collision risk: spindle body within 5% of machine Z travel limit
                var spindleTriggered = tool.ToolLengthMm > 0 &&
                    effectiveZ + tool.ToolLengthMm > machine.BedHeightMm * 0.95;

                // Only schedule machining on layers that actually have part geometry.
                // Support-only / draft-shield-only layers have nothing to machine; scheduling
                // on them would waste a machining event and confuse the process plan.
                var hasPartGeometryAuto = layerBounds.TryGetValue(curaIdx, out var autoLayBnd) && autoLayBnd.Area > 0;

                if ((fluteTriggered || accessBlocked || spindleTriggered)
                    && pending >= profile.LayerHeightMm
                    && hasPartGeometryAuto) // must be a real part layer, not support-only
                {
                    autoLayers.Add(layerIdx);
                    lastMachinedZ = currentZ;
                    _logger.LogDebug(
                        "AUTO layer {L}: flute={FT} access={AT} spindle={ST}  pending={P:F2} mm  crcOffset={CRC:F3}",
                        layerIdx, fluteTriggered, accessBlocked, spindleTriggered, pending, crcOffset);
                }
            }
            layersToMachine = autoLayers;
            gcodeBuilder.AppendLine($"; AUTO machining: flute={tool.FluteLengthMm} mm ({fluteLayerCount} layers)  look-ahead-access-blocking=CRC({crcOffset:F2}mm)  spindle-limit=95%");
            gcodeBuilder.AppendLine($"; AUTO machining: {autoLayers.Count} layers selected (irregular — geometry-driven)");
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
            layersToMachine = manualLayers;
            _logger.LogInformation(
                "Manual machining schedule (every {N} part layers): {Sched} events from {Part} part layers ({Total} total)",
                cmd.MachineEveryNLayers, manualLayers.Count, partLayerCount, job.TotalPrintLayers!.Value);
            gcodeBuilder.AppendLine($"; MANUAL machining: every {cmd.MachineEveryNLayers} part layer(s)  " +
                                    $"({partLayerCount} part layers / {job.TotalPrintLayers!.Value} total → {manualLayers.Count} event(s))");
            gcodeBuilder.AppendLine();
        }

        try
        {
            // Handler uses 1-based layer numbers; Cura uses 0-based
            foreach (var layer in layersToMachine)
            {
                var curaLayerIdx = layer - 1;   // convert to Cura 0-based index
                var zHeight      = layer * profile.LayerHeightMm;
                // Apply Z safety offset: raise every machining pass by the configured amount.
                // This adds a consistent safety distance in Z above the nominal layer surface.
                var effectiveZ = zHeight + cmd.ZSafetyOffsetMm;

                _logger.LogDebug("Processing layer {Layer} (Cura ;LAYER:{CI}) Z={Z:F3} effectiveZ={EZ:F3}",
                    layer, curaLayerIdx, zHeight, effectiveZ);

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
                    continue;
                }

                // Generate CNC toolpath from EXTERIOR walls (always).
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
                    SupportPaths:           supportPaths,
                    SupportClearanceMm:     cmd.SupportClearanceMm);

                var toolpath = exteriorPaths.Count > 0
                    ? await _planner.PlanFromWallPathsAsync(outerRequest, ct)
                    : new ToolpathResult(string.Empty, true, [], []);

                // Collect unmachinable regions from the outer toolpath
                if (toolpath.UnmachinableRegions is { Count: > 0 })
                    allUnmachinableRegions.AddRange(toolpath.UnmachinableRegions);

                // Check FluteTooShort for this layer: axial depth must not exceed flute length
                if (tool.FluteLengthMm > 0 && axialDepthMm > tool.FluteLengthMm)
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
                    continue;
                }

                // Safety validation
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

                gcodeBuilder.AppendLine($"; ── Layer {layer} (nominal Z={zHeight:F3} mm  effective Z={effectiveZ:F3} mm) [{layerData.OuterWallPaths.Count} outer wall segments] ─");
                gcodeBuilder.AppendLine(combinedGCode.TrimEnd());
                gcodeBuilder.AppendLine();

                machinedLayers.Add(layer);
                _logger.LogInformation(
                    "Toolpath OK — layer {L} Z={Z:F3} [{Status}]  {OW} outer + {IW} inner segments",
                    layer, zHeight, validation.Status,
                    layerData.OuterWallPaths.Count, layerData.InnerWallPaths.Count);
            }

            // Postamble: return spindle to end position
            gcodeBuilder.AppendLine();
            gcodeBuilder.AppendLine("; === Postamble: return spindle to end position ===");
            gcodeBuilder.AppendLine($"G0 Z{machine.SafeClearanceHeightMm.ToString("F3", inv)}");
            gcodeBuilder.AppendLine($"G0 X{resolvedEndX.ToString("F3", inv)} Y{resolvedEndY.ToString("F3", inv)}");
            gcodeBuilder.AppendLine($"G0 Z{resolvedEndZ.ToString("F3", inv)}");
            gcodeBuilder.AppendLine("M5 ; spindle stop — job complete");

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

            return new GenerateToolpathsResult(cmd.JobId, machinedLayers.Count, machinedLayers, allUnmachinableRegions);
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
