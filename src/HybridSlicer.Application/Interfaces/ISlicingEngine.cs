namespace HybridSlicer.Application.Interfaces;

/// <summary>
/// Port for the external slicing engine (e.g. CuraEngine).
/// Implementations must be thread-safe; each call spawns an isolated subprocess.
/// </summary>
public interface ISlicingEngine
{
    /// <summary>
    /// Slices the given STL using the provided print settings.
    /// Returns the path to the generated G-code file and the total layer count.
    /// </summary>
    Task<SlicingResult> SliceAsync(
        string stlFilePath,
        SlicingParameters parameters,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// All parameters required to drive the slicing engine.
/// Mirrors the major Cura fdmprinter settings used in production jobs.
/// </summary>
public sealed record SlicingParameters(
    // Layer geometry
    double LayerHeightMm,
    double LineWidthMm,
    int WallCount,
    int TopLayers,
    int BottomLayers,

    // Speeds (mm/s)
    double PrintSpeedMmS,
    double TravelSpeedMmS,
    double InfillSpeedMmS,
    double WallSpeedMmS,       // speed_wall_0 (outer wall)
    double InnerWallSpeedMmS,  // speed_wall_x (inner wall)
    double TopBottomSpeedMmS,  // speed_topbottom
    double FirstLayerSpeedMmS,
    double FirstLayerTravelSpeedMmS, // speed_travel_layer_0

    // Infill
    double InfillDensityPct,
    string InfillPattern,

    // Temperature (°C)
    int PrintTemperatureDegC,
    int BedTemperatureDegC,

    // Retraction
    bool RetractionEnabled,
    double RetractLengthMm,
    double RetractSpeedMmS,
    double RetractMinTravelMm,

    // Support
    bool SupportEnabled,
    string SupportType,
    string SupportPlacement,
    double SupportInfillDensityPct,
    string SupportInfillPattern,

    // Cooling
    bool CoolingEnabled,
    int CoolingFanSpeedPct,
    double MinLayerTimeSec,
    double MinSpeedMmS,

    // Motion control
    // Motion control
    bool AccelerationControlEnabled,
    bool JerkControlEnabled,

    // Surface quality
    bool SkinMonotonic,

    // Filament
    double FilamentDiameterMm,

    // Machine envelope
    double BedWidthMm,
    double BedDepthMm,
    double BedHeightMm,
    double NozzleDiameterMm,

    // Origin
    bool OriginIsBedCenter = true,

    // Extrusion
    double MaterialFlowPct = 100.0,

    // Bed adhesion
    string AdhesionType = "none");

public sealed record SlicingResult(
    string GCodeFilePath,
    int TotalLayers,
    double EstimatedPrintTimeSec,
    double EstimatedFilamentMm);
