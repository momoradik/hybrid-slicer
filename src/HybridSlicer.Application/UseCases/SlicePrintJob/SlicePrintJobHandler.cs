using System.Text;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.Exceptions;
using MediatR;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Application.UseCases.SlicePrintJob;

public sealed class SlicePrintJobHandler : IRequestHandler<SlicePrintJobCommand, SlicePrintJobResult>
{
    private readonly IPrintJobRepository _jobs;
    private readonly IPrintProfileRepository _printProfiles;
    private readonly IMachineProfileRepository _machines;
    private readonly IMaterialRepository _materials;
    private readonly ISlicingEngine _slicer;
    private readonly IMultiExtruderPostProcessor _multiExtruder;
    private readonly ICustomGCodeBlockRepository _customGCode;
    private readonly IMachineCoordinateTranslator _coordTranslator;
    private readonly ILogger<SlicePrintJobHandler> _logger;

    public SlicePrintJobHandler(
        IPrintJobRepository jobs,
        IPrintProfileRepository printProfiles,
        IMachineProfileRepository machines,
        IMaterialRepository materials,
        ISlicingEngine slicer,
        IMultiExtruderPostProcessor multiExtruder,
        ICustomGCodeBlockRepository customGCode,
        IMachineCoordinateTranslator coordTranslator,
        ILogger<SlicePrintJobHandler> logger)
    {
        _jobs = jobs;
        _printProfiles = printProfiles;
        _machines = machines;
        _materials = materials;
        _slicer = slicer;
        _multiExtruder = multiExtruder;
        _customGCode = customGCode;
        _coordTranslator = coordTranslator;
        _logger = logger;
    }

    public async Task<SlicePrintJobResult> Handle(SlicePrintJobCommand cmd, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(cmd.JobId, ct)
            ?? throw new DomainException("JOB_NOT_FOUND", $"Job {cmd.JobId} not found.");

        var profile = await _printProfiles.GetByIdAsync(job.PrintProfileId, ct)
            ?? throw new DomainException("PROFILE_NOT_FOUND", $"Print profile {job.PrintProfileId} not found.");

        var machine = await _machines.GetByIdAsync(job.MachineProfileId, ct)
            ?? throw new DomainException("MACHINE_NOT_FOUND", $"Machine profile {job.MachineProfileId} not found.");

        // Resolve material for temperature override
        var material = await _materials.GetByIdAsync(job.MaterialId, ct);

        // Resolve bed dimensions: use per-bed size if this is a multi-bed job
        double sliceBedWidth = machine.BedWidthMm, sliceBedDepth = machine.BedDepthMm, sliceBedHeight = machine.BedHeightMm;
        if (job.BedIndex.HasValue)
        {
            var beds = machine.Beds;
            var bi = job.BedIndex.Value;
            if (bi >= 0 && bi < beds.Count)
            {
                sliceBedWidth = beds[bi].WidthMm;
                sliceBedDepth = beds[bi].DepthMm;
                sliceBedHeight = beds[bi].HeightMm;
                _logger.LogInformation("Multi-bed job: using Bed {Bed} ({W}×{D}×{H} mm)",
                    bi + 1, sliceBedWidth, sliceBedDepth, sliceBedHeight);
            }
        }

        job.MarkSlicing();
        await _jobs.UpdateAsync(job, ct);

        _logger.LogInformation("Slicing job {JobId} with profile '{Profile}'", cmd.JobId, profile.Name);

        try
        {
            var parameters = new SlicingParameters(
                LayerHeightMm:         profile.LayerHeightMm,
                LineWidthMm:           profile.LineWidthMm,
                WallCount:             profile.WallCount,
                TopBottomLayers:       profile.TopBottomLayers,
                PrintSpeedMmS:         profile.PrintSpeedMmS,
                TravelSpeedMmS:        profile.TravelSpeedMmS,
                InfillSpeedMmS:        profile.InfillSpeedMmS,
                WallSpeedMmS:          profile.WallSpeedMmS,
                InnerWallSpeedMmS:     profile.InnerWallSpeedMmS,
                FirstLayerSpeedMmS:    profile.FirstLayerSpeedMmS,
                InfillDensityPct:      job.InfillDensityPct ?? profile.InfillDensityPct,
                InfillPattern:         string.IsNullOrWhiteSpace(job.InfillPattern) ? profile.InfillPattern : job.InfillPattern,
                // Use material temperature when available, fall back to profile defaults
                PrintTemperatureDegC:  material?.PrintTempMaxDegC > 0 ? material.PrintTempMaxDegC : profile.PrintTemperatureDegC,
                BedTemperatureDegC:    material?.BedTempMaxDegC > 0 ? material.BedTempMaxDegC : profile.BedTemperatureDegC,
                RetractLengthMm:       profile.RetractLengthMm,
                RetractSpeedMmS:       profile.RetractSpeedMmS,
                SupportEnabled:        job.SupportEnabled,
                SupportType:           job.SupportType,
                SupportPlacement:      job.SupportPlacement,
                SupportInfillDensityPct: job.SupportInfillDensityPct ?? 15,
                SupportInfillPattern:  string.IsNullOrWhiteSpace(job.SupportInfillPattern) ? "grid" : job.SupportInfillPattern,
                AdhesionType:          string.IsNullOrWhiteSpace(job.AdhesionType) ? "none" : job.AdhesionType,
                CoolingEnabled:        profile.CoolingEnabled,
                CoolingFanSpeedPct:    profile.CoolingFanSpeedPct,
                FilamentDiameterMm:    profile.PelletModeEnabled
                                           ? profile.VirtualFilamentDiameterMm
                                           : profile.FilamentDiameterMm,
                BedWidthMm:            sliceBedWidth,
                BedDepthMm:            sliceBedDepth,
                BedHeightMm:           sliceBedHeight,
                NozzleDiameterMm:      profile.NozzleDiameterMm > 0 ? profile.NozzleDiameterMm : 0.4,
                // Internal pipeline always uses bed-centre origin for STL viewer / preview consistency.
                // OriginMode in the machine profile is for documentation and future firmware output.
                OriginIsBedCenter:     true,
                MaterialFlowPct:       profile.MaterialFlowPct);

            var result = await _slicer.SliceAsync(job.StlFilePath, parameters, ct);

            // Multi-extruder post-processing: insert tool changes, apply nozzle offsets,
            // and inject per-extruder custom G-code blocks. No-op for single-extruder.
            var enabledBlocks = await _customGCode.GetEnabledAsync(ct);
            await _multiExtruder.ProcessAsync(result.GCodeFilePath, machine, enabledBlocks, ct);

            await ReplaceStartupGCodeAsync(result.GCodeFilePath, job.GcodeHoming, job.GcodeLevelling, parameters, ct);
            await InjectCustomGCodeAsync(result.GCodeFilePath, ct);

            // Final step: translate from bed-centre coordinates to real machine coordinates
            // using origin and bed position from the machine profile. No-op if origin = bed centre.
            await _coordTranslator.TranslateAsync(result.GCodeFilePath, machine, job.BedIndex, ct);
            await _coordTranslator.RemapAxesAsync(result.GCodeFilePath, machine.ExtruderAxes, ct);

            job.MarkSlicingComplete(result.GCodeFilePath, result.TotalLayers);
            await _jobs.UpdateAsync(job, ct);

            _logger.LogInformation("Slice complete for job {JobId}: {Layers} layers", cmd.JobId, result.TotalLayers);

            return new SlicePrintJobResult(
                cmd.JobId,
                result.GCodeFilePath,
                result.TotalLayers,
                result.EstimatedPrintTimeSec,
                result.EstimatedFilamentMm);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Slicing failed for job {JobId}", cmd.JobId);
            job.MarkFailed(ex.Message);
            await _jobs.UpdateAsync(job, ct);
            throw;
        }
    }

    /// <summary>
    /// Strips Cura's default startup G-code (everything before the first ;LAYER:0)
    /// and replaces it with user-selected startup commands (homing, levelling, temps).
    /// </summary>
    private async Task ReplaceStartupGCodeAsync(
        string gcodePath, bool homing, bool levelling, SlicingParameters p, CancellationToken ct)
    {
        var lines = await File.ReadAllLinesAsync(gcodePath, ct);

        // Find the first ;LAYER:0 marker — everything before it is Cura's preamble
        var layerStart = -1;
        for (var i = 0; i < lines.Length; i++)
        {
            if (lines[i].TrimStart().StartsWith(";LAYER:0", StringComparison.OrdinalIgnoreCase))
            { layerStart = i; break; }
        }

        if (layerStart < 0) return; // no layer marker found, leave file as-is

        // Preserve Cura metadata comments (;LAYER_COUNT, ;TIME, ;Filament, ;FLAVOR, etc.)
        var sb = new StringBuilder();
        foreach (var line in lines.Take(layerStart))
        {
            var t = line.TrimStart();
            if (t.StartsWith(";LAYER_COUNT:") || t.StartsWith(";TIME:") ||
                t.StartsWith(";Filament used:") || t.StartsWith(";FLAVOR:") ||
                t.StartsWith(";generated") || t.StartsWith(";MINX:") ||
                t.StartsWith(";MINY:") || t.StartsWith(";MINZ:") ||
                t.StartsWith(";MAXX:") || t.StartsWith(";MAXY:") ||
                t.StartsWith(";MAXZ:"))
                sb.AppendLine(line);
        }

        // Insert user-selected startup commands
        sb.AppendLine("; === HybridSlicer Startup ===");
        if (homing)    sb.AppendLine("G28          ; home all axes");
        if (levelling) sb.AppendLine("G29          ; bed levelling");
        sb.AppendLine($"M104 S{p.PrintTemperatureDegC} ; set extruder temp");
        sb.AppendLine($"M140 S{p.BedTemperatureDegC}   ; set bed temp");
        sb.AppendLine($"M109 S{p.PrintTemperatureDegC} ; wait for extruder");
        sb.AppendLine($"M190 S{p.BedTemperatureDegC}   ; wait for bed");
        sb.AppendLine("G92 E0       ; reset extruder");
        sb.AppendLine("; === End Startup ===");
        sb.AppendLine();

        // Append everything from ;LAYER:0 onward
        for (var i = layerStart; i < lines.Length; i++)
            sb.AppendLine(lines[i]);

        await File.WriteAllTextAsync(gcodePath, sb.ToString(), ct);
    }

    private async Task InjectCustomGCodeAsync(string gcodePath, CancellationToken ct)
    {
        var startBlocks = (await _customGCode.GetByTriggerAsync(GCodeTrigger.JobStart, ct))
            .Where(b => b.IsEnabled).OrderBy(b => b.SortOrder).ToList();
        var endBlocks = (await _customGCode.GetByTriggerAsync(GCodeTrigger.JobEnd, ct))
            .Where(b => b.IsEnabled).OrderBy(b => b.SortOrder).ToList();

        if (startBlocks.Count == 0 && endBlocks.Count == 0) return;

        var original = await File.ReadAllTextAsync(gcodePath, ct);
        var sb = new StringBuilder();

        if (startBlocks.Count > 0)
        {
            sb.AppendLine("; === Custom G-code: Job Start ===");
            foreach (var block in startBlocks)
                sb.AppendLine(block.GCodeContent);
            sb.AppendLine("; === End Custom G-code ===");
            sb.AppendLine();
        }

        sb.Append(original);

        if (endBlocks.Count > 0)
        {
            sb.AppendLine();
            sb.AppendLine("; === Custom G-code: Job End ===");
            foreach (var block in endBlocks)
                sb.AppendLine(block.GCodeContent);
            sb.AppendLine("; === End Custom G-code ===");
        }

        await File.WriteAllTextAsync(gcodePath, sb.ToString(), ct);
    }
}
