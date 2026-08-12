using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.Exceptions;

namespace HybridSlicer.Domain.Entities;

/// <summary>
/// A user-defined reusable G-code snippet that fires at a configured trigger
/// point in the hybrid process (e.g., before every machining operation).
///
/// A block belongs to exactly one machine profile, or to none — a block with
/// <see cref="MachineProfileId"/> set to null is shared and offered on every
/// machine. Blocks with the <see cref="GCodeTrigger.EveryNLayers"/> trigger fire
/// on a layer cadence instead of at a phase boundary.
/// </summary>
public class CustomGCodeBlock
{
    public Guid Id { get; private set; }
    public string Name { get; private set; } = string.Empty;
    public string GCodeContent { get; private set; } = string.Empty;
    public GCodeTrigger Trigger { get; private set; }
    public string? Description { get; private set; }
    public bool IsEnabled { get; private set; } = true;
    public int SortOrder { get; private set; }

    /// <summary>Machine this block belongs to; null = shared across all machines.</summary>
    public Guid? MachineProfileId { get; private set; }

    /// <summary>
    /// Layer cadence for <see cref="GCodeTrigger.EveryNLayers"/>: 10 means "every
    /// 10th layer". Ignored (and stored as 0) for all other triggers.
    /// </summary>
    public int RepeatEveryNLayers { get; private set; }

    /// <summary>First layer the cadence may fire on (inclusive). Default 1.</summary>
    public int StartLayer { get; private set; } = 1;

    /// <summary>Last layer the cadence may fire on (inclusive); null = to the end of the print.</summary>
    public int? EndLayer { get; private set; }

    public DateTime CreatedAt { get; private set; }
    public DateTime UpdatedAt { get; private set; }

    private CustomGCodeBlock() { }

    public static CustomGCodeBlock Create(
        string name,
        string gcode,
        GCodeTrigger trigger,
        int sortOrder = 0,
        Guid? machineProfileId = null,
        int repeatEveryNLayers = 0,
        int startLayer = 1,
        int? endLayer = null,
        string? description = null)
    {
        Validate(name, gcode, trigger, repeatEveryNLayers, startLayer, endLayer);

        return new CustomGCodeBlock
        {
            Id = Guid.NewGuid(),
            Name = name.Trim(),
            GCodeContent = gcode,
            Trigger = trigger,
            SortOrder = sortOrder,
            MachineProfileId = machineProfileId,
            RepeatEveryNLayers = trigger == GCodeTrigger.EveryNLayers ? repeatEveryNLayers : 0,
            StartLayer = startLayer,
            EndLayer = endLayer,
            Description = description,
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow
        };
    }

    public void Update(
        string name,
        string gcode,
        GCodeTrigger trigger,
        string? description,
        int sortOrder,
        Guid? machineProfileId = null,
        int repeatEveryNLayers = 0,
        int startLayer = 1,
        int? endLayer = null)
    {
        Validate(name, gcode, trigger, repeatEveryNLayers, startLayer, endLayer);

        Name = name.Trim();
        GCodeContent = gcode;
        Trigger = trigger;
        Description = description;
        SortOrder = sortOrder;
        MachineProfileId = machineProfileId;
        RepeatEveryNLayers = trigger == GCodeTrigger.EveryNLayers ? repeatEveryNLayers : 0;
        StartLayer = startLayer;
        EndLayer = endLayer;
        UpdatedAt = DateTime.UtcNow;
    }

    /// <summary>
    /// True when this block should fire on the given 1-based layer number.
    /// Only meaningful for <see cref="GCodeTrigger.EveryNLayers"/> blocks.
    /// </summary>
    public bool FiresOnLayer(int layer)
    {
        if (Trigger != GCodeTrigger.EveryNLayers || RepeatEveryNLayers <= 0) return false;
        if (layer < StartLayer) return false;
        if (EndLayer.HasValue && layer > EndLayer.Value) return false;
        return (layer - StartLayer) % RepeatEveryNLayers == 0;
    }

    public void Enable() { IsEnabled = true; UpdatedAt = DateTime.UtcNow; }
    public void Disable() { IsEnabled = false; UpdatedAt = DateTime.UtcNow; }

    private static void Validate(
        string name, string gcode, GCodeTrigger trigger,
        int repeatEveryNLayers, int startLayer, int? endLayer)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw DomainException.Invalid("INVALID_NAME", "name", "Block name", name, "a non-empty name");

        if (string.IsNullOrWhiteSpace(gcode))
            throw DomainException.Invalid("INVALID_GCODE", "gCodeContent", "G-code content", gcode,
                "at least one line of G-code");

        if (startLayer < 1)
            throw DomainException.Invalid("INVALID_START_LAYER", "startLayer", "Start layer", startLayer,
                "1 or greater (layers are numbered from 1)");

        if (endLayer.HasValue && endLayer.Value < startLayer)
            throw DomainException.Invalid("INVALID_END_LAYER", "endLayer", "End layer", endLayer.Value,
                $"greater than or equal to the start layer ({startLayer})");

        if (trigger == GCodeTrigger.EveryNLayers && repeatEveryNLayers < 1)
            throw DomainException.Invalid("INVALID_LAYER_INTERVAL", "repeatEveryNLayers",
                "Repeat interval", repeatEveryNLayers,
                "1 or greater — this is the 'every N layers' cadence and is required for a layer-interval block");
    }
}
