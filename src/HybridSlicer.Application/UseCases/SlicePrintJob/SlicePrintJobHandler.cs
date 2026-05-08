using HybridSlicer.Application.Common;
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
    private readonly ISlicingEngine _slicer;
    private readonly IMultiExtruderPostProcessor _multiExtruder;
    private readonly ICustomGCodeBlockRepository _customGCode;
    private readonly IMachineCoordinateTranslator _coordTranslator;
    private readonly ILogger<SlicePrintJobHandler> _logger;

    public SlicePrintJobHandler(
        IPrintJobRepository jobs,
        IPrintProfileRepository printProfiles,
        IMachineProfileRepository machines,
        ISlicingEngine slicer,
        IMultiExtruderPostProcessor multiExtruder,
        ICustomGCodeBlockRepository customGCode,
        IMachineCoordinateTranslator coordTranslator,
        ILogger<SlicePrintJobHandler> logger)
    {
        _jobs = jobs;
        _printProfiles = printProfiles;
        _machines = machines;
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
                PrintTemperatureDegC:  profile.PrintTemperatureDegC,
                BedTemperatureDegC:    profile.BedTemperatureDegC,
                RetractLengthMm:       profile.RetractLengthMm,
                RetractSpeedMmS:       profile.RetractSpeedMmS,
                SupportEnabled:        job.SupportEnabled,
                SupportType:           job.SupportType,
                SupportPlacement:      job.SupportPlacement,
                SupportInfillDensityPct: job.SupportInfillDensityPct ?? 15,
                SupportInfillPattern:  string.IsNullOrWhiteSpace(job.SupportInfillPattern) ? "grid" : job.SupportInfillPattern,
                CoolingEnabled:        profile.CoolingEnabled,
                CoolingFanSpeedPct:    profile.CoolingFanSpeedPct,
                FilamentDiameterMm:    profile.PelletModeEnabled
                                           ? profile.VirtualFilamentDiameterMm
                                           : profile.FilamentDiameterMm,
                BedWidthMm:            machine.BedWidthMm,
                BedDepthMm:            machine.BedDepthMm,
                BedHeightMm:           machine.BedHeightMm,
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

            await ApplyWrapperBlocksAsync(result.GCodeFilePath, enabledBlocks, ct);

            // Final step: translate from bed-centre coordinates to real machine coordinates
            // using origin and bed position from the machine profile. No-op if origin = bed centre.
            await _coordTranslator.TranslateAsync(result.GCodeFilePath, machine, ct);

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
    /// Wraps the sliced G-code with the four bookend custom triggers
    /// (JobStart, BeforePrinting, AfterPrinting, JobEnd) using HSCB markers so the
    /// hybrid orchestrator can later strip and re-apply them on the merged file.
    /// </summary>
    private async Task ApplyWrapperBlocksAsync(
        string gcodePath,
        IReadOnlyList<Domain.Entities.CustomGCodeBlock> enabledBlocks,
        CancellationToken ct)
    {
        var hasAny = enabledBlocks.Any(b => b.IsEnabled && b.Trigger
            is GCodeTrigger.JobStart
            or GCodeTrigger.BeforePrinting
            or GCodeTrigger.AfterPrinting
            or GCodeTrigger.JobEnd);
        if (!hasAny) return;

        var original = await File.ReadAllTextAsync(gcodePath, ct);
        var wrapped  = CustomGCodeBlockApplier.ApplyWrappers(original, enabledBlocks);
        await File.WriteAllTextAsync(gcodePath, wrapped, ct);
    }
}
