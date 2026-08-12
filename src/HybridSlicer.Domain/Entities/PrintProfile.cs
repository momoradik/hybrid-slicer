using HybridSlicer.Domain.Exceptions;

namespace HybridSlicer.Domain.Entities;

/// <summary>
/// Complete set of 3-D printing / slicing parameters, matching Cura's
/// major setting categories. All values use SI units (mm, mm/s, °C).
/// </summary>
public class PrintProfile
{
    public Guid Id { get; private set; }
    public string Name { get; private set; } = string.Empty;

    // Layer / line
    public double LayerHeightMm { get; private set; } = 0.2;
    public double LineWidthMm { get; private set; } = 0.4;
    public int WallCount { get; private set; } = 3;
    public int TopBottomLayers { get; private set; } = 4;
    public int TopLayers { get; private set; } = 4;
    public int BottomLayers { get; private set; } = 4;

    // Speed (mm/s)
    public double PrintSpeedMmS { get; private set; } = 50;
    public double TravelSpeedMmS { get; private set; } = 150;
    public double InfillSpeedMmS { get; private set; } = 70;
    public double WallSpeedMmS { get; private set; } = 30;
    public double FirstLayerSpeedMmS { get; private set; } = 20;
    public double FirstLayerTravelSpeedMmS { get; private set; } = 50;

    // Infill
    public double InfillDensityPct { get; private set; } = 20;
    public string InfillPattern { get; private set; } = "grid";

    // Temperature (°C)
    public int PrintTemperatureDegC { get; private set; } = 210;
    public int BedTemperatureDegC { get; private set; } = 60;

    // Retraction
    public bool RetractionEnabled { get; private set; } = true;
    public double RetractLengthMm { get; private set; } = 5;
    public double RetractSpeedMmS { get; private set; } = 45;

    // Support
    public bool SupportEnabled { get; private set; }
    public string SupportType { get; private set; } = "normal";
    public double SupportOverhangAngleDeg { get; private set; } = 50;

    // Cooling
    public bool CoolingEnabled { get; private set; } = true;
    public int CoolingFanSpeedPct { get; private set; } = 100;

    // Filament
    public double FilamentDiameterMm { get; private set; } = 1.75;

    // Extrusion
    public double MaterialFlowPct { get; private set; } = 100.0;

    // Nozzle — overrides machine profile when > 0; 0 = use machine nozzle diameter
    public double NozzleDiameterMm { get; private set; } = 0.0;

    // Advanced speeds (mm/s)
    public double InnerWallSpeedMmS { get; private set; } = 60;
    public double TopBottomSpeedMmS { get; private set; } = 30;

    // Retraction (advanced)
    public double RetractMinTravelMm { get; private set; } = 1.5;

    // Cooling (advanced)
    public double MinLayerTimeSec { get; private set; } = 5;
    public double MinSpeedMmS { get; private set; } = 10;

    // Motion control
    public bool AccelerationControlEnabled { get; private set; }
    public bool JerkControlEnabled { get; private set; }

    // Surface quality
    public bool SkinMonotonic { get; private set; } = true;

    // Skirt / brim
    public bool BrimEnabled { get; private set; }
    public int BrimLineCount { get; private set; } = 8;

    // Pellet extrusion mode
    public bool PelletModeEnabled { get; private set; }
    public double VirtualFilamentDiameterMm { get; private set; } = 1.0;

    // ── Pellet / high-flow tuning ────────────────────────────────────────────
    // These map 1:1 onto real CuraEngine keys (verified against fdmprinter.def.json
    // 5.12). They are only emitted when PelletModeEnabled is true — a pellet screw
    // has very different pressure dynamics to a filament feeder, and these are the
    // settings that model it.

    /// <summary>material_pressure_advance_factor — syncs extrusion with motion.</summary>
    public double PressureAdvanceFactor { get; private set; } = 0.05;

    /// <summary>flow_rate_extrusion_offset_factor (%) — back-pressure compensation strength.</summary>
    public double FlowRateCompensationFactorPct { get; private set; } = 100;

    /// <summary>flow_rate_max_extrusion_offset (mm) — cap on back-pressure compensation.</summary>
    public double FlowRateMaxExtrusionOffsetMm { get; private set; }

    /// <summary>material_max_flowrate (mm³/s) — the screw's maximum melt throughput.</summary>
    public double MaterialMaxFlowRateMm3S { get; private set; } = 16;

    /// <summary>speed_equalize_flow_width_factor (%) — slows wide lines to hold flow constant.</summary>
    public double FlowEqualizationRatioPct { get; private set; } = 100;

    /// <summary>material_flow_layer_0 (%) — initial layer flow.</summary>
    public double InitialLayerFlowPct { get; private set; } = 100;

    /// <summary>material_standby_temperature (°C) — nozzle temp while another tool prints.</summary>
    public double StandbyTemperatureDegC { get; private set; } = 150;

    // Versioning
    public string Version { get; private set; } = "1.0";
    public DateTime CreatedAt { get; private set; }
    public DateTime UpdatedAt { get; private set; }
    public bool IsDeleted { get; private set; }

    private PrintProfile() { }

    public static PrintProfile Create(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw DomainException.Invalid("INVALID_NAME", "name", "Profile name", name, "a non-empty name");

        return new PrintProfile
        {
            Id = Guid.NewGuid(),
            Name = name.Trim(),
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow
        };
    }

    public PrintProfile WithLayerHeight(double mm)
    {
        if (mm is <= 0 or > 1)
            throw DomainException.Invalid("INVALID_LAYER_HEIGHT", "layerHeightMm", "Layer height", mm,
                "greater than 0 and at most 1 mm");
        LayerHeightMm = mm;
        return Touch();
    }

    public PrintProfile WithSpeeds(double print, double travel, double infill, double wall, double firstLayer)
    {
        RequireSpeed(print,      "printSpeedMmS",      "Print speed");
        RequireSpeed(travel,     "travelSpeedMmS",     "Travel speed");
        RequireSpeed(infill,     "infillSpeedMmS",     "Infill speed");
        RequireSpeed(wall,       "wallSpeedMmS",       "Outer wall speed");
        RequireSpeed(firstLayer, "firstLayerSpeedMmS", "Initial layer speed");

        PrintSpeedMmS = print;
        TravelSpeedMmS = travel;
        InfillSpeedMmS = infill;
        WallSpeedMmS = wall;
        FirstLayerSpeedMmS = firstLayer;
        return Touch();
    }

    public PrintProfile WithTemperatures(int extruder, int bed)
    {
        if (extruder is < 0 or > 500)
            throw DomainException.Invalid("INVALID_TEMPERATURE", "printTemperatureDegC",
                "Print temperature", extruder, "between 0 and 500 °C");
        if (bed is < 0 or > 200)
            throw DomainException.Invalid("INVALID_TEMPERATURE", "bedTemperatureDegC",
                "Bed temperature", bed, "between 0 and 200 °C");

        PrintTemperatureDegC = extruder;
        BedTemperatureDegC = bed;
        return Touch();
    }

    public PrintProfile WithInfill(double densityPct, string pattern)
    {
        if (densityPct is < 0 or > 100)
            throw DomainException.Invalid("INVALID_INFILL", "infillDensityPct", "Infill density", densityPct,
                "between 0 and 100 %");
        InfillDensityPct = densityPct;
        InfillPattern = pattern;
        return Touch();
    }

    public PrintProfile WithSupport(bool enabled, string type = "normal", double overhangAngle = 50)
    {
        SupportEnabled = enabled;
        SupportType = type;
        SupportOverhangAngleDeg = overhangAngle;
        return Touch();
    }

    public PrintProfile WithFlow(double flowPct)
    {
        if (flowPct is <= 0 or > 200)
            throw DomainException.Invalid("INVALID_FLOW", "materialFlowPct", "Flow / extrusion", flowPct,
                "greater than 0 and at most 200 %");
        MaterialFlowPct = flowPct;
        return Touch();
    }

    public PrintProfile WithNozzleDiameter(double mm)
    {
        if (mm < 0 || mm > 5)
            throw DomainException.Invalid("INVALID_NOZZLE", "nozzleDiameterMm", "Nozzle diameter", mm,
                "between 0 and 5 mm (0 means: use the machine profile's nozzle)");
        NozzleDiameterMm = mm;
        return Touch();
    }

    public PrintProfile WithInnerWallSpeed(double mmS)
    {
        if (mmS < 0 || mmS > 1000)
            throw DomainException.Invalid("INVALID_SPEED", "innerWallSpeedMmS", "Inner wall speed", mmS,
                "between 0 and 1000 mm/s");
        InnerWallSpeedMmS = mmS;
        return Touch();
    }

    public PrintProfile WithTopBottomSpeed(double mmS)
    {
        if (mmS < 0 || mmS > 1000)
            throw DomainException.Invalid("INVALID_SPEED", "topBottomSpeedMmS", "Top/bottom speed", mmS,
                "between 0 and 1000 mm/s");
        TopBottomSpeedMmS = mmS;
        return Touch();
    }

    public PrintProfile WithRetraction(bool enabled, double lengthMm, double speedMmS, double minTravelMm)
    {
        RetractionEnabled = enabled;
        RetractLengthMm = lengthMm;
        RetractSpeedMmS = speedMmS;
        RetractMinTravelMm = minTravelMm;
        return Touch();
    }

    public PrintProfile WithTopBottomLayers(int top, int bottom)
    {
        TopLayers = top;
        BottomLayers = bottom;
        TopBottomLayers = Math.Max(top, bottom); // keep legacy field in sync
        return Touch();
    }

    public PrintProfile WithSkinMonotonic(bool enabled)
    {
        SkinMonotonic = enabled;
        return Touch();
    }

    public PrintProfile WithFirstLayerTravelSpeed(double mmS)
    {
        FirstLayerTravelSpeedMmS = mmS;
        return Touch();
    }

    public PrintProfile WithCoolingLimits(double minLayerTimeSec, double minSpeedMmS)
    {
        MinLayerTimeSec = minLayerTimeSec;
        MinSpeedMmS = minSpeedMmS;
        return Touch();
    }

    public PrintProfile WithMotionControl(bool accelerationEnabled, bool jerkEnabled)
    {
        AccelerationControlEnabled = accelerationEnabled;
        JerkControlEnabled = jerkEnabled;
        return Touch();
    }

    public PrintProfile WithPelletMode(bool enabled, double virtualDiameterMm = 1.0)
    {
        if (enabled && (virtualDiameterMm <= 0 || virtualDiameterMm > 5))
            throw DomainException.Invalid("INVALID_VIRTUAL_DIAMETER", "virtualFilamentDiameterMm",
                "Virtual filament diameter", virtualDiameterMm,
                "greater than 0 and at most 5 mm (this is the diameter Cura's E-math is scaled to, not a real filament)");

        PelletModeEnabled = enabled;
        VirtualFilamentDiameterMm = virtualDiameterMm;
        return Touch();
    }

    /// <summary>
    /// Pellet / high-flow tuning. Validated only when pellet mode is on, so a
    /// filament profile is never rejected for a pellet field it doesn't use.
    /// </summary>
    public PrintProfile WithPelletTuning(
        double pressureAdvanceFactor,
        double flowRateCompensationFactorPct,
        double flowRateMaxExtrusionOffsetMm,
        double materialMaxFlowRateMm3S,
        double flowEqualizationRatioPct,
        double initialLayerFlowPct,
        double standbyTemperatureDegC)
    {
        if (PelletModeEnabled)
        {
            if (pressureAdvanceFactor is < 0 or > 10)
                throw DomainException.Invalid("INVALID_PRESSURE_ADVANCE", "pressureAdvanceFactor",
                    "Pressure advance factor", pressureAdvanceFactor,
                    "between 0 and 10 (Cura's default is 0.05; higher values push more material into corners)");

            if (flowRateCompensationFactorPct is < 0 or > 1000)
                throw DomainException.Invalid("INVALID_FLOW_COMPENSATION", "flowRateCompensationFactorPct",
                    "Back-pressure compensation factor", flowRateCompensationFactorPct,
                    "between 0 and 1000 % (100 % = Cura default)");

            if (flowRateMaxExtrusionOffsetMm is < 0 or > 100)
                throw DomainException.Invalid("INVALID_MAX_EXTRUSION_OFFSET", "flowRateMaxExtrusionOffsetMm",
                    "Max compensation offset", flowRateMaxExtrusionOffsetMm,
                    "between 0 and 100 mm (0 disables the cap)");

            if (materialMaxFlowRateMm3S is <= 0 or > 1000)
                throw DomainException.Invalid("INVALID_MAX_FLOWRATE", "materialMaxFlowRateMm3S",
                    "Maximum flow rate", materialMaxFlowRateMm3S,
                    "greater than 0 and at most 1000 mm³/s — this is your screw's melt throughput ceiling");

            if (flowEqualizationRatioPct is < 0 or > 1000)
                throw DomainException.Invalid("INVALID_FLOW_EQUALIZATION", "flowEqualizationRatioPct",
                    "Flow equalization ratio", flowEqualizationRatioPct,
                    "between 0 and 1000 % (100 % holds volumetric flow constant across line widths)");

            if (initialLayerFlowPct is <= 0 or > 500)
                throw DomainException.Invalid("INVALID_INITIAL_LAYER_FLOW", "initialLayerFlowPct",
                    "Initial layer flow", initialLayerFlowPct, "greater than 0 and at most 500 %");

            if (standbyTemperatureDegC is < 0 or > 500)
                throw DomainException.Invalid("INVALID_STANDBY_TEMPERATURE", "standbyTemperatureDegC",
                    "Standby temperature", standbyTemperatureDegC, "between 0 and 500 °C");
        }

        PressureAdvanceFactor         = pressureAdvanceFactor;
        FlowRateCompensationFactorPct = flowRateCompensationFactorPct;
        FlowRateMaxExtrusionOffsetMm  = flowRateMaxExtrusionOffsetMm;
        MaterialMaxFlowRateMm3S       = materialMaxFlowRateMm3S;
        FlowEqualizationRatioPct      = flowEqualizationRatioPct;
        InitialLayerFlowPct           = initialLayerFlowPct;
        StandbyTemperatureDegC        = standbyTemperatureDegC;
        return Touch();
    }

    public void SoftDelete() { IsDeleted = true; Touch(); }

    private static void RequireSpeed(double mmS, string field, string label)
    {
        if (mmS is <= 0 or > 1000)
            throw DomainException.Invalid("INVALID_SPEED", field, label, mmS,
                "greater than 0 and at most 1000 mm/s");
    }

    private PrintProfile Touch() { UpdatedAt = DateTime.UtcNow; return this; }
}
