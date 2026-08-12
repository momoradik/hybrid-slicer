using HybridSlicer.Application.Interfaces.Repositories;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.EntityFrameworkCore;

namespace HybridSlicer.Infrastructure.Persistence.Repositories;

public sealed class CustomGCodeBlockRepository : ICustomGCodeBlockRepository
{
    private readonly AppDbContext _db;

    public CustomGCodeBlockRepository(AppDbContext db) => _db = db;

    public Task<CustomGCodeBlock?> GetByIdAsync(Guid id, CancellationToken ct = default)
        => _db.CustomGCodeBlocks.FirstOrDefaultAsync(x => x.Id == id, ct);

    public async Task<IReadOnlyList<CustomGCodeBlock>> GetAllAsync(CancellationToken ct = default)
        => await _db.CustomGCodeBlocks.OrderBy(x => x.SortOrder).ToListAsync(ct);

    public async Task<IReadOnlyList<CustomGCodeBlock>> GetEnabledAsync(CancellationToken ct = default)
        => await _db.CustomGCodeBlocks.Where(x => x.IsEnabled).OrderBy(x => x.SortOrder).ToListAsync(ct);

    public async Task<IReadOnlyList<CustomGCodeBlock>> GetForMachineAsync(
        Guid? machineProfileId, bool enabledOnly, CancellationToken ct = default)
    {
        var q = _db.CustomGCodeBlocks.AsQueryable();

        // Shared blocks (null machine) always apply; machine-specific blocks only
        // when the caller names that machine.
        q = machineProfileId is null
            ? q.Where(x => x.MachineProfileId == null)
            : q.Where(x => x.MachineProfileId == null || x.MachineProfileId == machineProfileId);

        if (enabledOnly) q = q.Where(x => x.IsEnabled);

        return await q.OrderBy(x => x.SortOrder).ThenBy(x => x.Name).ToListAsync(ct);
    }

    public async Task<IReadOnlyList<CustomGCodeBlock>> GetByTriggerAsync(GCodeTrigger trigger, CancellationToken ct = default)
        => await _db.CustomGCodeBlocks.Where(x => x.IsEnabled && x.Trigger == trigger)
            .OrderBy(x => x.SortOrder).ToListAsync(ct);

    public async Task AddAsync(CustomGCodeBlock block, CancellationToken ct = default)
    {
        await _db.CustomGCodeBlocks.AddAsync(block, ct);
        await _db.SaveChangesAsync(ct);
    }

    public async Task UpdateAsync(CustomGCodeBlock block, CancellationToken ct = default)
    {
        _db.CustomGCodeBlocks.Update(block);
        await _db.SaveChangesAsync(ct);
    }

    public async Task DeleteAsync(Guid id, CancellationToken ct = default)
    {
        var block = await _db.CustomGCodeBlocks.FindAsync([id], ct);
        if (block is not null)
        {
            _db.CustomGCodeBlocks.Remove(block);
            await _db.SaveChangesAsync(ct);
        }
    }
}
