using HybridSlicer.Domain.Enums;
using HybridSlicer.Domain.Exceptions;

namespace HybridSlicer.Domain.Entities;

/// <summary>
/// CNC cutting tool definition including geometry and recommended cutting parameters.
/// </summary>
public class CncTool
{
    public Guid Id { get; private set; }
    public string Name { get; private set; } = string.Empty;
    public ToolType Type { get; private set; }

    // Geometry (mm)
    public double DiameterMm { get; private set; }
    public double RadiusMm => DiameterMm / 2.0;
    public double FluteLengthMm { get; private set; }
    /// <summary>Overall tool length from spindle collet face to tool tip (mm).</summary>
    public double ToolLengthMm { get; private set; }
    public double ShankDiameterMm { get; private set; }
    public int FluteCount { get; private set; }

    // Material
    public string ToolMaterial { get; private set; } = "HSS";

    // Tip geometry
    /// <summary>Extra depth (mm) below the band bottom to compensate for rounded/tapered tips.
    /// 0 for flat end mills, ~1.0–1.5 for ball/rounded burrs.</summary>
    public double TipOverlapMm { get; private set; }
    /// <summary>Tip shape: "flat", "ball", "rounded". Informational — tip_overlap does the real work.</summary>
    public string TipShape { get; private set; } = "flat";

    // Spindle / collet geometry
    /// <summary>Radius of the spindle body, collet or nut (mm). Must clear the part top during machining.</summary>
    public double SpindleRadiusMm { get; private set; }
    /// <summary>Extra safety margin (mm) added to flute-length and spindle-clearance checks.</summary>
    public double SafetyMarginMm { get; private set; } = 0.5;

    // Recommended parameters
    public double MaxDepthOfCutMm { get; private set; }
    public int RecommendedRpm { get; private set; }
    public double RecommendedFeedMmPerMin { get; private set; }

    // Audit
    public DateTime CreatedAt { get; private set; }
    public DateTime UpdatedAt { get; private set; }
    public bool IsDeleted { get; private set; }

    private CncTool() { }

    public static CncTool Create(
        string name,
        ToolType type,
        double diameterMm,
        double fluteLengthMm,
        double shankDiameterMm,
        int fluteCount = 2,
        string toolMaterial = "HSS",
        double maxDepthOfCutMm = 0,
        int recommendedRpm = 10000,
        double recommendedFeedMmPerMin = 500,
        double toolLengthMm = 50.0,
        double tipOverlapMm = 0,
        string tipShape = "flat",
        double spindleRadiusMm = 0,
        double safetyMarginMm = 0.5)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new DomainException("INVALID_NAME", "Tool name must not be empty.");
        if (diameterMm <= 0)
            throw new DomainException("INVALID_DIAMETER", "Tool diameter must be positive.");

        return new CncTool
        {
            Id = Guid.NewGuid(),
            Name = name.Trim(),
            Type = type,
            DiameterMm = diameterMm,
            FluteLengthMm = fluteLengthMm,
            ToolLengthMm = toolLengthMm > 0 ? toolLengthMm : 50.0,
            ShankDiameterMm = shankDiameterMm,
            FluteCount = fluteCount,
            ToolMaterial = toolMaterial,
            TipOverlapMm = tipOverlapMm,
            TipShape = string.IsNullOrWhiteSpace(tipShape) ? "flat" : tipShape.Trim(),
            SpindleRadiusMm = spindleRadiusMm,
            SafetyMarginMm = safetyMarginMm,
            MaxDepthOfCutMm = maxDepthOfCutMm > 0 ? maxDepthOfCutMm : diameterMm * 0.25,
            RecommendedRpm = recommendedRpm,
            RecommendedFeedMmPerMin = recommendedFeedMmPerMin,
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow
        };
    }

    public void Update(
        string name, ToolType type, double diameterMm, double fluteLengthMm,
        double shankDiameterMm, int fluteCount, string toolMaterial,
        double maxDepthOfCutMm, int recommendedRpm, double recommendedFeedMmPerMin,
        double toolLengthMm, double tipOverlapMm = 0, string tipShape = "flat",
        double spindleRadiusMm = 0, double safetyMarginMm = 0.5)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new DomainException("INVALID_NAME", "Tool name must not be empty.");
        if (diameterMm <= 0)
            throw new DomainException("INVALID_DIAMETER", "Tool diameter must be positive.");

        Name = name.Trim();
        Type = type;
        DiameterMm = diameterMm;
        FluteLengthMm = fluteLengthMm;
        ToolLengthMm = toolLengthMm > 0 ? toolLengthMm : 50.0;
        ShankDiameterMm = shankDiameterMm;
        FluteCount = fluteCount;
        ToolMaterial = toolMaterial;
        TipOverlapMm = tipOverlapMm;
        TipShape = string.IsNullOrWhiteSpace(tipShape) ? "flat" : tipShape.Trim();
        SpindleRadiusMm = spindleRadiusMm;
        SafetyMarginMm = safetyMarginMm;
        MaxDepthOfCutMm = maxDepthOfCutMm > 0 ? maxDepthOfCutMm : diameterMm * 0.25;
        RecommendedRpm = recommendedRpm;
        RecommendedFeedMmPerMin = recommendedFeedMmPerMin;
        UpdatedAt = DateTime.UtcNow;
    }

    public void UpdateCuttingParameters(int rpm, double feedMmPerMin, double maxDocMm)
    {
        if (rpm <= 0) throw new DomainException("INVALID_RPM", "RPM must be positive.");
        if (feedMmPerMin <= 0) throw new DomainException("INVALID_FEED", "Feed must be positive.");
        if (maxDocMm <= 0) throw new DomainException("INVALID_DOC", "Depth of cut must be positive.");

        RecommendedRpm = rpm;
        RecommendedFeedMmPerMin = feedMmPerMin;
        MaxDepthOfCutMm = maxDocMm;
        UpdatedAt = DateTime.UtcNow;
    }

    public void SoftDelete() { IsDeleted = true; UpdatedAt = DateTime.UtcNow; }
}
