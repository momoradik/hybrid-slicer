using HybridSlicer.Domain.Exceptions;

namespace HybridSlicer.Domain.Entities;

public class Material
{
    public Guid Id { get; private set; }
    public string Name { get; private set; } = string.Empty;
    public string Type { get; private set; } = string.Empty; // PLA, PETG, ABS, TPU, etc.
    public double DensityGPerCm3 { get; private set; }
    public double DiameterMm { get; private set; } = 1.75;
    public int PrintTempMinDegC { get; private set; }
    public int PrintTempMaxDegC { get; private set; }
    public int BedTempMinDegC { get; private set; }
    public int BedTempMaxDegC { get; private set; }
    public int GlassTransitionTempDegC { get; private set; }
    public string? Manufacturer { get; private set; }
    public string? ColorHex { get; private set; }
    public DateTime CreatedAt { get; private set; }
    public DateTime UpdatedAt { get; private set; }

    private Material() { }

    public static Material Create(
        string name,
        string type,
        int printTempMin,
        int printTempMax,
        int bedTempMin = 0,
        int bedTempMax = 0,
        double density = 1.24,
        double diameterMm = 1.75)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new DomainException("INVALID_NAME", "Material name must not be empty.");

        return new Material
        {
            Id = Guid.NewGuid(),
            Name = name.Trim(),
            Type = type.Trim(),
            PrintTempMinDegC = printTempMin,
            PrintTempMaxDegC = printTempMax,
            BedTempMinDegC = bedTempMin,
            BedTempMaxDegC = bedTempMax,
            DensityGPerCm3 = density,
            DiameterMm = diameterMm,
            CreatedAt = DateTime.UtcNow,
            UpdatedAt = DateTime.UtcNow
        };
    }

    public void Update(
        string name,
        string type,
        int printTempMin,
        int printTempMax,
        int bedTempMin,
        int bedTempMax,
        double density,
        double diameterMm)
    {
        if (string.IsNullOrWhiteSpace(name))
            throw new DomainException("INVALID_NAME", "Material name must not be empty.");

        Name = name.Trim();
        Type = type.Trim();
        PrintTempMinDegC = printTempMin;
        PrintTempMaxDegC = printTempMax;
        BedTempMinDegC = bedTempMin;
        BedTempMaxDegC = bedTempMax;
        DensityGPerCm3 = density;
        DiameterMm = diameterMm;
        UpdatedAt = DateTime.UtcNow;
    }
}
