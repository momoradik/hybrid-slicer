namespace HybridSlicer.Domain.Enums;

public enum GCodeTrigger
{
    BeforeMachining = 0,
    AfterMachining = 1,
    BeforePrinting = 2,
    AfterPrinting = 3,
    JobStart = 4,
    JobEnd = 5,

    /// <summary>
    /// Fires on a layer cadence rather than at a phase boundary — e.g. every
    /// 10th layer. The cadence itself lives on the block
    /// (<see cref="Entities.CustomGCodeBlock.RepeatEveryNLayers"/>) together with
    /// the optional first/last layer window.
    /// </summary>
    EveryNLayers = 6,

    // Per-extruder triggers (extruder index encoded in value: 100+N = before, 200+N = after)
    BeforeExtruder0 = 100,
    BeforeExtruder1 = 101,
    BeforeExtruder2 = 102,
    BeforeExtruder3 = 103,
    BeforeExtruder4 = 104,
    BeforeExtruder5 = 105,
    BeforeExtruder6 = 106,
    BeforeExtruder7 = 107,
    AfterExtruder0 = 200,
    AfterExtruder1 = 201,
    AfterExtruder2 = 202,
    AfterExtruder3 = 203,
    AfterExtruder4 = 204,
    AfterExtruder5 = 205,
    AfterExtruder6 = 206,
    AfterExtruder7 = 207,
}
