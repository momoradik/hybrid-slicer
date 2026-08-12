using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/custom-gcode-blocks")]
public sealed class CustomGCodeController : ControllerBase
{
    private readonly ICustomGCodeBlockRepository _repo;

    public CustomGCodeController(ICustomGCodeBlockRepository repo) => _repo = repo;

    /// <summary>
    /// All blocks, or — when <paramref name="machineProfileId"/> is supplied — the
    /// blocks that apply to that machine (its own plus the shared ones).
    /// </summary>
    [HttpGet]
    public async Task<IActionResult> GetAll([FromQuery] Guid? machineProfileId, CancellationToken ct)
        => Ok(machineProfileId is null
            ? await _repo.GetAllAsync(ct)
            : await _repo.GetForMachineAsync(machineProfileId, enabledOnly: false, ct));

    [HttpGet("{id:guid}")]
    public async Task<IActionResult> GetById(Guid id, CancellationToken ct)
    {
        var block = await _repo.GetByIdAsync(id, ct);
        return block is null ? NotFound() : Ok(block);
    }

    [HttpPost]
    public async Task<IActionResult> Create([FromBody] CreateGCodeBlockRequest req, CancellationToken ct)
    {
        var block = CustomGCodeBlock.Create(
            req.Name, req.GCodeContent, req.Trigger, req.SortOrder,
            req.MachineProfileId, req.RepeatEveryNLayers, req.StartLayer, req.EndLayer, req.Description);

        await _repo.AddAsync(block, ct);
        return CreatedAtAction(nameof(GetById), new { id = block.Id }, block);
    }

    [HttpPut("{id:guid}")]
    public async Task<IActionResult> Update(Guid id, [FromBody] UpdateGCodeBlockRequest req, CancellationToken ct)
    {
        var block = await _repo.GetByIdAsync(id, ct);
        if (block is null) return NotFound();

        block.Update(
            req.Name, req.GCodeContent, req.Trigger, req.Description, req.SortOrder,
            req.MachineProfileId, req.RepeatEveryNLayers, req.StartLayer, req.EndLayer);

        await _repo.UpdateAsync(block, ct);
        return Ok(block);
    }

    [HttpPatch("{id:guid}/toggle")]
    public async Task<IActionResult> Toggle(Guid id, [FromBody] ToggleRequest req, CancellationToken ct)
    {
        var block = await _repo.GetByIdAsync(id, ct);
        if (block is null) return NotFound();
        if (req.Enabled) block.Enable(); else block.Disable();
        await _repo.UpdateAsync(block, ct);
        return Ok(block);
    }

    [HttpDelete("{id:guid}")]
    public async Task<IActionResult> Delete(Guid id, CancellationToken ct)
    {
        await _repo.DeleteAsync(id, ct);
        return NoContent();
    }
}

public record CreateGCodeBlockRequest(
    string Name,
    string GCodeContent,
    GCodeTrigger Trigger,
    int SortOrder = 0,
    Guid? MachineProfileId = null,
    int RepeatEveryNLayers = 0,
    int StartLayer = 1,
    int? EndLayer = null,
    string? Description = null);

public record UpdateGCodeBlockRequest(
    string Name,
    string GCodeContent,
    GCodeTrigger Trigger,
    string? Description,
    int SortOrder,
    Guid? MachineProfileId = null,
    int RepeatEveryNLayers = 0,
    int StartLayer = 1,
    int? EndLayer = null);

public record ToggleRequest(bool Enabled);
