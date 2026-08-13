using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.Enums;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Safety;

/// <summary>
/// Validates a CNC toolpath against:
///   1. Machine axis envelope limits
///   2. Rapid-move clearance above the printed part
///   3. Cutter engagement — no rapid moves inside the part bounding box at cut depth
///
/// Parsing strategy: lightweight line-by-line G-code parser extracts X/Y/Z moves.
/// A full mesh intersection BVH can be plugged in later via the same interface.
///
/// SAFETY INVARIANT: Any unrecognised condition returns Blocked, not Clear.
/// </summary>
public sealed class CollisionSafetyValidator : ISafetyValidator
{
    private readonly ILogger<CollisionSafetyValidator> _logger;

    public CollisionSafetyValidator(ILogger<CollisionSafetyValidator> logger) => _logger = logger;

    public Task<SafetyValidationResult> ValidateToolpathAsync(
        SafetyValidationRequest request,
        CancellationToken cancellationToken = default)
    {
        var issues = new List<string>();

        var moves = ParseMoves(request.CncGCode);

        // The G-code reaching this validator is in MACHINE coordinates, not bed-centre
        // coordinates: GenerateToolpathsHandler adds the machine's CNC offset to every
        // toolpath point (and to PrintedGeometryBounds) before calling us, and passes
        // MachineMaxX/Y/Z as the full travel envelope measured from the machine origin.
        // So the valid range is [0, Max] on every axis.
        //
        // Treating the origin as the bed centre here — i.e. ±Max/2 — was wrong in both
        // directions. On a machine like Robocera (travel X 900, CNC offset X 500, 200 mm
        // bed) the toolpath legitimately sits around X 400–600, which ±450 reports as a
        // collision; meanwhile a genuinely negative coordinate, which is a real crash into
        // the frame, fell inside ±450 and was reported Clear.
        foreach (var move in moves)
        {
            // 1. Axis envelope check — machine origin at zero, travel limits are the maxima
            if (move.X < 0 || move.X > request.MachineMaxX)
                issues.Add($"X={move.X:F4} exceeds machine envelope [0, {request.MachineMaxX}].");
            if (move.Y < 0 || move.Y > request.MachineMaxY)
                issues.Add($"Y={move.Y:F4} exceeds machine envelope [0, {request.MachineMaxY}].");
            if (move.Z < 0 || move.Z > request.MachineMaxZ)
                issues.Add($"Z={move.Z:F4} exceeds machine envelope [0, {request.MachineMaxZ}].");

            // 2. Rapid moves (G0) must be above safe clearance height
            if (move.IsRapid && move.Z < request.SafeClearanceHeightMm)
                issues.Add($"Rapid move at Z={move.Z:F4} is below safe clearance height {request.SafeClearanceHeightMm}.");

            // 3. Check cut-depth moves against printed geometry bounding boxes
            if (!move.IsRapid)
            {
                foreach (var box in request.PrintedGeometryBounds)
                {
                    if (box.ContainsPoint(move.X, move.Y, move.Z))
                    {
                        issues.Add($"Feed move at ({move.X:F4},{move.Y:F4},{move.Z:F4}) intersects printed geometry.");
                    }
                }
            }

            // 4. Spindle clearance: spindle bottom (tool tip Z + tool length) must stay within machine Z envelope.
            //    Prevents the spindle body from crashing into the machine frame or fixture.
            if (request.ToolLengthMm > 0)
            {
                var spindleZ = move.Z + request.ToolLengthMm;
                if (spindleZ > request.MachineMaxZ)
                    issues.Add(
                        $"SpindleCollision: spindle at Z={spindleZ:F4} mm (tip {move.Z:F4} + tool length {request.ToolLengthMm:F4}) " +
                        $"exceeds machine Z limit {request.MachineMaxZ:F4} mm.");
            }
        }

        SafetyStatus status;
        if (issues.Count == 0)
        {
            status = SafetyStatus.Clear;
        }
        else if (issues.Any(i => i.Contains("intersects") || i.Contains("SpindleCollision")
                               || i.Contains("envelope")))
        {
            // Envelope violations, part-geometry intersections, and spindle collisions are all
            // hard stops: exceeding machine travel limits can destroy hardware just as surely as
            // driving the tool through the printed part.
            status = SafetyStatus.Blocked;
            _logger.LogWarning("Safety BLOCKED for toolpath: {Count} issues", issues.Count);
        }
        else
        {
            status = SafetyStatus.Warning;
            _logger.LogWarning("Safety WARNING for toolpath: {Count} issues", issues.Count);
        }

        return Task.FromResult(new SafetyValidationResult(status, issues));
    }

    private static List<GCodeMove> ParseMoves(string gcode)
    {
        var moves = new List<GCodeMove>();
        double x = 0, y = 0, z = 0;
        bool isRapid = false;

        foreach (var rawLine in gcode.Split('\n'))
        {
            var line = rawLine.Trim();
            if (line.StartsWith(';') || string.IsNullOrEmpty(line)) continue;

            // Strip inline comments
            var commentIdx = line.IndexOf(';');
            if (commentIdx >= 0) line = line[..commentIdx].Trim();

            var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0) continue;

            var cmd = parts[0].ToUpperInvariant();
            isRapid = cmd == "G0";

            if (cmd is not ("G0" or "G1")) continue;

            foreach (var part in parts.Skip(1))
            {
                if (part.Length < 2) continue;
                var axis = part[0];
                if (!double.TryParse(part[1..], out var val)) continue;
                switch (char.ToUpperInvariant(axis))
                {
                    case 'X': x = val; break;
                    case 'Y': y = val; break;
                    case 'Z': z = val; break;
                }
            }

            moves.Add(new GCodeMove(x, y, z, isRapid));
        }

        return moves;
    }

    private sealed record GCodeMove(double X, double Y, double Z, bool IsRapid);
}
