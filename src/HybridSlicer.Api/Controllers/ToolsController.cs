using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/tools")]
public sealed class ToolsController : ControllerBase
{
    private readonly ICncToolRepository _repo;

    public ToolsController(ICncToolRepository repo) => _repo = repo;

    [HttpGet]
    public async Task<IActionResult> GetAll(CancellationToken ct) => Ok(await _repo.GetAllAsync(ct));

    [HttpGet("{id:guid}")]
    public async Task<IActionResult> GetById(Guid id, CancellationToken ct)
    {
        var tool = await _repo.GetByIdAsync(id, ct);
        return tool is null ? NotFound() : Ok(tool);
    }

    [HttpPost]
    public async Task<IActionResult> Create([FromBody] CreateToolRequest req, CancellationToken ct)
    {
        var tool = CncTool.Create(
            req.Name, req.Type, req.DiameterMm, req.FluteLengthMm, req.ShankDiameterMm,
            req.FluteCount, req.ToolMaterial, req.MaxDepthOfCutMm,
            req.RecommendedRpm, req.RecommendedFeedMmPerMin, req.ToolLengthMm);
        await _repo.AddAsync(tool, ct);
        return CreatedAtAction(nameof(GetById), new { id = tool.Id }, tool);
    }

    [HttpPut("{id:guid}")]
    public async Task<IActionResult> Update(Guid id, [FromBody] CreateToolRequest req, CancellationToken ct)
    {
        var tool = await _repo.GetByIdAsync(id, ct);
        if (tool is null) return NotFound();
        tool.Update(
            req.Name, req.Type, req.DiameterMm, req.FluteLengthMm, req.ShankDiameterMm,
            req.FluteCount, req.ToolMaterial, req.MaxDepthOfCutMm,
            req.RecommendedRpm, req.RecommendedFeedMmPerMin, req.ToolLengthMm);
        await _repo.UpdateAsync(tool, ct);
        return Ok(tool);
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id, CancellationToken ct)
    {
        var tool = await _repo.GetByIdAsync(id, ct);
        if (tool is null) return NotFound();
        tool.SoftDelete();
        await _repo.UpdateAsync(tool, ct);
        return NoContent();
    }
}

public record CreateToolRequest(
    string Name,
    ToolType Type,
    double DiameterMm,
    double FluteLengthMm,
    double ShankDiameterMm,
    int FluteCount = 2,
    string ToolMaterial = "HSS",
    double MaxDepthOfCutMm = 0,
    int RecommendedRpm = 10000,
    double RecommendedFeedMmPerMin = 500,
    double ToolLengthMm = 50.0);

public record UpdateCuttingParamsRequest(int RecommendedRpm, double FeedMmPerMin, double MaxDocMm);
