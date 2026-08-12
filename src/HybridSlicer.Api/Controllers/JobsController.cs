using HybridSlicer.Application.Common;
using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Application.UseCases.GenerateToolpaths;
using HybridSlicer.Application.UseCases.ImportStl;
using HybridSlicer.Application.UseCases.PlanHybridProcess;
using HybridSlicer.Application.UseCases.SlicePrintJob;
using HybridSlicer.Infrastructure.Slicing;
using MediatR;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Options;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/jobs")]
public sealed class JobsController : ControllerBase
{
    private readonly IMediator _mediator;
    private readonly IPrintJobRepository _jobs;
    private readonly ICustomGCodeBlockRepository _customGCode;
    private readonly MultiBedMerger _merger;
    private readonly StorageOptions _storage;

    public JobsController(
        IMediator mediator,
        IPrintJobRepository jobs,
        ICustomGCodeBlockRepository customGCode,
        MultiBedMerger merger,
        IOptions<StorageOptions> storage)
    {
        _mediator = mediator;
        _jobs = jobs;
        _customGCode = customGCode;
        _merger = merger;
        _storage = storage.Value;
    }

    [HttpGet]
    public async Task<IActionResult> GetAll(CancellationToken ct)
        => Ok(await _jobs.GetAllAsync(ct));

    [HttpGet("{id:guid}")]
    public async Task<IActionResult> GetById(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        return job is null ? NotFound() : Ok(job);
    }

    [HttpPost("upload-stl")]
    [RequestSizeLimit(256 * 1024 * 1024)]
    public async Task<IActionResult> UploadStl(
        [FromForm] UploadStlRequest request,
        CancellationToken ct)
    {
        if (request.File.Length == 0) return BadRequest("File is empty.");

        await using var stream = request.File.OpenReadStream();
        var result = await _mediator.Send(new ImportStlCommand(
            JobName: request.JobName,
            StlStream: stream,
            OriginalFileName: request.File.FileName,
            MachineProfileId: request.MachineProfileId,
            PrintProfileId: request.PrintProfileId,
            MaterialId: request.MaterialId,
            SupportEnabled: request.SupportEnabled,
            SupportType: request.SupportType,
            SupportPlacement: request.SupportPlacement,
            InfillPattern: request.InfillPattern,
            InfillDensityPct: request.InfillDensityPct,
            SupportInfillPattern: request.SupportInfillPattern,
            SupportInfillDensityPct: request.SupportInfillDensityPct,
            AdhesionType: request.AdhesionType,
            GcodeHoming: request.GcodeHoming,
            GcodeLevelling: request.GcodeLevelling,
            BedIndex: request.BedIndex,
            ParentJobId: request.ParentJobId,
            ApplyCustomGCodeBlocks: request.ApplyCustomGCodeBlocks), ct);

        return CreatedAtAction(nameof(GetById), new { id = result.JobId }, result);
    }

    [HttpPost]
    public IActionResult Create([FromBody] CreateJobRequest request, CancellationToken ct)
    {
        // Convenience endpoint: creates a job record before STL upload
        // In full implementation this creates a Draft job and returns ID for upload
        return Accepted(new { message = "Use upload-stl endpoint with multipart form data." });
    }

    [HttpPost("{id:guid}/slice")]
    public async Task<IActionResult> Slice(Guid id, CancellationToken ct)
    {
        var result = await _mediator.Send(new SlicePrintJobCommand(id), ct);
        return Accepted(result);
    }

    [HttpPost("{id:guid}/generate-toolpaths")]
    public async Task<IActionResult> GenerateToolpaths(
        Guid id,
        [FromBody] GenerateToolpathsRequest request,
        CancellationToken ct)
    {
        var result = await _mediator.Send(
            new GenerateToolpathsCommand(
                id,
                request.CncToolId,
                request.MachineEveryNLayers,
                request.MachineInnerWalls,
                request.AvoidSupports,
                request.SupportClearanceMm,
                request.AutoMachiningFrequency,
                request.ZSafetyOffsetMm,
                request.SpindleRpmOverride,
                request.SpindleStartX,
                request.SpindleStartY,
                request.SpindleStartZ,
                request.SpindleEndX,
                request.SpindleEndY,
                request.SpindleEndZ,
                request.SkipMachiningLayers), ct);
        return Accepted(result);
    }

    [HttpPost("{id:guid}/plan-hybrid")]
    public async Task<IActionResult> PlanHybrid(
        Guid id,
        [FromBody] PlanHybridRequest request,
        CancellationToken ct)
    {
        var result = await _mediator.Send(
            new PlanHybridProcessCommand(id, request.MachineEveryNLayers), ct);
        return Accepted(result);
    }

    [HttpGet("{id:guid}/toolpath-gcode")]
    public async Task<IActionResult> GetToolpathGCode(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        if (job is null) return NotFound();
        if (job.ToolpathGCodePath is null) return BadRequest("Toolpath G-code not yet generated.");
        if (!System.IO.File.Exists(job.ToolpathGCodePath)) return NotFound("Toolpath G-code file not found on disk.");
        var stream = System.IO.File.OpenRead(job.ToolpathGCodePath);
        return File(stream, "text/plain");
    }

    [HttpGet("{id:guid}/print-gcode")]
    public async Task<IActionResult> GetPrintGCode(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        if (job is null) return NotFound();
        if (job.PrintGCodePath is null) return BadRequest("Print G-code not yet generated.");
        if (!System.IO.File.Exists(job.PrintGCodePath)) return NotFound("G-code file not found on disk.");
        var stream = System.IO.File.OpenRead(job.PrintGCodePath);
        return File(stream, "text/plain");
    }

    [HttpGet("{id:guid}/print-gcode/download")]
    public async Task<IActionResult> DownloadPrintGCode(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        if (job is null) return NotFound();
        if (job.PrintGCodePath is null) return BadRequest("Print G-code not yet generated.");
        if (!System.IO.File.Exists(job.PrintGCodePath)) return NotFound("G-code file not found on disk.");
        var stream = System.IO.File.OpenRead(job.PrintGCodePath);
        return File(stream, "application/octet-stream", $"{job.Name}_extrusion.gcode");
    }

    [HttpGet("{id:guid}/gcode")]
    public async Task<IActionResult> DownloadGCode(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        if (job is null) return NotFound();
        if (job.HybridGCodePath is null) return BadRequest("Hybrid G-code not yet generated.");
        if (!System.IO.File.Exists(job.HybridGCodePath)) return NotFound("G-code file not found on disk.");

        var stream = System.IO.File.OpenRead(job.HybridGCodePath);
        return File(stream, "text/plain", $"hybrid_{id}.gcode");
    }

    /// <summary>
    /// Merges per-bed sliced G-code into one final print G-code, interleaving layers.
    /// Creates a real merged job that can be selected in Hybrid Preview, Dashboard, etc.
    /// </summary>
    [HttpPost("merge-beds")]
    public async Task<IActionResult> MergeBeds([FromBody] MergeBedsRequest request, CancellationToken ct)
    {
        if (request.JobIds.Count < 2) return BadRequest("At least 2 bed jobs required.");

        var jobs = new List<Domain.Entities.PrintJob>();
        foreach (var jid in request.JobIds)
        {
            var j = await _jobs.GetByIdAsync(jid, ct);
            if (j is null) return NotFound($"Job {jid} not found.");
            if (j.PrintGCodePath is null) return BadRequest($"Job {jid} has not been sliced yet.");
            if (!System.IO.File.Exists(j.PrintGCodePath)) return NotFound($"G-code for job {jid} not found on disk.");
            jobs.Add(j);
        }

        // Use the first bed job as template for the merged job's profile references
        var template = jobs[0];

        // Deduplicate merged job name
        var mergedName = request.Name ?? $"{template.Name} (merged {jobs.Count} beds)";
        var allExisting = await _jobs.GetAllAsync(ct);
        var usedNames = new HashSet<string>(allExisting.Select(j => j.Name), StringComparer.OrdinalIgnoreCase);
        if (usedNames.Contains(mergedName))
        {
            for (var i = 1; ; i++)
            {
                var candidate = $"{mergedName} ({i})";
                if (!usedNames.Contains(candidate)) { mergedName = candidate; break; }
            }
        }

        // Create a real merged job
        var mergedJob = Domain.Entities.PrintJob.Create(
            name: mergedName,
            stlFilePath: template.StlFilePath,
            machineProfileId: template.MachineProfileId,
            printProfileId: template.PrintProfileId,
            materialId: template.MaterialId,
            supportEnabled: template.SupportEnabled,
            parentJobId: null);

        // Create output directory and merge
        var jobDir = Path.Combine(_storage.Root, "jobs", mergedJob.Id.ToString());
        Directory.CreateDirectory(jobDir);
        var outputPath = Path.Combine(jobDir, "print.gcode");

        var bedPaths = jobs.Select(j => j.PrintGCodePath!).ToList();

        // Hybrid mode: automatically generate CNC toolpaths for each bed job
        if (request.Hybrid && request.CncToolId.HasValue)
        {
            for (var i = 0; i < jobs.Count; i++)
            {
                var bedJob = jobs[i];
                // Always regenerate CNC toolpaths to ensure they use the latest
                // machine config and coordinate translation.
                await _mediator.Send(new GenerateToolpathsCommand(
                    bedJob.Id,
                    request.CncToolId.Value,
                    request.MachineEveryNLayers,
                    request.MachineInnerWalls,
                    request.AvoidSupports,
                    request.SupportClearanceMm,
                    request.AutoMachiningFrequency,
                    request.ZSafetyOffsetMm,
                    request.SpindleRpmOverride), ct);

                // Re-read the job to get the updated toolpath path
                var updated = await _jobs.GetByIdAsync(bedJob.Id, ct);
                if (updated is not null)
                    jobs[i] = updated;
            }
        }

        var cncPaths = jobs.Where(j => j.ToolpathGCodePath is not null && System.IO.File.Exists(j.ToolpathGCodePath))
            .Select(j => j.ToolpathGCodePath!).ToList();
        string? mergedToolpathPath = null;

        string? hybridOutputPath = null;

        if (request.Hybrid && cncPaths.Count > 0)
        {
            // Hybrid merge: one file with print + CNC interleaved per interval
            // Sequence: print N on each bed → CNC N on each bed → repeat
            hybridOutputPath = Path.Combine(jobDir, "hybrid.gcode");

            // Load custom G-code blocks for injection at each interval
            var customBlocks = await BuildCustomBlocksAsync(ct);
            await _merger.MergeHybridAsync(bedPaths, cncPaths, request.LayerStep, hybridOutputPath, customBlocks, ct);

            // Also write the print-only merge for preview compatibility
            await _merger.MergeAsync(bedPaths, outputPath, request.LayerStep, ct);
        }
        else
        {
            // Print-only merge: interleave print layers across beds
            await _merger.MergeAsync(bedPaths, outputPath, request.LayerStep, ct);

            // Also merge CNC toolpath files separately if they exist
            if (cncPaths.Count > 0)
            {
                mergedToolpathPath = Path.Combine(jobDir, "toolpath.gcode");
                await _merger.MergeCncAsync(cncPaths, mergedToolpathPath, request.LayerStep, ct);
            }
        }

        // Count total layers from merged print output
        var totalLayers = 0;
        foreach (var line in await System.IO.File.ReadAllLinesAsync(outputPath, ct))
            if (line.StartsWith(";LAYER:")) totalLayers++;

        // Mark the merged job as sliced (so it appears in Dashboard, Hybrid Planner, Preview)
        mergedJob.MarkSlicing();
        mergedJob.MarkSlicingComplete(outputPath, Math.Max(totalLayers, 1));

        if (hybridOutputPath is not null)
        {
            // Write a separate CNC-only merged toolpath for Hybrid Preview simulation
            var mergedToolpathForPreview = Path.Combine(jobDir, "toolpath.gcode");
            await _merger.MergeCncAsync(cncPaths, mergedToolpathForPreview, request.LayerStep, ct);

            // ToolpathGCodePath = CNC-only merge (for preview/simulation parsing)
            // HybridGCodePath   = combined print+CNC interleaved (for download/execution)
            mergedJob.MarkGeneratingToolpaths();
            mergedJob.MarkToolpathsComplete(mergedToolpathForPreview);
            mergedJob.MarkPlanningHybrid();
            mergedJob.MarkReady(hybridOutputPath);
        }
        else if (mergedToolpathPath is not null)
        {
            mergedJob.MarkGeneratingToolpaths();
            mergedJob.MarkToolpathsComplete(mergedToolpathPath);
        }

        // Set parent references on per-bed jobs
        await _jobs.AddAsync(mergedJob, ct);

        return Ok(new {
            jobId = mergedJob.Id,
            mergedPath = outputPath,
            beds = jobs.Count,
            layerStep = request.LayerStep,
            totalLayers,
        });
    }

    private async Task<MultiBedMerger.CustomBlocks> BuildCustomBlocksAsync(CancellationToken ct)
    {
        var enabled = await _customGCode.GetEnabledAsync(ct);
        string collect(Domain.Enums.GCodeTrigger trigger) =>
            string.Join("\n", enabled
                .Where(b => b.Trigger == trigger)
                .OrderBy(b => b.SortOrder)
                .Select(b => b.GCodeContent));
        return new MultiBedMerger.CustomBlocks
        {
            BeforePrinting = collect(Domain.Enums.GCodeTrigger.BeforePrinting),
            AfterPrinting = collect(Domain.Enums.GCodeTrigger.AfterPrinting),
            BeforeMachining = collect(Domain.Enums.GCodeTrigger.BeforeMachining),
            AfterMachining = collect(Domain.Enums.GCodeTrigger.AfterMachining),
            JobStart = collect(Domain.Enums.GCodeTrigger.JobStart),
            JobEnd = collect(Domain.Enums.GCodeTrigger.JobEnd),
        };
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id, CancellationToken ct)
    {
        var job = await _jobs.GetByIdAsync(id, ct);
        if (job is null) return NotFound();

        // Delete all files in the job directory (STL + all generated G-code)
        var jobDir = Path.GetDirectoryName(job.StlFilePath);
        if (jobDir is not null && Directory.Exists(jobDir))
        {
            try { Directory.Delete(jobDir, recursive: true); }
            catch { /* best-effort */ }
        }

        await _jobs.DeleteAsync(id, ct);
        return NoContent();
    }
}

public record UploadStlRequest(
    [FromForm] IFormFile File,
    [FromForm] string JobName,
    [FromForm] Guid MachineProfileId,
    [FromForm] Guid PrintProfileId,
    [FromForm] Guid MaterialId,
    [FromForm] bool SupportEnabled = false,
    [FromForm] string SupportType = "normal",
    [FromForm] string SupportPlacement = "everywhere",
    [FromForm] string InfillPattern = "grid",
    [FromForm] double? InfillDensityPct = 15,
    [FromForm] string SupportInfillPattern = "grid",
    [FromForm] double? SupportInfillDensityPct = null,
    [FromForm] string AdhesionType = "none",
    [FromForm] bool GcodeHoming = true,
    [FromForm] bool GcodeLevelling = false,
    [FromForm] int? BedIndex = null,
    [FromForm] Guid? ParentJobId = null,
    [FromForm] bool ApplyCustomGCodeBlocks = true);

public record CreateJobRequest(string JobName, Guid MachineProfileId, Guid PrintProfileId, Guid MaterialId);
public record GenerateToolpathsRequest(
    Guid   CncToolId,
    int    MachineEveryNLayers,
    bool   MachineInnerWalls        = false,
    bool   AvoidSupports            = false,
    double SupportClearanceMm       = 2.0,
    bool   AutoMachiningFrequency   = false,
    double ZSafetyOffsetMm          = 0.0,
    int?   SpindleRpmOverride       = null,
    double SpindleStartX            = 0.0,
    double SpindleStartY            = 0.0,
    double? SpindleStartZ           = null,
    double SpindleEndX              = 0.0,
    double SpindleEndY              = 0.0,
    double? SpindleEndZ             = null,
    int    SkipMachiningLayers      = 0);
public record PlanHybridRequest(int MachineEveryNLayers);
public record MergeBedsRequest(
    IReadOnlyList<Guid> JobIds,
    int    LayerStep              = 1,
    string? Name                  = null,
    bool   Hybrid                 = false,
    // CNC parameters — only used when Hybrid = true
    Guid?  CncToolId              = null,
    int    MachineEveryNLayers    = 10,
    bool   MachineInnerWalls      = false,
    bool   AvoidSupports          = false,
    double SupportClearanceMm     = 2.0,
    bool   AutoMachiningFrequency = false,
    double ZSafetyOffsetMm        = 0.0,
    int?   SpindleRpmOverride     = null);
