using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.ValueObjects;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/machine-profiles")]
public sealed class MachineProfilesController : ControllerBase
{
    private readonly IMachineProfileRepository _repo;

    public MachineProfilesController(IMachineProfileRepository repo) => _repo = repo;

    [HttpGet]
    public async Task<IActionResult> GetAll(CancellationToken ct)
        => Ok(await _repo.GetAllAsync(ct));

    [HttpGet("{id:guid}")]
    public async Task<IActionResult> GetById(Guid id, CancellationToken ct)
    {
        var profile = await _repo.GetByIdAsync(id, ct);
        return profile is null ? NotFound() : Ok(profile);
    }

    [HttpPost]
    public async Task<IActionResult> Create([FromBody] CreateMachineProfileRequest req, CancellationToken ct)
    {
        // Prevent duplicate names
        var all = await _repo.GetAllAsync(ct);
        if (all.Any(m => string.Equals(m.Name, req.Name?.Trim(), StringComparison.OrdinalIgnoreCase)))
            return BadRequest($"A machine profile named '{req.Name?.Trim()}' already exists.");

        var profile = MachineProfile.Create(
            req.Name, req.Type,
            req.BedWidthMm, req.BedDepthMm, req.BedHeightMm,
            req.ExtruderCount);

        if (req.TravelXMm.HasValue && req.TravelYMm.HasValue && req.TravelZMm.HasValue)
            profile.SetTravel(req.TravelXMm.Value, req.TravelYMm.Value, req.TravelZMm.Value);

        if (req.OriginMode is not null && Enum.TryParse<OriginMode>(req.OriginMode, true, out var om))
            profile.SetOriginMode(om);

        if (req.BedPositionXMm.HasValue || req.BedPositionYMm.HasValue)
            profile.SetBedPosition(req.BedPositionXMm ?? 0, req.BedPositionYMm ?? 0);

        if (req.OriginXMm.HasValue || req.OriginYMm.HasValue)
            profile.SetOrigin(req.OriginXMm ?? 0, req.OriginYMm ?? 0);

        if (req.Beds is { Count: > 0 })
            profile.SetBeds(req.Beds.Select((b, i) =>
                new BedDefinition(i, b.WidthMm, b.DepthMm, b.HeightMm, b.PositionXMm, b.PositionYMm)).ToList());

        if (req.IpAddress is not null)
            profile.SetNetworkEndpoint(req.IpAddress, req.Port);

        if (req.NozzleXOffsets is not null)
            profile.SetNozzleXOffsets(req.NozzleXOffsets);
        if (req.NozzleYOffsets is not null)
            profile.SetNozzleYOffsets(req.NozzleYOffsets);

        profile.SetBedEdgeOffsets(req.LeftBedEdgeOffsetMm, req.RightBedEdgeOffsetMm,
            req.FrontBedEdgeOffsetMm, req.BackBedEdgeOffsetMm);

        if (req.ExtruderAssignments is not null)
            profile.SetExtruderAssignments(
                req.ExtruderAssignments.Select(a => new ExtruderAssignment(a.ExtruderIndex, a.Duty)).ToList());

        if (req.CncOffset is not null)
            profile.UpdateCncOffset(new Domain.ValueObjects.MachineOffset(
                req.CncOffset.X, req.CncOffset.Y, req.CncOffset.Z, req.CncOffset.RotationDeg));

        if (req.SafeClearanceHeightMm.HasValue)
            profile.SetSafeClearanceHeight(req.SafeClearanceHeightMm.Value);

        if (req.GCodeFlavor is not null) profile.SetGCodeFlavor(req.GCodeFlavor);
        if (req.ExtruderAxes is not null) profile.SetExtruderAxes(req.ExtruderAxes);
        if (req.CncAxes is not null) profile.SetCncAxes(req.CncAxes);
        if (req.MotionAssignmentEnabled.HasValue || req.MotionAssignmentJson is not null)
            profile.SetMotionAssignment(req.MotionAssignmentEnabled ?? false, req.MotionAssignmentJson ?? "{}");

        await _repo.AddAsync(profile, ct);
        return CreatedAtAction(nameof(GetById), new { id = profile.Id }, profile);
    }

    [HttpPut("{id:guid}")]
    public async Task<IActionResult> Update(Guid id, [FromBody] UpdateMachineProfileRequest req, CancellationToken ct)
    {
        var profile = await _repo.GetByIdAsync(id, ct);
        if (profile is null) return NotFound();

        if (req.Name is not null)
        {
            var trimmed = req.Name.Trim();
            if (!string.Equals(trimmed, profile.Name, StringComparison.OrdinalIgnoreCase))
            {
                var all = await _repo.GetAllAsync(ct);
                if (all.Any(m => m.Id != id && string.Equals(m.Name, trimmed, StringComparison.OrdinalIgnoreCase)))
                    return BadRequest($"A machine profile named '{trimmed}' already exists.");
            }
            profile.Rename(req.Name);
        }

        if (req.TravelXMm.HasValue || req.TravelYMm.HasValue || req.TravelZMm.HasValue)
            profile.SetTravel(
                req.TravelXMm ?? profile.TravelXMm,
                req.TravelYMm ?? profile.TravelYMm,
                req.TravelZMm ?? profile.TravelZMm);

        if (req.OriginMode is not null && Enum.TryParse<OriginMode>(req.OriginMode, true, out var om))
            profile.SetOriginMode(om);

        if (req.BedPositionXMm.HasValue || req.BedPositionYMm.HasValue)
            profile.SetBedPosition(
                req.BedPositionXMm ?? profile.BedPositionXMm,
                req.BedPositionYMm ?? profile.BedPositionYMm);

        if (req.OriginXMm.HasValue || req.OriginYMm.HasValue)
            profile.SetOrigin(
                req.OriginXMm ?? profile.OriginXMm,
                req.OriginYMm ?? profile.OriginYMm);

        if (req.BedWidthMm.HasValue || req.BedDepthMm.HasValue || req.BedHeightMm.HasValue)
            profile.UpdateBedDimensions(
                req.BedWidthMm ?? profile.BedWidthMm,
                req.BedDepthMm ?? profile.BedDepthMm,
                req.BedHeightMm ?? profile.BedHeightMm);

        if (req.Beds is { Count: > 0 })
            profile.SetBeds(req.Beds.Select((b, i) =>
                new BedDefinition(i, b.WidthMm, b.DepthMm, b.HeightMm, b.PositionXMm, b.PositionYMm)).ToList());

        if (req.ExtruderCount.HasValue)
            profile.SetExtruderCount(req.ExtruderCount.Value);

        if (req.NozzleXOffsets is not null)
            profile.SetNozzleXOffsets(req.NozzleXOffsets);
        if (req.NozzleYOffsets is not null)
            profile.SetNozzleYOffsets(req.NozzleYOffsets);

        if (req.LeftBedEdgeOffsetMm.HasValue || req.RightBedEdgeOffsetMm.HasValue
            || req.FrontBedEdgeOffsetMm.HasValue || req.BackBedEdgeOffsetMm.HasValue)
            profile.SetBedEdgeOffsets(
                req.LeftBedEdgeOffsetMm ?? profile.LeftBedEdgeOffsetMm,
                req.RightBedEdgeOffsetMm ?? profile.RightBedEdgeOffsetMm,
                req.FrontBedEdgeOffsetMm ?? profile.FrontBedEdgeOffsetMm,
                req.BackBedEdgeOffsetMm ?? profile.BackBedEdgeOffsetMm);

        if (req.ExtruderAssignments is not null)
            profile.SetExtruderAssignments(
                req.ExtruderAssignments.Select(a => new ExtruderAssignment(a.ExtruderIndex, a.Duty)).ToList());

        if (req.IpAddress is not null)
        {
            if (string.IsNullOrWhiteSpace(req.IpAddress))
                profile.ClearNetworkEndpoint();
            else
                profile.SetNetworkEndpoint(req.IpAddress, req.Port ?? profile.Port);
        }

        if (req.CncOffset is not null)
            profile.UpdateCncOffset(new MachineOffset(
                req.CncOffset.X, req.CncOffset.Y, req.CncOffset.Z, req.CncOffset.RotationDeg));

        if (req.SafeClearanceHeightMm.HasValue)
            profile.SetSafeClearanceHeight(req.SafeClearanceHeightMm.Value);

        if (req.GCodeFlavor is not null) profile.SetGCodeFlavor(req.GCodeFlavor);
        if (req.ExtruderAxes is not null) profile.SetExtruderAxes(req.ExtruderAxes);
        if (req.CncAxes is not null) profile.SetCncAxes(req.CncAxes);
        if (req.MotionAssignmentEnabled.HasValue || req.MotionAssignmentJson is not null)
            profile.SetMotionAssignment(req.MotionAssignmentEnabled ?? profile.MotionAssignmentEnabled, req.MotionAssignmentJson ?? profile.MotionAssignmentJson);

        await _repo.UpdateAsync(profile, ct);
        return Ok(profile);
    }

    [HttpPut("{id:guid}/offsets")]
    public async Task<IActionResult> UpdateOffsets(Guid id, [FromBody] UpdateOffsetsRequest req, CancellationToken ct)
    {
        var profile = await _repo.GetByIdAsync(id, ct);
        if (profile is null) return NotFound();

        profile.UpdateCncOffset(new MachineOffset(req.X, req.Y, req.Z, req.RotationDeg));

        foreach (var to in req.ToolOffsets)
            profile.UpsertToolOffset(new ToolOffset(to.ToolIndex, to.LengthOffsetMm, to.RadiusOffsetMm,
                to.OffsetX, to.OffsetY, to.OffsetZ, to.Description));

        await _repo.UpdateAsync(profile, ct);
        return Ok(profile);
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id, CancellationToken ct)
    {
        var profile = await _repo.GetByIdAsync(id, ct);
        if (profile is null) return NotFound();
        profile.SoftDelete();
        await _repo.UpdateAsync(profile, ct);
        return NoContent();
    }
}

public record CreateMachineProfileRequest(
    string Name,
    MachineType Type,
    double BedWidthMm,
    double BedDepthMm,
    double BedHeightMm,
    int ExtruderCount = 1,
    double? TravelXMm = null,
    double? TravelYMm = null,
    double? TravelZMm = null,
    string? OriginMode = null,
    double? BedPositionXMm = null,
    double? BedPositionYMm = null,
    double? OriginXMm = null,
    double? OriginYMm = null,
    IReadOnlyList<BedDto>? Beds = null,
    IReadOnlyList<double>? NozzleXOffsets = null,
    IReadOnlyList<double>? NozzleYOffsets = null,
    double LeftBedEdgeOffsetMm = 0,
    double RightBedEdgeOffsetMm = 0,
    double FrontBedEdgeOffsetMm = 0,
    double BackBedEdgeOffsetMm = 0,
    IReadOnlyList<ExtruderAssignmentDto>? ExtruderAssignments = null,
    string? IpAddress = null,
    int Port = 8080,
    OffsetDto? CncOffset = null,
    double? SafeClearanceHeightMm = null,
    string? GCodeFlavor = null,
    string? ExtruderAxes = null,
    string? CncAxes = null,
    bool? MotionAssignmentEnabled = null,
    string? MotionAssignmentJson = null);

public record UpdateMachineProfileRequest(
    string? Name = null,
    double? BedWidthMm = null,
    double? BedDepthMm = null,
    double? BedHeightMm = null,
    int? ExtruderCount = null,
    double? TravelXMm = null,
    double? TravelYMm = null,
    double? TravelZMm = null,
    string? OriginMode = null,
    double? BedPositionXMm = null,
    double? BedPositionYMm = null,
    double? OriginXMm = null,
    double? OriginYMm = null,
    IReadOnlyList<BedDto>? Beds = null,
    IReadOnlyList<double>? NozzleXOffsets = null,
    IReadOnlyList<double>? NozzleYOffsets = null,
    double? LeftBedEdgeOffsetMm = null,
    double? RightBedEdgeOffsetMm = null,
    double? FrontBedEdgeOffsetMm = null,
    double? BackBedEdgeOffsetMm = null,
    IReadOnlyList<ExtruderAssignmentDto>? ExtruderAssignments = null,
    string? IpAddress = null,
    int? Port = null,
    OffsetDto? CncOffset = null,
    double? SafeClearanceHeightMm = null,
    string? GCodeFlavor = null,
    string? ExtruderAxes = null,
    string? CncAxes = null,
    bool? MotionAssignmentEnabled = null,
    string? MotionAssignmentJson = null);

public record UpdateOffsetsRequest(
    double X, double Y, double Z, double RotationDeg,
    IReadOnlyList<ToolOffsetDto> ToolOffsets);

public record OffsetDto(double X, double Y, double Z, double RotationDeg = 0);
public record ToolOffsetDto(int ToolIndex, double LengthOffsetMm, double RadiusOffsetMm,
    double OffsetX = 0, double OffsetY = 0, double OffsetZ = 0, string? Description = null);
public record ExtruderAssignmentDto(int ExtruderIndex, string Duty);
public record BedDto(double WidthMm, double DepthMm, double HeightMm, double PositionXMm, double PositionYMm);
