using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/materials")]
public sealed class MaterialsController : ControllerBase
{
    private readonly IMaterialRepository _repo;

    public MaterialsController(IMaterialRepository repo) => _repo = repo;

    [HttpGet]
    public async Task<IActionResult> GetAll(CancellationToken ct)
        => Ok(await _repo.GetAllAsync(ct));

    [HttpGet("{id:guid}")]
    public async Task<IActionResult> GetById(Guid id, CancellationToken ct)
    {
        var m = await _repo.GetByIdAsync(id, ct);
        return m is null ? NotFound() : Ok(m);
    }

    [HttpPost]
    public async Task<IActionResult> Create([FromBody] CreateMaterialRequest req, CancellationToken ct)
    {
        var material = Material.Create(
            req.Name, req.Type,
            req.PrintTempMin, req.PrintTempMax,
            req.BedTempMin, req.BedTempMax,
            req.Density, req.DiameterMm);

        await _repo.AddAsync(material, ct);
        return CreatedAtAction(nameof(GetById), new { id = material.Id }, material);
    }

    [HttpPut("{id:guid}")]
    public async Task<IActionResult> Update(
        Guid id, [FromBody] CreateMaterialRequest req, CancellationToken ct)
    {
        var m = await _repo.GetByIdAsync(id, ct);
        if (m is null) return NotFound();

        m.Update(
            req.Name, req.Type,
            req.PrintTempMin, req.PrintTempMax,
            req.BedTempMin, req.BedTempMax,
            req.Density, req.DiameterMm);

        await _repo.UpdateAsync(m, ct);
        return Ok(m);
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id, CancellationToken ct)
    {
        await _repo.DeleteAsync(id, ct);
        return NoContent();
    }
}

public record CreateMaterialRequest(
    string Name,
    string Type,
    int    PrintTempMin,
    int    PrintTempMax,
    int    BedTempMin     = 0,
    int    BedTempMax     = 0,
    double Density        = 1.24,
    double DiameterMm     = 1.75);
