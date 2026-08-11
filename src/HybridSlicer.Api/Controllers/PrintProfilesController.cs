using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/print-profiles")]
public sealed class PrintProfilesController : ControllerBase
{
    private readonly IPrintProfileRepository _repo;

    public PrintProfilesController(IPrintProfileRepository repo) => _repo = repo;

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
    public async Task<IActionResult> Create([FromBody] CreatePrintProfileRequest req, CancellationToken ct)
    {
        var profile = PrintProfile.Create(req.Name)
            .WithLayerHeight(req.LayerHeightMm)
            .WithSpeeds(req.PrintSpeedMmS, req.TravelSpeedMmS,
                        req.InfillSpeedMmS, req.WallSpeedMmS, req.FirstLayerSpeedMmS)
            .WithTemperatures(req.PrintTemperatureDegC, req.BedTemperatureDegC)
            .WithInfill(req.InfillDensityPct, req.InfillPattern)
            .WithSupport(req.SupportEnabled, req.SupportType, req.SupportOverhangAngleDeg)
            .WithFlow(req.MaterialFlowPct)
            .WithNozzleDiameter(req.NozzleDiameterMm)
            .WithInnerWallSpeed(req.InnerWallSpeedMmS)
            .WithTopBottomSpeed(req.TopBottomSpeedMmS)
            .WithRetraction(req.RetractionEnabled, req.RetractLengthMm, req.RetractSpeedMmS, req.RetractMinTravelMm)
            .WithTopBottomLayers(req.TopLayers, req.BottomLayers)
            .WithCoolingLimits(req.MinLayerTimeSec, req.MinSpeedMmS)
            .WithMotionControl(req.AccelerationControlEnabled, req.JerkControlEnabled)
            .WithSkinMonotonic(req.SkinMonotonic)
            .WithFirstLayerTravelSpeed(req.FirstLayerTravelSpeedMmS)
            .WithPelletMode(req.PelletModeEnabled, req.VirtualFilamentDiameterMm);

        await _repo.AddAsync(profile, ct);
        return CreatedAtAction(nameof(GetById), new { id = profile.Id }, profile);
    }

    [HttpPut("{id:guid}")]
    public async Task<IActionResult> Update(
        Guid id, [FromBody] CreatePrintProfileRequest req, CancellationToken ct)
    {
        var profile = await _repo.GetByIdAsync(id, ct);
        if (profile is null) return NotFound();

        profile
            .WithLayerHeight(req.LayerHeightMm)
            .WithSpeeds(req.PrintSpeedMmS, req.TravelSpeedMmS,
                        req.InfillSpeedMmS, req.WallSpeedMmS, req.FirstLayerSpeedMmS)
            .WithTemperatures(req.PrintTemperatureDegC, req.BedTemperatureDegC)
            .WithInfill(req.InfillDensityPct, req.InfillPattern)
            .WithSupport(req.SupportEnabled, req.SupportType, req.SupportOverhangAngleDeg)
            .WithFlow(req.MaterialFlowPct)
            .WithNozzleDiameter(req.NozzleDiameterMm)
            .WithInnerWallSpeed(req.InnerWallSpeedMmS)
            .WithTopBottomSpeed(req.TopBottomSpeedMmS)
            .WithRetraction(req.RetractionEnabled, req.RetractLengthMm, req.RetractSpeedMmS, req.RetractMinTravelMm)
            .WithTopBottomLayers(req.TopLayers, req.BottomLayers)
            .WithCoolingLimits(req.MinLayerTimeSec, req.MinSpeedMmS)
            .WithMotionControl(req.AccelerationControlEnabled, req.JerkControlEnabled)
            .WithSkinMonotonic(req.SkinMonotonic)
            .WithFirstLayerTravelSpeed(req.FirstLayerTravelSpeedMmS)
            .WithPelletMode(req.PelletModeEnabled, req.VirtualFilamentDiameterMm);

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

public record CreatePrintProfileRequest(
    string Name,
    double LayerHeightMm          = 0.2,
    double LineWidthMm            = 0.4,
    int    WallCount              = 3,
    int    TopBottomLayers        = 4,
    int    TopLayers              = 4,
    int    BottomLayers           = 4,
    double PrintSpeedMmS          = 50,
    double TravelSpeedMmS         = 150,
    double InfillSpeedMmS         = 70,
    double WallSpeedMmS           = 30,
    double FirstLayerSpeedMmS     = 20,
    double InfillDensityPct       = 20,
    string InfillPattern          = "grid",
    int    PrintTemperatureDegC   = 210,
    int    BedTemperatureDegC     = 60,
    bool   RetractionEnabled       = true,
    double RetractLengthMm        = 5,
    double RetractSpeedMmS        = 45,
    bool   SupportEnabled         = false,
    string SupportType            = "normal",
    double SupportOverhangAngleDeg = 50,
    bool   CoolingEnabled         = true,
    int    CoolingFanSpeedPct     = 100,
    double FilamentDiameterMm     = 1.75,
    double MaterialFlowPct        = 100.0,
    double NozzleDiameterMm             = 0.0,
    double InnerWallSpeedMmS            = 60.0,
    double TopBottomSpeedMmS            = 30.0,
    double RetractMinTravelMm           = 1.5,
    double MinLayerTimeSec              = 5.0,
    double MinSpeedMmS                  = 10.0,
    bool   AccelerationControlEnabled   = false,
    bool   JerkControlEnabled           = false,
    bool   SkinMonotonic                = true,
    double FirstLayerTravelSpeedMmS     = 50.0,
    bool   PelletModeEnabled            = false,
    double VirtualFilamentDiameterMm    = 1.0);
