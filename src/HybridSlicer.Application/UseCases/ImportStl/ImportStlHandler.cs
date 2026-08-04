using HybridSlicer.Application.Common;
using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Exceptions;
using MediatR;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HybridSlicer.Application.UseCases.ImportStl;

public sealed class ImportStlHandler : IRequestHandler<ImportStlCommand, ImportStlResult>
{
    private const long MaxStlSizeBytes = 256 * 1024 * 1024; // 256 MB
    private static readonly byte[] StlAsciiPrefix = "solid"u8.ToArray();

    private readonly IPrintJobRepository _jobs;
    private readonly IMachineProfileRepository _machines;
    private readonly StorageOptions _storage;
    private readonly ILogger<ImportStlHandler> _logger;

    public ImportStlHandler(
        IPrintJobRepository jobs,
        IMachineProfileRepository machines,
        IOptions<StorageOptions> storageOpts,
        ILogger<ImportStlHandler> logger)
    {
        _jobs = jobs;
        _machines = machines;
        _storage = storageOpts.Value;
        _logger = logger;
    }

    public async Task<ImportStlResult> Handle(ImportStlCommand cmd, CancellationToken ct)
    {
        _logger.LogInformation("Importing STL for job '{Name}'", cmd.JobName);

        if (cmd.StlStream.Length > MaxStlSizeBytes)
            throw new DomainException("FILE_TOO_LARGE",
                $"STL file exceeds the {MaxStlSizeBytes / 1024 / 1024} MB limit.");

        if (!await _machines.ExistsAsync(cmd.MachineProfileId, ct))
            throw new DomainException("MACHINE_NOT_FOUND",
                $"Machine profile {cmd.MachineProfileId} does not exist.");

        // Build job storage directory
        var jobId = Guid.NewGuid();
        var jobDir = Path.Combine(_storage.Root, "jobs", jobId.ToString());
        Directory.CreateDirectory(jobDir);

        // Sanitise the filename to prevent path traversal
        var safeFileName = Path.GetFileName(cmd.OriginalFileName);
        if (string.IsNullOrWhiteSpace(safeFileName) || !safeFileName.EndsWith(".stl", StringComparison.OrdinalIgnoreCase))
            safeFileName = "model.stl";

        var destination = Path.Combine(jobDir, safeFileName);

        await using (var fs = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None,
                         bufferSize: 65536, useAsync: true))
        {
            await cmd.StlStream.CopyToAsync(fs, ct);
        }

        ValidateStlHeader(destination);

        // Deduplicate job name: append (1), (2), etc. if name already exists
        var jobName = cmd.JobName;
        var allJobs = await _jobs.GetAllAsync(ct);
        var existingNames = new HashSet<string>(allJobs.Select(j => j.Name), StringComparer.OrdinalIgnoreCase);
        if (existingNames.Contains(jobName))
        {
            for (var i = 1; ; i++)
            {
                var candidate = $"{jobName} ({i})";
                if (!existingNames.Contains(candidate)) { jobName = candidate; break; }
            }
        }

        var job = PrintJob.Create(
            jobName,
            destination,
            cmd.MachineProfileId,
            cmd.PrintProfileId,
            cmd.MaterialId,
            cmd.SupportEnabled,
            cmd.SupportType,
            cmd.SupportPlacement,
            cmd.InfillPattern,
            cmd.InfillDensityPct,
            cmd.SupportInfillPattern,
            cmd.SupportInfillDensityPct,
            cmd.AdhesionType,
            cmd.GcodeHoming,
            cmd.GcodeLevelling,
            cmd.BedIndex,
            cmd.ParentJobId);

        await _jobs.AddAsync(job, ct);

        _logger.LogInformation("STL imported. JobId={JobId} Path={Path}", job.Id, destination);

        return new ImportStlResult(job.Id, destination, new FileInfo(destination).Length);
    }

    private static void ValidateStlHeader(string path)
    {
        using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);

        // Minimum binary STL is 84 bytes (80 header + 4 count)
        if (fs.Length < 84)
            throw new DomainException("INVALID_STL", "File is too small to be a valid STL.");

        Span<byte> header = stackalloc byte[80];
        _ = fs.Read(header);

        var isBinaryStl = !header[..5].SequenceEqual(StlAsciiPrefix);
        if (!isBinaryStl) return; // ASCII STL — CuraEngine validates it fully

        Span<byte> countBytes = stackalloc byte[4];
        fs.Seek(80, SeekOrigin.Begin);
        _ = fs.Read(countBytes);
        var triangleCount = BitConverter.ToUInt32(countBytes);

        if (triangleCount == 0)
            throw new DomainException("INVALID_STL", "Binary STL reports zero triangles.");

        // Verify expected file size
        var expectedSize = 84L + triangleCount * 50L;
        if (fs.Length != expectedSize)
            throw new DomainException("INVALID_STL",
                $"Binary STL size mismatch: expected {expectedSize} bytes for {triangleCount} triangles, got {fs.Length}.");
    }
}
