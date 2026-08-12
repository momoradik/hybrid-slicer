using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;

namespace HybridSlicer.Application.Interfaces.Repositories;

public interface ICustomGCodeBlockRepository
{
    Task<CustomGCodeBlock?> GetByIdAsync(Guid id, CancellationToken ct = default);
    Task<IReadOnlyList<CustomGCodeBlock>> GetAllAsync(CancellationToken ct = default);
    Task<IReadOnlyList<CustomGCodeBlock>> GetEnabledAsync(CancellationToken ct = default);

    /// <summary>
    /// Blocks that apply to the given machine: those scoped to it plus the shared
    /// ones (MachineProfileId == null). Pass enabledOnly to exclude deactivated blocks.
    /// </summary>
    Task<IReadOnlyList<CustomGCodeBlock>> GetForMachineAsync(
        Guid? machineProfileId, bool enabledOnly, CancellationToken ct = default);
    Task<IReadOnlyList<CustomGCodeBlock>> GetByTriggerAsync(GCodeTrigger trigger, CancellationToken ct = default);
    Task AddAsync(CustomGCodeBlock block, CancellationToken ct = default);
    Task UpdateAsync(CustomGCodeBlock block, CancellationToken ct = default);
    Task DeleteAsync(Guid id, CancellationToken ct = default);
}
