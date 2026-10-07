using System.Globalization;
using System.Text;
using HybridSlicer.Application.Interfaces;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Toolpath;

/// <summary>
/// Generates 3-axis surface machining G-code from alignmesh deviation data.
/// Positive deviations = excess material to remove by raster/zigzag milling.
/// </summary>
public sealed class DeviationMachiningPlanner : ISurfaceMachiningPlanner
{
    private readonly ILogger<DeviationMachiningPlanner> _logger;

    public DeviationMachiningPlanner(ILogger<DeviationMachiningPlanner> logger)
        => _logger = logger;

    public Task<SurfaceMachiningResult> GenerateAsync(
        SurfaceMachiningRequest req, CancellationToken ct = default)
    {
        var ic = CultureInfo.InvariantCulture;
        var warnings = new List<string>();

        // ── 1. Validate inputs ──────────────────────────────────────────────
        int nPoints = req.PointDeviations.Length;
        if (nPoints == 0 || req.PointPositions.Length < nPoints * 3)
            return Task.FromResult(Empty("No deviation data provided."));

        if (req.TransformMatrix.Length != 16)
            return Task.FromResult(Empty("Invalid transform matrix (expected 16 elements)."));

        // ── 2. Inverse transform: nominal → machine frame ───────────────────
        var tInv = Invert4x4(req.TransformMatrix);

        // ── 3. Filter + transform points ────────────────────────────────────
        double toolRadius = req.ToolDiameterMm / 2.0;
        double maxDoc = req.MaxDepthPerPass > 0
            ? req.MaxDepthPerPass
            : req.ToolMaxDepthOfCutMm;
        if (maxDoc <= 0) maxDoc = toolRadius; // fallback

        var excessPts = new List<(double X, double Y, double Z, double Dev)>();
        for (int i = 0; i < nPoints; i++)
        {
            double dev = req.PointDeviations[i];
            if (dev <= req.DeviationThreshold) continue;

            double nx = req.PointPositions[i * 3];
            double ny = req.PointPositions[i * 3 + 1];
            double nz = req.PointPositions[i * 3 + 2];

            // Transform nominal → machine coords
            var (mx, my, mz) = Transform(tInv, nx, ny, nz);
            excessPts.Add((mx, my, mz, dev));
        }

        if (excessPts.Count == 0)
            return Task.FromResult(Empty("No points exceed the deviation threshold — nothing to machine."));

        _logger.LogInformation("Surface machining: {N} excess points (of {Total}), tool Ø{D}mm",
            excessPts.Count, nPoints, req.ToolDiameterMm);

        // ── 4. Bounding box ─────────────────────────────────────────────────
        double minX = double.MaxValue, minY = double.MaxValue, minZ = double.MaxValue;
        double maxX = double.MinValue, maxY = double.MinValue, maxZ = double.MinValue;
        double globalMaxDev = 0;
        foreach (var p in excessPts)
        {
            if (p.X < minX) minX = p.X; if (p.X > maxX) maxX = p.X;
            if (p.Y < minY) minY = p.Y; if (p.Y > maxY) maxY = p.Y;
            if (p.Z < minZ) minZ = p.Z; if (p.Z > maxZ) maxZ = p.Z;
            if (p.Dev > globalMaxDev) globalMaxDev = p.Dev;
        }

        // Expand by tool radius for approach margin
        minX -= toolRadius; minY -= toolRadius;
        maxX += toolRadius; maxY += toolRadius;

        // ── 5. Build 2D deviation grid ──────────────────────────────────────
        double stepover = req.ToolDiameterMm * req.StepoverPercent / 100.0;
        if (stepover < 0.1) stepover = 0.1;

        int nx_ = Math.Max(1, (int)Math.Ceiling((maxX - minX) / stepover));
        int ny_ = Math.Max(1, (int)Math.Ceiling((maxY - minY) / stepover));

        // Limit grid size to prevent memory issues
        if ((long)nx_ * ny_ > 10_000_000)
        {
            warnings.Add($"Grid too large ({nx_}x{ny_}). Increase stepover.");
            return Task.FromResult(new SurfaceMachiningResult(
                "", true, 0, 0, 0,
                Array.Empty<MachiningRegion>(),
                Array.Empty<SkippedRegion>(),
                warnings));
        }

        var grid = new GridCell[nx_, ny_];
        for (int ix = 0; ix < nx_; ix++)
            for (int iy = 0; iy < ny_; iy++)
                grid[ix, iy] = new GridCell();

        foreach (var p in excessPts)
        {
            int ix = Math.Clamp((int)((p.X - minX) / stepover), 0, nx_ - 1);
            int iy = Math.Clamp((int)((p.Y - minY) / stepover), 0, ny_ - 1);
            if (p.Dev > grid[ix, iy].MaxDev) grid[ix, iy].MaxDev = p.Dev;
            grid[ix, iy].NominalZ = p.Z; // last write wins; approximation
            grid[ix, iy].HasData = true;
        }

        // ── 6. Compute Z passes ─────────────────────────────────────────────
        int globalPasses = Math.Max(1, (int)Math.Ceiling(globalMaxDev / maxDoc));
        double safeZ = maxZ + req.SafeClearanceHeightMm;

        // Envelope check
        double cncDx = req.CncOffsetX, cncDy = req.CncOffsetY, cncDz = req.CncOffsetZ;
        if (req.MachineEnvelopeX > 0 && maxX + cncDx > req.MachineEnvelopeX)
            warnings.Add("Some points exceed machine X travel.");
        if (req.MachineEnvelopeY > 0 && maxY + cncDy > req.MachineEnvelopeY)
            warnings.Add("Some points exceed machine Y travel.");

        // Axis names
        string ax = "X", ay = "Y", az = "Z";
        if (!string.IsNullOrEmpty(req.CncAxes) && req.CncAxes.Length >= 3)
        {
            ax = req.CncAxes[0].ToString();
            ay = req.CncAxes[1].ToString();
            az = req.CncAxes[2].ToString();
        }

        // ── 7. G-code emission ──────────────────────────────────────────────
        var sb = new StringBuilder();
        int rpm = req.ToolRpm;
        double feed = req.ToolFeedMmPerMin;
        double plungeFeed = feed * 0.3;
        int cuttingMoves = 0;

        // Header
        sb.AppendLine("; === CMM Surface Machining ===");
        sb.AppendLine($"; Tool: Ø{req.ToolDiameterMm:F2}mm  Tip: {req.ToolTipShape}");
        sb.AppendLine($"; Stepover: {stepover:F2}mm ({req.StepoverPercent:F0}%)");
        sb.AppendLine($"; Max depth/pass: {maxDoc:F3}mm  Passes: {globalPasses}");
        sb.AppendLine($"; Deviation threshold: {req.DeviationThreshold:F3}mm");
        sb.AppendLine($"; Finish allowance: {req.FinishAllowance:F3}mm");
        sb.AppendLine($"; Pattern: {req.PatternType}");
        sb.AppendLine($"; Points machined: {excessPts.Count} of {nPoints}");
        sb.AppendLine($"; Grid: {nx_}x{ny_}  Safe Z: {safeZ:F3}mm");
        sb.AppendLine("G90 G21 ; absolute positioning, mm");
        sb.AppendLine($"G0 {az}{F(safeZ + cncDz)} F6000 ; retract to safe height");
        sb.AppendLine($"M3 S{rpm} ; spindle start");
        if (req.SpindleDwellSec > 0)
            sb.AppendLine($"G4 S{req.SpindleDwellSec:F1} ; spindle spin-up dwell");

        var machinedRegions = new List<MachiningRegion>();
        var skippedRegions = new List<SkippedRegion>();

        // Per-pass raster
        for (int pass = 0; pass < globalPasses; pass++)
        {
            ct.ThrowIfCancellationRequested();
            double passTopZ = maxZ + globalMaxDev - pass * maxDoc;
            double passCutZ = Math.Max(passTopZ - maxDoc, minZ + req.FinishAllowance);

            sb.AppendLine($"; --- Pass {pass + 1}/{globalPasses}  Z={F(passCutZ)} ---");

            bool reverseX = false;
            for (int iy = 0; iy < ny_; iy++)
            {
                // Build segments along this row
                var segments = new List<(int startIx, int endIx)>();
                int segStart = -1;

                int fromIx = reverseX ? nx_ - 1 : 0;
                int toIx = reverseX ? -1 : nx_;
                int stepIx = reverseX ? -1 : 1;

                for (int ix = fromIx; ix != toIx; ix += stepIx)
                {
                    var cell = grid[ix, iy];
                    if (cell.HasData && cell.MaxDev > (globalMaxDev - (pass + 1) * maxDoc + req.DeviationThreshold))
                    {
                        if (segStart < 0) segStart = ix;
                    }
                    else
                    {
                        if (segStart >= 0)
                        {
                            segments.Add((segStart, ix - stepIx));
                            segStart = -1;
                        }
                    }
                }
                if (segStart >= 0)
                    segments.Add((segStart, reverseX ? fromIx : nx_ - 1));

                foreach (var (sIx, eIx) in segments)
                {
                    double y = minY + iy * stepover + cncDy;
                    double startX = minX + sIx * stepover + cncDx;
                    double endX = minX + eIx * stepover + cncDx;
                    double cutZ = passCutZ + cncDz;

                    // Retract + rapid to start
                    sb.AppendLine($"G0 {az}{F(safeZ + cncDz)} F6000");
                    sb.AppendLine($"G0 {ax}{F(startX)} {ay}{F(y)} F6000");
                    sb.AppendLine($"G1 {az}{F(cutZ)} F{plungeFeed:F0}");

                    // Cut across
                    sb.AppendLine($"G1 {ax}{F(endX)} F{feed:F0}");
                    cuttingMoves++;
                }

                if (req.PatternType == "zigzag") reverseX = !reverseX;
            }
        }

        // Footer
        sb.AppendLine($"G0 {az}{F(safeZ + cncDz)} F6000 ; final retract");
        sb.AppendLine("M5 ; spindle stop");
        sb.AppendLine("; === End Surface Machining ===");

        // Compute regions
        machinedRegions.Add(new MachiningRegion(minX, minY, minZ, maxX, maxY, maxZ, globalMaxDev));

        // Estimate time: assume average move is stepover length at feed rate + rapids
        double totalCutDist = cuttingMoves * (maxX - minX) * 0.5; // rough estimate
        double cutTime = totalCutDist / feed * 60; // seconds
        double rapidTime = cuttingMoves * 0.5; // ~0.5s per retract+rapid
        double estTime = cutTime + rapidTime;

        var result = new SurfaceMachiningResult(
            sb.ToString(),
            cuttingMoves == 0,
            globalPasses,
            cuttingMoves,
            estTime,
            machinedRegions,
            skippedRegions,
            warnings);

        _logger.LogInformation("Surface machining G-code: {Passes} passes, {Moves} cuts, ~{Time:F0}s",
            globalPasses, cuttingMoves, estTime);

        return Task.FromResult(result);
    }

    private static string F(double v) => v.ToString("F3", CultureInfo.InvariantCulture);

    private static SurfaceMachiningResult Empty(string warning)
        => new("", true, 0, 0, 0,
            Array.Empty<MachiningRegion>(),
            Array.Empty<SkippedRegion>(),
            new[] { warning });

    /// <summary>4x4 row-major matrix inversion.</summary>
    private static double[] Invert4x4(double[] m)
    {
        // For rigid transforms (rotation + translation), the inverse is:
        // R^T | -R^T * t
        // 0   | 1
        double r00 = m[0], r01 = m[1], r02 = m[2], tx = m[3];
        double r10 = m[4], r11 = m[5], r12 = m[6], ty = m[7];
        double r20 = m[8], r21 = m[9], r22 = m[10], tz = m[11];

        double itx = -(r00 * tx + r10 * ty + r20 * tz);
        double ity = -(r01 * tx + r11 * ty + r21 * tz);
        double itz = -(r02 * tx + r12 * ty + r22 * tz);

        return new[]
        {
            r00, r10, r20, itx,
            r01, r11, r21, ity,
            r02, r12, r22, itz,
            0, 0, 0, 1
        };
    }

    private static (double X, double Y, double Z) Transform(double[] m, double x, double y, double z)
    {
        return (
            m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11]);
    }

    private struct GridCell
    {
        public double MaxDev;
        public double NominalZ;
        public bool HasData;
    }
}
