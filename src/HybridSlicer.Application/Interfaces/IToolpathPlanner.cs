using HybridSlicer.Domain.ValueObjects;

namespace HybridSlicer.Application.Interfaces;

/// <summary>
/// Port for CNC toolpath generation. Takes part geometry and tool specs,
/// produces G-code for a single machining operation at one layer height.
/// </summary>
public interface IToolpathPlanner
{
    /// <summary>
    /// Generates a contour-milling toolpath from Cura-extracted wall paths.
    /// This is the primary method — it uses the actual outer-wall G-code paths
    /// that Cura computed, applying tool-radius compensation and machine offsets.
    /// </summary>
    Task<ToolpathResult> PlanFromWallPathsAsync(
        WallPathsRequest request,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Fallback: generates a contour-milling toolpath by slicing the STL at Z.
    /// Used only when no print G-code is available.
    /// </summary>
    Task<ToolpathResult> PlanContourAsync(
        ToolpathRequest request,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Request to generate a CNC toolpath from pre-extracted Cura wall paths.
/// Correct CRC formula: CNC offset = tool_radius + nozzle_radius (outward for outer wall).
/// </summary>
public sealed record WallPathsRequest(
    /// <summary>Per-segment ordered XY points extracted from Cura wall G-code.</summary>
    IReadOnlyList<IReadOnlyList<(double X, double Y)>> WallPaths,
    double ZHeightMm,
    double ToolDiameterMm,
    /// <summary>Nozzle/line width — used for cutter-radius compensation offset.</summary>
    double NozzleDiameterMm,
    double FeedRateMmPerMin,
    int SpindleRpm,
    MachineOffset MachineOffset,
    double SafeClearanceHeightMm,
    /// <summary>True = outer wall (offset outward). False = inner wall (offset inward).</summary>
    bool IsOuterWall = true,
    bool ClimbMilling = true,
    /// <summary>
    /// Optional support path segments for this layer.
    /// When non-empty, they are buffered by <see cref="SupportClearanceMm"/> and
    /// subtracted from the milling area so the tool never enters support regions.
    /// </summary>
    /// <summary>Top of the printed part at this machining event. When set, retract height
    /// uses this instead of ZHeightMm so the tool retracts above the part top, not the tip.</summary>
    double? PartTopZMm = null,
    IReadOnlyList<IReadOnlyList<(double X, double Y)>>? SupportPaths = null,
    /// <summary>XY clearance (mm) added around each support region as a forbidden zone.</summary>
    double SupportClearanceMm = 2.0,
    /// <summary>Spindle spin-up dwell time in seconds after M3.</summary>
    double SpindleDwellSec = 3.0,
    /// <summary>Lead-in/out arc radius (mm). Tool enters tangentially from this distance off-part.</summary>
    double LeadInRadiusMm = 2.0,
    /// <summary>End overlap distance (mm). Tool continues past the start point to re-cut the entry.</summary>
    double EndOverlapMm = 3.0,
    /// <summary>Lead-in feed rate as a fraction of cutting feed (0.0–1.0).</summary>
    double LeadInFeedFraction = 0.5,
    /// <summary>Pass index (0-based) used to rotate the start point between passes.</summary>
    int PassIndex = 0);

public sealed record ToolpathRequest(
    string StlFilePath,
    double ZHeightMm,
    double ToolDiameterMm,
    double MaxDepthOfCutMm,
    double FeedRateMmPerMin,
    int SpindleRpm,
    MachineOffset MachineOffset,
    double SafeClearanceHeightMm,
    bool ClimbMilling = true);

public sealed record ToolpathResult(
    string GCode,
    bool IsEmpty,
    IReadOnlyList<BoundingBox2D> ToolpathBounds,
    IReadOnlyList<UnmachinableRegion>? UnmachinableRegions = null);

/// <summary>
/// Describes a region that could not be machined and the reason why.
/// </summary>
public sealed record UnmachinableRegion(
    double ZHeightMm,
    /// <summary>"ToolTooWide" | "FluteTooShort" | "SupportBlocked" | "PartInteriorCollision" | "InvalidCrcDirection"</summary>
    string Reason,
    BoundingBox2D Bounds);

public sealed record BoundingBox2D(
    double MinX, double MinY,
    double MaxX, double MaxY)
{
    public bool Intersects(BoundingBox2D other)
        => MinX <= other.MaxX && MaxX >= other.MinX
        && MinY <= other.MaxY && MaxY >= other.MinY;
}
