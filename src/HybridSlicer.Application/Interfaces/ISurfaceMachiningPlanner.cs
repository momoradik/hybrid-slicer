namespace HybridSlicer.Application.Interfaces;

/// <summary>
/// Generates 3-axis surface machining G-code from deviation data.
/// Takes alignment results from alignmesh (point positions + signed deviations)
/// and produces raster/zigzag toolpaths to remove excess material.
/// </summary>
public interface ISurfaceMachiningPlanner
{
    Task<SurfaceMachiningResult> GenerateAsync(
        SurfaceMachiningRequest request,
        CancellationToken ct = default);
}

public sealed record SurfaceMachiningRequest(
    /// <summary>Flat XYZ triples in nominal/reference frame from alignmesh.</summary>
    double[] PointPositions,
    /// <summary>Signed deviation per point — positive = excess material to remove.</summary>
    double[] PointDeviations,
    /// <summary>4x4 row-major transform (measured→nominal) from alignmesh.</summary>
    double[] TransformMatrix,

    // Tool & machine (loaded from DB by controller)
    double ToolDiameterMm,
    double ToolMaxDepthOfCutMm,
    int ToolRpm,
    double ToolFeedMmPerMin,
    string ToolTipShape,
    double SafeClearanceHeightMm,
    double CncOffsetX,
    double CncOffsetY,
    double CncOffsetZ,
    string CncAxes,
    double MachineEnvelopeX,
    double MachineEnvelopeY,
    double MachineEnvelopeZ,

    // Machining parameters
    double DeviationThreshold = 0.05,
    double StepoverPercent = 40,
    double MaxDepthPerPass = 0,
    double FinishAllowance = 0,
    string PatternType = "zigzag",
    bool ClimbMilling = true,
    double SpindleDwellSec = 3.0);

public sealed record SurfaceMachiningResult(
    string GCode,
    bool IsEmpty,
    int TotalPasses,
    int CuttingMoves,
    double EstimatedTimeSec,
    IReadOnlyList<MachiningRegion> MachinedRegions,
    IReadOnlyList<SkippedRegion> SkippedRegions,
    IReadOnlyList<string> Warnings);

public sealed record MachiningRegion(
    double MinX, double MinY, double MinZ,
    double MaxX, double MaxY, double MaxZ,
    double MaxDeviation);

public sealed record SkippedRegion(
    double MinX, double MinY,
    double MaxX, double MaxY,
    string Reason);
