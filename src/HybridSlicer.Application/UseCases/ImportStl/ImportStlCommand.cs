using MediatR;

namespace HybridSlicer.Application.UseCases.ImportStl;

public sealed record ImportStlCommand(
    string JobName,
    Stream StlStream,
    string OriginalFileName,
    Guid MachineProfileId,
    Guid PrintProfileId,
    Guid MaterialId,
    bool SupportEnabled = false,
    string SupportType = "normal",
    string SupportPlacement = "everywhere",
    string InfillPattern = "grid",
    double? InfillDensityPct = null,
    string SupportInfillPattern = "grid",
    double? SupportInfillDensityPct = null,
    string AdhesionType = "none",
    bool GcodeHoming = true,
    bool GcodeLevelling = false,
    int? BedIndex = null,
    Guid? ParentJobId = null,
    bool ApplyCustomGCodeBlocks = true) : IRequest<ImportStlResult>;

public sealed record ImportStlResult(
    Guid JobId,
    string StoredFilePath,
    long FileSizeBytes);
