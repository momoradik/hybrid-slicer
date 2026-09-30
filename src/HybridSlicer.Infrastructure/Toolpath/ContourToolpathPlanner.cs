using System.Text;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.ValueObjects;
using Microsoft.Extensions.Logging;
using NetTopologySuite.Geometries;
// UnmachinableRegion is defined in HybridSlicer.Application.Interfaces (IToolpathPlanner.cs)

namespace HybridSlicer.Infrastructure.Toolpath;

/// <summary>
/// Generates 2.5-D contour-milling CNC G-code from either:
///   (A) Pre-extracted Cura wall paths  [primary — PlanFromWallPathsAsync]
///   (B) Direct STL cross-section       [fallback — PlanContourAsync]
///
/// Tool-radius compensation (CRC) for outer wall paths:
///   CNC tool center = Cura nozzle path + (tool_radius + nozzle_radius)  [outward]
///   Rationale: Cura nozzle center is nozzle_radius INWARD of the part outer surface.
///              CNC tool center must be tool_radius OUTWARD of that same surface.
///              Net offset from Cura path = nozzle_radius + tool_radius.
///
/// For inner wall paths (holes/pockets):
///   CNC tool center = Cura nozzle path − (tool_radius + nozzle_radius) [inward]
/// </summary>
public sealed class ContourToolpathPlanner : IToolpathPlanner
{
    private static readonly GeometryFactory GF = new(new PrecisionModel(1000), 0);
    private readonly ILogger<ContourToolpathPlanner> _logger;

    public ContourToolpathPlanner(ILogger<ContourToolpathPlanner> logger) => _logger = logger;

    // ── Primary: plan from Cura wall paths ────────────────────────────────────

    public Task<ToolpathResult> PlanFromWallPathsAsync(
        WallPathsRequest request,
        CancellationToken cancellationToken = default)
    {
        return Task.FromResult(PlanFromWallPathsCore(request));
    }

    private ToolpathResult PlanFromWallPathsCore(WallPathsRequest request)
    {
        _logger.LogDebug(
            "PlanFromWallPaths Z={Z} mm  tool Ø{D} mm  nozzle Ø{N} mm  {Count} wall segments  {S} support segments  clearance={C} mm",
            request.ZHeightMm, request.ToolDiameterMm, request.NozzleDiameterMm,
            request.WallPaths.Count,
            request.SupportPaths?.Count ?? 0,
            request.SupportClearanceMm);

        if (request.WallPaths.Count == 0)
            return new ToolpathResult(string.Empty, true, [], []);

        // CRC offset: tool_radius + nozzle_radius.
        //
        // Sign is what keeps the cutter out of solid material:
        //   outer wall — the tool rides OUTSIDE the printed surface, so expand (+)
        //   inner wall — the tool rides INSIDE the cavity, so erode (−)
        //
        // Both branches previously used +, which offset a pocket OUTWARD, i.e. into
        // the part wall. A closed ring is built as a Polygon by BuildGeometry, so a
        // negative buffer erodes it as intended; an open (unclosed) segment buffers to
        // empty, which is then reported as ToolTooWide — the safe outcome, and the
        // case the earlier "flip the sign" change appears to have been reacting to.
        var toolRadius   = request.ToolDiameterMm   / 2.0;
        var nozzleRadius = request.NozzleDiameterMm / 2.0;
        var crcOffset    = request.IsOuterWall
            ?  (toolRadius + nozzleRadius)   // outer wall — expand outward
            : -(toolRadius + nozzleRadius);  // inner wall — erode inward

        var dx = request.MachineOffset.X;
        var dy = request.MachineOffset.Y;
        var dz = request.MachineOffset.Z;

        var zCut  = request.ZHeightMm + dz;
        // Retract above the part top (not the tip position) so the tool clears the printed layers.
        // SafeClearanceHeightMm is a RELATIVE clearance above the part top, not an absolute position.
        var retractRef = request.PartTopZMm ?? request.ZHeightMm;
        var zSafe = retractRef + dz + request.SafeClearanceHeightMm;

        // ── Build support forbidden zone (buffered union of all support paths) ──────
        // Each support segment is buffered by (tool_radius + clearance) so the tool
        // centre never gets closer than clearanceMm to any support boundary.
        Geometry? forbiddenZone = null;
        if (request.SupportPaths is { Count: > 0 })
        {
            var supportBuffer = toolRadius + request.SupportClearanceMm;
            foreach (var seg in request.SupportPaths)
            {
                if (seg.Count < 2) continue;
                var supportGeom = BuildGeometry(seg);
                if (supportGeom is null || supportGeom.IsEmpty) continue;
                try
                {
                    var buffered = supportGeom.Buffer(supportBuffer, 16);
                    forbiddenZone = forbiddenZone is null
                        ? buffered
                        : forbiddenZone.Union(buffered);
                }
                catch (Exception ex)
                {
                    _logger.LogWarning(ex, "Support buffer failed at Z={Z}, segment skipped", request.ZHeightMm);
                }
            }

            if (forbiddenZone is not null)
            {
                // Normalize the forbidden zone topology to reduce NTS exceptions during Difference.
                // Buffer(0) is the standard NTS technique for self-union / topology repair.
                try { forbiddenZone = forbiddenZone.Buffer(0); } catch { /* proceed unnormalized */ }
                _logger.LogDebug("Forbidden zone built: {Area:F1} mm² at Z={Z}", forbiddenZone.Area, request.ZHeightMm);
            }
        }

        var gcodeBuilder       = new StringBuilder();
        var allBounds          = new List<BoundingBox2D>();
        var unmachinableRegions = new List<UnmachinableRegion>();

        // Minimum area threshold below which a compensated geometry is considered degenerate
        const double MinAreaMm2 = 0.01;

        // Process each wall path segment independently
        foreach (var seg in request.WallPaths)
        {
            if (seg.Count < 2) continue;

            // Build geometry: try closed polygon first, fall back to open linestring
            var geom = BuildGeometry(seg);
            if (geom is null || geom.IsEmpty) continue;

            // Capture original bounding box for unmachinable region reporting
            var origEnv = geom.EnvelopeInternal;
            var segBounds = new BoundingBox2D(
                origEnv.MinX + dx, origEnv.MinY + dy,
                origEnv.MaxX + dx, origEnv.MaxY + dy);

            // Apply cutter-radius compensation via NTS buffer
            Geometry compensated;
            try
            {
                compensated = geom.Buffer(crcOffset, 16);
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "CRC buffer failed for segment at Z={Z}, skipping", request.ZHeightMm);
                continue;
            }

            // Normalize compensated geometry — fixes self-intersecting rings that Cura
            // sometimes produces, which cause NTS topology exceptions during Difference.
            try { compensated = compensated.Buffer(0); } catch { /* proceed unnormalized */ }

            // Detect ToolTooWide:
            // • Outer walls: CRC expands outward so compensated is always ≥ original — only flag if truly empty.
            // • Inner walls: CRC shrinks inward — also flag if area is negligible (pocket too small for tool).
            if (compensated is null || compensated.IsEmpty ||
                (!request.IsOuterWall && compensated.Area < MinAreaMm2))
            {
                _logger.LogDebug(
                    "Segment at Z={Z} too narrow for tool Ø{D} mm — ToolTooWide",
                    request.ZHeightMm, request.ToolDiameterMm);
                unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "ToolTooWide", segBounds));
                continue;
            }

            // Validate CRC direction:
            // Outer wall: buffer expands outward, so compensated area must be >= original polygon area.
            // If it shrank, the input geometry was self-intersecting in a way that caused Buffer() to collapse.
            // Inner wall: buffer shrinks inward, so compensated area must be <= original polygon area.
            // If it expanded, the offset direction flipped and the tool would enter solid material.
            if (geom is Polygon origPoly)
            {
                var origArea = origPoly.Area;
                if (request.IsOuterWall && compensated.Area < origArea - MinAreaMm2)
                {
                    _logger.LogWarning(
                        "Outer-wall CRC produced smaller geometry at Z={Z} " +
                        "(orig={Orig:F2} mm2 -> comp={Comp:F2} mm2) -- likely self-intersecting Cura path, skipping",
                        request.ZHeightMm, origArea, compensated.Area);
                    unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "InvalidCrcDirection", segBounds));
                    continue;
                }
                if (!request.IsOuterWall && compensated.Area > origArea + MinAreaMm2)
                {
                    _logger.LogWarning(
                        "Inner-wall CRC produced larger geometry at Z={Z} " +
                        "(orig={Orig:F2} mm2 -> comp={Comp:F2} mm2) -- offset direction invalid, tool would enter solid, skipping",
                        request.ZHeightMm, origArea, compensated.Area);
                    unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "InvalidCrcDirection", segBounds));
                    continue;
                }
            }

            // ── Build local forbidden zone for this segment ────────────────────────────
            // The forbidden zone starts as the buffered support-path union (if any).
            // ALWAYS add the printed wall polygon to the forbidden zone for outer walls.
            // This is the primary "never damage the part" safety net: any contour arc that
            // would enter the solid material gets clipped out regardless of whether support
            // paths are present. Previously this was gated on forbiddenZone != null, which
            // meant no protection when there were no support paths.
            Geometry? localForbidden = forbiddenZone;
            if (request.IsOuterWall && geom is Polygon)
            {
                try
                {
                    var geomNorm = geom.Buffer(0);
                    localForbidden = localForbidden is null
                        ? geomNorm
                        : localForbidden.Union(geomNorm);
                }
                catch (Exception ex)
                {
                    _logger.LogWarning(ex,
                        "Could not add part interior to forbidden zone at Z={Z}, proceeding without part-interior guard",
                        request.ZHeightMm);
                }
            }

            // ── Extract the milling contour(s) as 1-D line strings ────────────────────
            // CRITICAL: We clip the contour LINE (1D), NOT the compensated polygon (2D).
            //
            // Clipping a polygon against the forbidden zone and then following its exterior
            // ring is WRONG: when the forbidden zone bisects the polygon, NTS closes the
            // resulting polygon by routing the exterior ring through the part interior —
            // i.e., the tool would mill INTO the printed material on the other side.
            //
            // The correct approach: extract the exterior ring as a LineString, then
            // LineString.Difference(forbiddenZone) → MultiLineString of accessible arcs.
            // Each arc is emitted as a separate G-code block with a safe-height lift
            // between arcs. The tool simply skips the blocked section instead of routing
            // around it through solid material.
            var contourLines = ExtractContourLines(compensated);
            if (contourLines.Count == 0) continue;

            var env2 = compensated.EnvelopeInternal;
            var compBounds = new BoundingBox2D(env2.MinX + dx, env2.MinY + dy, env2.MaxX + dx, env2.MaxY + dy);

            bool anyMachined = false;

            foreach (var contourLine in contourLines)
            {
                List<List<(double X, double Y)>> millingArcs;

                if (localForbidden is not null)
                {
                    // Clip the 1-D contour ring against the forbidden zone.
                    // Result: the arcs of the contour that lie outside the forbidden zone.
                    // Each arc becomes its own G-code segment with a lift at each end.
                    Geometry? clipped;
                    try
                    {
                        clipped = contourLine.Difference(localForbidden);
                    }
                    catch (Exception ex)
                    {
                        // Fail closed: if topology operation fails, skip this ring.
                        _logger.LogWarning(ex,
                            "Contour Difference failed at Z={Z} — ring skipped (fail-closed)",
                            request.ZHeightMm);
                        unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "SupportBlocked", segBounds));
                        continue;
                    }

                    if (clipped is null || clipped.IsEmpty)
                    {
                        _logger.LogDebug(
                            "Contour ring at Z={Z} fully inside forbidden zone — SupportBlocked",
                            request.ZHeightMm);
                        unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "SupportBlocked", segBounds));
                        continue;
                    }

                    millingArcs = ExtractLineSegments(clipped);
                    _logger.LogDebug(
                        "Z={Z}: contour clipped into {N} arc(s) around support",
                        request.ZHeightMm, millingArcs.Count);
                }
                else
                {
                    // No forbidden zone — machine the full ring as one closed contour.
                    millingArcs = [contourLine.Coordinates.Select(c => (c.X, c.Y)).ToList()];
                }

                foreach (var arc in millingArcs)
                {
                    if (arc.Count < 2) continue;
                    anyMachined = true;

                    var gcode = BuildGCode(arc, zCut, zSafe,
                        request.FeedRateMmPerMin, request.SpindleRpm,
                        dx, dy, request.ClimbMilling,
                        request.SpindleDwellSec, request.LeadInRadiusMm,
                        request.EndOverlapMm, request.LeadInFeedFraction,
                        request.PassIndex);
                    gcodeBuilder.AppendLine(gcode);
                    allBounds.Add(compBounds);
                }
            }

            if (!anyMachined)
            {
                // All contour rings were completely blocked — report region as unmachinable.
                unmachinableRegions.Add(new UnmachinableRegion(request.ZHeightMm, "SupportBlocked", segBounds));
            }
        }

        var totalGCode = gcodeBuilder.ToString().Trim();
        return totalGCode.Length == 0
            ? new ToolpathResult(string.Empty, true, [], unmachinableRegions)
            : new ToolpathResult(totalGCode, false, allBounds, unmachinableRegions);
    }

    // ── Fallback: plan from STL cross-section ─────────────────────────────────

    public async Task<ToolpathResult> PlanContourAsync(
        ToolpathRequest request,
        CancellationToken cancellationToken = default)
    {
        _logger.LogDebug("PlanContour (STL fallback) Z={Z} mm, tool Ø{D} mm",
            request.ZHeightMm, request.ToolDiameterMm);

        var polygon = await Task.Run(
            () => ExtractContourAtZ(request.StlFilePath, request.ZHeightMm),
            cancellationToken);

        if (polygon is null || polygon.IsEmpty)
        {
            _logger.LogDebug("No STL geometry at Z={Z}", request.ZHeightMm);
            return new ToolpathResult(string.Empty, true, [], []);
        }

        var offsetDist  = request.ToolDiameterMm / 2.0;
        var compensated = (Polygon)polygon.Buffer(offsetDist, 16);

        var dx = request.MachineOffset.X;
        var dy = request.MachineOffset.Y;
        var dz = request.MachineOffset.Z;

        var gcode = BuildGCode(
            compensated.ExteriorRing.Coordinates.Select(c => (c.X, c.Y)).ToList(),
            request.ZHeightMm + dz,
            request.SafeClearanceHeightMm + request.ZHeightMm + dz,
            request.FeedRateMmPerMin, request.SpindleRpm,
            dx, dy, request.ClimbMilling);

        var env    = compensated.EnvelopeInternal;
        var bounds = new BoundingBox2D(env.MinX + dx, env.MinY + dy, env.MaxX + dx, env.MaxY + dy);

        return new ToolpathResult(gcode, false, [bounds], []);
    }

    // ── Geometry helpers ──────────────────────────────────────────────────────

    /// <summary>
    /// Builds a NTS Geometry from a list of XY points.
    /// If the path is (approximately) closed it becomes a Polygon,
    /// otherwise a LineString.
    /// </summary>
    private static Geometry? BuildGeometry(IReadOnlyList<(double X, double Y)> points)
    {
        const double closeTolerance = 0.5; // mm

        var coords = points.Select(p => new Coordinate(p.X, p.Y)).ToList();

        // Check if the path closes on itself
        var first = coords[0];
        var last  = coords[^1];
        var dist  = Math.Sqrt(Math.Pow(last.X - first.X, 2) + Math.Pow(last.Y - first.Y, 2));
        var isClosed = dist <= closeTolerance;

        if (isClosed)
        {
            // Ensure the ring is explicitly closed
            if (dist > 1e-9) coords.Add(new Coordinate(first.X, first.Y));
            if (coords.Count < 4) return null;  // Need ≥4 for a valid ring (3 unique + close)

            try
            {
                var ring = GF.CreateLinearRing(coords.ToArray());
                return GF.CreatePolygon(ring);
            }
            catch
            {
                // Fall back to convex hull if ring is self-intersecting
                var mp = GF.CreateMultiPointFromCoords(coords.ToArray());
                return mp.ConvexHull();
            }
        }
        else
        {
            // Open path — buffer the LineString to get a ribbon
            if (coords.Count < 2) return null;
            return GF.CreateLineString(coords.ToArray());
        }
    }

    /// <summary>
    /// Extracts the exterior ring of every polygon in a geometry as a LinearRing.
    /// These are the 1-D milling contours — one closed loop per polygon face.
    /// </summary>
    private static List<LinearRing> ExtractContourLines(Geometry geom)
    {
        var result = new List<LinearRing>();
        void AddPoly(Polygon poly)
        {
            if (poly.ExteriorRing is LinearRing lr && lr.NumPoints >= 4)
                result.Add(lr);
        }
        if (geom is Polygon p)            { AddPoly(p); }
        else if (geom is MultiPolygon mp) { foreach (var g in mp.Geometries.OfType<Polygon>()) AddPoly(g); }
        else if (geom is GeometryCollection gc) { foreach (var g in gc.Geometries.OfType<Polygon>()) AddPoly(g); }
        return result;
    }

    /// <summary>
    /// Extracts all LineString components from a geometry (result of LineString.Difference).
    /// Each component represents an arc of the milling contour that is outside the forbidden zone.
    /// </summary>
    private static List<List<(double X, double Y)>> ExtractLineSegments(Geometry geom)
    {
        var result = new List<List<(double X, double Y)>>();
        void AddLine(LineString ls)
        {
            var pts = ls.Coordinates.Select(c => (c.X, c.Y)).ToList();
            if (pts.Count >= 2) result.Add(pts);
        }
        if      (geom is LinearRing lr)      { AddLine(lr); }
        else if (geom is LineString ls)      { AddLine(ls); }
        else if (geom is MultiLineString mls){ foreach (var g in mls.Geometries.OfType<LineString>()) AddLine(g); }
        else if (geom is GeometryCollection gc)
        {
            foreach (var g in gc.Geometries)
            {
                if      (g is MultiLineString ml) foreach (var g2 in ml.Geometries.OfType<LineString>()) AddLine(g2);
                else if (g is LineString l)       AddLine(l);
            }
        }
        return result;
    }

    /// <summary>
    /// Correct plane–triangle intersection:
    /// Finds the two edges of a triangle that straddle Z=zHeight and
    /// returns the interpolated XY intersection point on each edge as a segment.
    /// </summary>
    private static Polygon? ExtractContourAtZ(string stlPath, double zHeight)
    {
        var segments = new List<(double x1, double y1, double x2, double y2)>();

        using var fs     = new FileStream(stlPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var reader = new BinaryReader(fs);

        var header        = reader.ReadBytes(80);
        var triangleCount = reader.ReadUInt32();

        var expectedSize = 80L + 4L + triangleCount * 50L;
        if (fs.Length != expectedSize || triangleCount == 0) return null;

        for (var i = 0; i < triangleCount; i++)
        {
            // Skip normal
            reader.ReadSingle(); reader.ReadSingle(); reader.ReadSingle();

            double[] vx = new double[3], vy = new double[3], vz = new double[3];
            for (var v = 0; v < 3; v++)
            {
                vx[v] = reader.ReadSingle();
                vy[v] = reader.ReadSingle();
                vz[v] = reader.ReadSingle();
            }
            reader.ReadUInt16(); // attribute byte count

            // Find the two edges that straddle Z=zHeight (independently, not from a shared vertex)
            var crossings = new List<(double x, double y)>();
            for (var a = 0; a < 3; a++)
            {
                var b  = (a + 1) % 3;
                var za = vz[a]; var zb = vz[b];

                // Edge straddles the plane if one vertex is at or below and the other above
                var aboveA = za > zHeight;
                var aboveB = zb > zHeight;
                if (aboveA == aboveB) continue;   // same side — no crossing

                if (Math.Abs(za - zb) < 1e-12) continue;
                var t  = (zHeight - za) / (zb - za);
                var xi = vx[a] + t * (vx[b] - vx[a]);
                var yi = vy[a] + t * (vy[b] - vy[a]);
                crossings.Add((xi, yi));
            }

            // A plane through a triangle gives exactly 0 or 2 crossings
            if (crossings.Count == 2)
                segments.Add((crossings[0].x, crossings[0].y,
                              crossings[1].x, crossings[1].y));
        }

        if (segments.Count == 0) return null;

        var coords = segments
            .SelectMany(s => new[] { new Coordinate(s.x1, s.y1), new Coordinate(s.x2, s.y2) })
            .Distinct()
            .ToArray();

        if (coords.Length < 3) return null;

        var hull = (Polygon)GF.CreateMultiPointFromCoords(coords).ConvexHull();
        return hull.IsValid ? hull : null;
    }

    // ── Arc linearization ─────────────────────────────────────────────────────
    // Converts an arc into short G1 line segments. Used instead of G2/G3 because
    // RepRapFirmware does not support arcs on remapped axes (X/V plane).

    private static void LinearizeArc(
        StringBuilder sb,
        double startX, double startY,
        double endX, double endY,
        double centerX, double centerY,
        bool clockwise,
        double dx, double dy,
        int segments = 12)
    {
        var r = Math.Sqrt((startX - centerX) * (startX - centerX) +
                          (startY - centerY) * (startY - centerY));
        if (r < 0.001) { sb.AppendLine($"G1 X{endX + dx:F3} Y{endY + dy:F3}"); return; }

        var startAngle = Math.Atan2(startY - centerY, startX - centerX);
        var endAngle   = Math.Atan2(endY - centerY,   endX - centerX);

        // Adjust sweep direction
        var sweep = endAngle - startAngle;
        if (clockwise)
        {
            if (sweep > 0) sweep -= 2 * Math.PI;
            if (sweep > -0.001) sweep = -2 * Math.PI; // full circle fallback
        }
        else
        {
            if (sweep < 0) sweep += 2 * Math.PI;
            if (sweep < 0.001) sweep = 2 * Math.PI;
        }

        for (int i = 1; i <= segments; i++)
        {
            var t = startAngle + sweep * i / segments;
            var px = centerX + r * Math.Cos(t);
            var py = centerY + r * Math.Sin(t);
            sb.AppendLine($"G1 X{px + dx:F3} Y{py + dy:F3}");
        }
        // Ensure we end exactly at the target
        sb.AppendLine($"G1 X{endX + dx:F3} Y{endY + dy:F3}");
    }

    // ── G-code builder ────────────────────────────────────────────────────────

    private static string BuildGCode(
        IList<(double X, double Y)> coords,
        double zCut, double zSafe,
        double feed, int rpm,
        double dx, double dy,
        bool climb,
        double dwellSec = 3.0,
        double leadInRadius = 2.0,
        double endOverlap = 3.0,
        double leadInFeedFrac = 0.5,
        int passIndex = 0)
    {
        if (coords.Count < 2) return string.Empty;
        // DEBUG MARKER: if this line appears in output, the new BuildGCode is running

        var pts = climb ? coords.ToList() : coords.Reverse().ToList();

        // ── Rotate start point by passIndex to avoid stacking marks ──────────
        if (passIndex > 0 && pts.Count > 2)
        {
            // Compute total contour length, then offset by passIndex * 10mm
            var lengths = new double[pts.Count];
            double totalLen = 0;
            for (int i = 1; i < pts.Count; i++)
            {
                var segDx = pts[i].X - pts[i - 1].X;
                var segDy = pts[i].Y - pts[i - 1].Y;
                totalLen += Math.Sqrt(segDx * segDx + segDy * segDy);
                lengths[i] = totalLen;
            }
            var shiftDist = (passIndex * 10.0) % totalLen;
            if (shiftDist > 1.0)
            {
                // Find the vertex closest to shiftDist along the contour
                var bestIdx = 1;
                for (int i = 1; i < pts.Count; i++)
                {
                    if (lengths[i] >= shiftDist) { bestIdx = i; break; }
                }
                // Rotate the list so bestIdx becomes the start
                var rotated = new List<(double X, double Y)>();
                for (int i = bestIdx; i < pts.Count; i++) rotated.Add(pts[i]);
                for (int i = 1; i <= bestIdx; i++) rotated.Add(pts[i]); // skip [0] since it == [last]
                pts = rotated;
            }
        }

        // Ensure the contour is closed (last point == first)
        var first = pts[0];
        var last = pts[^1];
        var isClosed = Math.Abs(last.X - first.X) < 0.01 && Math.Abs(last.Y - first.Y) < 0.01;

        // ── Compute outward normal at the start point ────────────────────────
        // Direction from start to next point → tangent; rotate 90° outward
        var tangX = pts[1].X - pts[0].X;
        var tangY = pts[1].Y - pts[0].Y;
        var tangLen = Math.Sqrt(tangX * tangX + tangY * tangY);
        if (tangLen < 0.001) tangLen = 1;
        tangX /= tangLen; tangY /= tangLen;
        // Outward normal: for climb milling (CW around outer), outward is to the LEFT of travel
        // For conventional (CCW), outward is to the RIGHT
        var normX = climb ? -tangY : tangY;
        var normY = climb ? tangX : -tangX;

        // Lead-in start: offset from contour start along outward normal
        var leadClearance = 0.5; // mm extra beyond the lead-in radius
        var offDist = leadInRadius + leadClearance;
        var leadStartX = pts[0].X + normX * offDist;
        var leadStartY = pts[0].Y + normY * offDist;

        // Arc centre: midpoint between lead-in start and contour start
        var arcCX = pts[0].X + normX * (leadInRadius / 2.0);
        var arcCY = pts[0].Y + normY * (leadInRadius / 2.0);
        // I/J: offset from lead-in start to arc centre
        var leadI = arcCX - leadStartX;
        var leadJ = arcCY - leadStartY;
        var leadFeed = (int)(feed * leadInFeedFrac);

        var sb = new StringBuilder();
        sb.AppendLine($"; === Contour Milling Z={zCut:F3}  retract={zSafe:F3} lead-in={leadInRadius:F1} overlap={endOverlap:F1} ===");

        // A. Spindle start + dwell
        sb.AppendLine($"M3 S{rpm}");
        if (dwellSec > 0)
            sb.AppendLine($"G4 S{dwellSec:F1} ; spindle spin-up dwell {dwellSec:F1} s");

        // B. Lead-in: rapid to off-part position, plunge in free air, arc onto wall
        sb.AppendLine($"G0 Z{zSafe:F3} F6000 ; retract to safe height");
        sb.AppendLine($"; lead-in start (off part) X{leadStartX + dx:F3} Y{leadStartY + dy:F3}");
        sb.AppendLine($"G0 X{leadStartX + dx:F3} Y{leadStartY + dy:F3} F6000 ; travel to lead-in (off part)");
        sb.AppendLine($"; plunge in air to Z{zCut:F3}");
        sb.AppendLine($"G1 Z{zCut:F3} F{feed * 0.3:F0} ; plunge in free air");
        sb.AppendLine($"; lead-in arc (linearized — G2/G3 not supported on remapped axes)");
        sb.AppendLine($"G1 F{leadFeed}");
        LinearizeArc(sb, leadStartX, leadStartY, pts[0].X, pts[0].Y,
            arcCX, arcCY, climb, dx, dy);

        // Contour
        sb.AppendLine($"; contour");
        sb.AppendLine($"G1 F{feed:F0}");
        foreach (var pt in pts.Skip(1))
            sb.AppendLine($"G1 X{pt.X + dx:F3} Y{pt.Y + dy:F3}");

        // C. End overlap: continue past the start point
        if (endOverlap > 0 && isClosed)
        {
            sb.AppendLine($"; end overlap {endOverlap:F1} mm");
            double overlapDist = 0;
            for (int i = 1; i < pts.Count && overlapDist < endOverlap; i++)
            {
                var segDx2 = pts[i].X - pts[i - 1].X;
                var segDy2 = pts[i].Y - pts[i - 1].Y;
                var segLen2 = Math.Sqrt(segDx2 * segDx2 + segDy2 * segDy2);
                overlapDist += segLen2;
                sb.AppendLine($"G1 X{pts[i].X + dx:F3} Y{pts[i].Y + dy:F3}");
            }
        }

        // Lead-out: arc away from wall, then retract in free air
        // Compute outward normal at the current position (end of overlap or end of contour)
        var exitPt = isClosed && endOverlap > 0
            ? pts[Math.Min((int)(endOverlap / (tangLen > 0.001 ? tangLen : 1)) + 1, pts.Count - 1)]
            : pts[^1];
        // Use the same outward offset for lead-out
        var leadOutX = exitPt.X + normX * offDist;
        var leadOutY = exitPt.Y + normY * offDist;
        var outI = (exitPt.X + normX * (leadInRadius / 2.0)) - exitPt.X;
        var outJ = (exitPt.Y + normY * (leadInRadius / 2.0)) - exitPt.Y;

        sb.AppendLine($"; lead-out arc (linearized)");
        sb.AppendLine($"G1 F{leadFeed}");
        {
            var outCX = exitPt.X + normX * (leadInRadius / 2.0);
            var outCY = exitPt.Y + normY * (leadInRadius / 2.0);
            LinearizeArc(sb, exitPt.X, exitPt.Y, leadOutX, leadOutY,
                outCX, outCY, !climb, dx, dy); // opposite direction for lead-out
        }
        sb.AppendLine($"; retract (clear of part)");
        sb.AppendLine($"G0 Z{zSafe:F3} F6000 ; retract");
        sb.AppendLine("M5");
        sb.AppendLine($"; === End Contour Z={zCut:F3} ===");
        return sb.ToString();
    }
}
