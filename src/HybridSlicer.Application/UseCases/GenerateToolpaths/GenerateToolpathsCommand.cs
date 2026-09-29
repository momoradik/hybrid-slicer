using HybridSlicer.Application.Interfaces;
using MediatR;

namespace HybridSlicer.Application.UseCases.GenerateToolpaths;

public sealed record GenerateToolpathsCommand(
    Guid JobId,
    Guid CncToolId,
    int  MachineEveryNLayers,
    /// <summary>When true, also machine inner-wall surfaces (holes, pockets).</summary>
    bool MachineInnerWalls = false,
    /// <summary>
    /// When true, support regions are treated as no-cut zones.
    /// The toolpath is clipped to avoid them rather than skipping the whole layer.
    /// </summary>
    bool   AvoidSupports      = false,
    /// <summary>XY clearance (mm) kept between the tool and any support geometry.</summary>
    double SupportClearanceMm = 2.0,
    /// <summary>
    /// When true, machining layers are computed automatically based on tool flute length
    /// so that lower geometry never becomes unreachable. MachineEveryNLayers is ignored.
    /// Rule: machine when pendingPrintHeight >= tool.FluteLengthMm * 0.8.
    /// </summary>
    bool AutoMachiningFrequency = false,
    /// <summary>
    /// Additional Z lift (mm) applied to every machining pass above the nominal layer height.
    /// Default 0 = machine at exact layer surface. Positive values add safety distance in Z.
    /// Applied consistently from the first to the last machining layer.
    /// </summary>
    double ZSafetyOffsetMm = 0.0,
    /// <summary>
    /// Optional spindle speed override (RPM). When null, the tool's RecommendedRpm is used.
    /// Allows the user to fine-tune spindle speed per job without changing the tool definition.
    /// </summary>
    int? SpindleRpmOverride = null,
    /// <summary>Spindle start position X (mm). Tool moves here before first machining pass.</summary>
    double SpindleStartX = 0.0,
    /// <summary>Spindle start position Y (mm).</summary>
    double SpindleStartY = 0.0,
    /// <summary>Spindle start position Z (mm). Null = machine safe clearance height.</summary>
    double? SpindleStartZ = null,
    /// <summary>Spindle end position X (mm). Tool returns here after last pass.</summary>
    double SpindleEndX = 0.0,
    /// <summary>Spindle end position Y (mm).</summary>
    double SpindleEndY = 0.0,
    /// <summary>Spindle end position Z (mm). Null = same as start Z.</summary>
    double? SpindleEndZ = null,
    /// <summary>Skip the first N part layers before starting machining.</summary>
    int SkipMachiningLayers = 0,
    /// <summary>Extra depth (mm) below the band bottom to overlap into the previous band.
    /// Compensates for rounded burr tips where the cutting diameter tapers near the tip.
    /// 0 for flat end mills, ~1.0–1.5 for ball/rounded burrs.</summary>
    double TipOverlapMm = 0.0,
    /// <summary>Minimum Z above the bed (mm). The tool tip never goes below this.</summary>
    double BedClearanceMm = 0.1,
    /// <summary>Number of already-machined layers below each band to re-cut.
    /// Compensates for ooze/sag on the first layers printed after a CNC pass.
    /// 0 = current behaviour (no overlap). E.g. 2 with frequency 5 at layer 15 → cuts layers 9–15.</summary>
    int RemachineLowerLayers = 0) : IRequest<GenerateToolpathsResult>;

public sealed record GenerateToolpathsResult(
    Guid                 JobId,
    int                  ToolpathCount,
    IReadOnlyList<int>   MachinedAtLayers,
    IReadOnlyList<UnmachinableRegion> UnmachinableRegions,
    IReadOnlyList<string> Warnings);
