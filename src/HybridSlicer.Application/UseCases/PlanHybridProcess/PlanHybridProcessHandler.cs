using System.Text;
using System.Text.RegularExpressions;
using HybridSlicer.Application.Common;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.Exceptions;
using MediatR;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HybridSlicer.Application.UseCases.PlanHybridProcess;

public sealed class PlanHybridProcessHandler : IRequestHandler<PlanHybridProcessCommand, PlanHybridProcessResult>
{
    private readonly IPrintJobRepository _jobs;
    private readonly IHybridOrchestrator _orchestrator;
    private readonly ICustomGCodeBlockRepository _blocks;
    private readonly StorageOptions _storage;
    private readonly ILogger<PlanHybridProcessHandler> _logger;

    public PlanHybridProcessHandler(
        IPrintJobRepository jobs,
        IHybridOrchestrator orchestrator,
        ICustomGCodeBlockRepository blocks,
        IOptions<StorageOptions> storageOpts,
        ILogger<PlanHybridProcessHandler> logger)
    {
        _jobs = jobs;
        _orchestrator = orchestrator;
        _blocks = blocks;
        _storage = storageOpts.Value;
        _logger = logger;
    }

    public async Task<PlanHybridProcessResult> Handle(PlanHybridProcessCommand cmd, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(cmd.JobId, ct)
            ?? throw new DomainException("JOB_NOT_FOUND", $"Job {cmd.JobId} not found.");

        if (job.Status != JobStatus.ToolpathsComplete && job.Status != JobStatus.Ready)
            throw new DomainException("INVALID_STATE",
                $"Job must be in ToolpathsComplete or Ready state (current: {job.Status}).");

        if (job.PrintGCodePath is null || job.TotalPrintLayers is null)
            throw new DomainException("MISSING_DATA", "Job has no sliced G-code. Run slice first.");

        if (job.CncToolId is null)
            throw new DomainException("MISSING_TOOL", "No CNC tool assigned to job. Run generate-toolpaths first.");

        if (job.ToolpathGCodePath is null || !File.Exists(job.ToolpathGCodePath))
            throw new DomainException("NO_TOOLPATH",
                "Toolpath G-code not found. Run generate-toolpaths first.");

        // Blocks are per-machine: this job's machine plus the shared ones. The job
        // can opt out entirely (checkbox on STL import).
        var enabledBlocks = job.ApplyCustomGCodeBlocks
            ? await _blocks.GetForMachineAsync(job.MachineProfileId, enabledOnly: true, ct)
            : [];

        _logger.LogInformation(
            "Job {JobId}: {Count} custom G-code block(s) apply (machine {MachineId}, opted-in={OptedIn})",
            job.Id, enabledBlocks.Count, job.MachineProfileId, job.ApplyCustomGCodeBlocks);

        job.MarkPlanningHybrid();
        await _jobs.UpdateAsync(job, ct);

        _logger.LogInformation("Building hybrid plan for job {JobId}", cmd.JobId);

        try
        {
            // Parse the already-generated toolpath.gcode to extract per-layer blocks.
            // This ensures the hybrid G-code uses the exact same machining passes
            // (including auto-machining frequency, support avoidance, spindle positions, etc.)
            // that were validated during generate-toolpaths — not a simplified re-generation.
            var toolpathText = await File.ReadAllTextAsync(job.ToolpathGCodePath, ct);
            var (cncByLayer, preamble, postamble) = ParseToolpathGCode(toolpathText);

            if (cncByLayer.Count == 0)
                throw new DomainException("EMPTY_TOOLPATH",
                    "Toolpath G-code contains no layer blocks. Re-run generate-toolpaths.");

            _logger.LogInformation(
                "Parsed toolpath G-code: {Count} machined layers from {Path}",
                cncByLayer.Count, job.ToolpathGCodePath);

            var outputPath = Path.Combine(
                _storage.Root, "jobs", job.Id.ToString(), "hybrid.gcode");

            var planResult = await _orchestrator.BuildPlanAsync(new HybridPlanRequest(
                JobId:               job.Id,
                PrintGCodePath:      job.PrintGCodePath,
                CncGCodeByLayer:     cncByLayer,
                CncPreamble:         preamble,
                CncPostamble:        postamble,
                MachineEveryNLayers: cmd.MachineEveryNLayers,
                TotalPrintLayers:    job.TotalPrintLayers.Value,
                CncToolId:           job.CncToolId.Value,
                EnabledCustomBlocks: enabledBlocks,
                OutputGCodePath:     outputPath), ct);

            job.MarkReady(planResult.HybridGCodePath);
            await _jobs.UpdateAsync(job, ct);

            _logger.LogInformation("Hybrid plan complete: {Steps} steps → {Path}",
                planResult.Plan.Steps.Count, planResult.HybridGCodePath);

            return new PlanHybridProcessResult(
                cmd.JobId,
                planResult.Plan.Id,
                planResult.HybridGCodePath,
                planResult.Plan.Steps.Count);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Hybrid planning failed for job {JobId}", cmd.JobId);
            job.MarkFailed(ex.Message);
            await _jobs.UpdateAsync(job, ct);
            throw;
        }
    }

    /// <summary>
    /// Splits toolpath.gcode into preamble, per-layer blocks, and postamble.
    /// Layer blocks are keyed by the 1-based layer number parsed from
    /// <c>; ── Layer N (nominal Z=…)</c> comment markers written by GenerateToolpathsHandler.
    /// </summary>
    private static (Dictionary<int, string> byLayer, string preamble, string postamble)
        ParseToolpathGCode(string gcode)
    {
        var byLayer   = new Dictionary<int, string>();
        var preamble  = new StringBuilder();
        var postamble = new StringBuilder();
        var current   = new StringBuilder();
        int? currentLayer = null;
        bool inPostamble  = false;

        foreach (var rawLine in gcode.Split('\n'))
        {
            var line = rawLine.TrimEnd('\r');

            // Detect postamble (must come before layer-marker check)
            if (!inPostamble && line.Contains("=== Postamble"))
            {
                // Store the last layer block that was being accumulated
                if (currentLayer.HasValue)
                    byLayer[currentLayer.Value] = current.ToString();
                else
                    preamble.Append(current.ToString());

                current.Clear();
                inPostamble = true;
                postamble.AppendLine(line);
                continue;
            }

            if (inPostamble)
            {
                postamble.AppendLine(line);
                continue;
            }

            // Detect layer marker: "; ── Layer 5 (nominal Z=…)"
            // The handler emits: $"; ── Layer {layer} (nominal Z={zHeight:F3} mm …)"
            var m = Regex.Match(line, @"^;.*Layer\s+(\d+)", RegexOptions.IgnoreCase);
            if (m.Success)
            {
                // Store the previous block
                if (currentLayer.HasValue)
                    byLayer[currentLayer.Value] = current.ToString();
                else
                    preamble.Append(current.ToString());

                currentLayer = int.Parse(m.Groups[1].Value);
                current.Clear();
                current.AppendLine(line);
                continue;
            }

            current.AppendLine(line);
        }

        // EOF: flush whatever remains
        if (!inPostamble)
        {
            if (currentLayer.HasValue)
                byLayer[currentLayer.Value] = current.ToString();
            else
                preamble.Append(current.ToString());
        }

        return (byLayer, preamble.ToString(), postamble.ToString());
    }
}
