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

    // Versioning
    public string Version { get; private set; } = "1.0";
    public DateTime CreatedAt { get; private set; }
    public DateTime UpdatedAt { get; private set; }
    public bool IsDeleted { get; private set; }

    private PrintProfile() { }

    public static PrintProfile Create(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new DomainException("INVALID_NAME", "Print profile name must not be empty.");

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
            throw new DomainException("INVALID_LAYER_HEIGHT", "Layer height must be between 0 and 1 mm.");
        LayerHeightMm = mm;
        return Touch();
    }

    public PrintProfile WithSpeeds(double print, double travel, double infill, double wall, double firstLayer)
    {
        PrintSpeedMmS = print;
        TravelSpeedMmS = travel;
        InfillSpeedMmS = infill;
        WallSpeedMmS = wall;
        FirstLayerSpeedMmS = firstLayer;
        return Touch();
    }

    public PrintProfile WithTemperatures(int extruder, int bed)
    {
        PrintTemperatureDegC = extruder;
        BedTemperatureDegC = bed;
        return Touch();
    }

    public PrintProfile WithInfill(double densityPct, string pattern)
    {
        if (densityPct is < 0 or > 100)
            throw new DomainException("INVALID_INFILL", "Infill density must be 0–100 %.");
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
            throw new DomainException("INVALID_FLOW", "Material flow must be 1–200 %.");
        MaterialFlowPct = flowPct;
        return Touch();
    }

    public PrintProfile WithNozzleDiameter(double mm)
    {
        if (mm < 0 || mm > 5)
            throw new DomainException("INVALID_NOZZLE", "Nozzle diameter must be 0–5 mm (0 = use machine default).");
        NozzleDiameterMm = mm;
        return Touch();
    }

    public PrintProfile WithInnerWallSpeed(double mmS)
    {
        if (mmS < 0 || mmS > 1000)
            throw new DomainException("INVALID_SPEED", "Speed must be 0–1000 mm/s.");
        InnerWallSpeedMmS = mmS;
        return Touch();
    }

    public PrintProfile WithTopBottomSpeed(double mmS)
    {
        if (mmS < 0 || mmS > 1000)
            throw new DomainException("INVALID_SPEED", "Speed must be 0–1000 mm/s.");
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
            throw new DomainException("INVALID_VIRTUAL_DIAMETER",
                "Virtual filament diameter must be between 0.1 and 5 mm.");
        PelletModeEnabled = enabled;
        VirtualFilamentDiameterMm = virtualDiameterMm;
        return Touch();
    }

    public void SoftDelete() { IsDeleted = true; Touch(); }

    private PrintProfile Touch() { UpdatedAt = DateTime.UtcNow; return this; }
}
