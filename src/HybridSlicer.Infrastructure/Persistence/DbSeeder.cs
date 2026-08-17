using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Persistence;

/// <summary>
/// Seeds the database with sensible default data so the application
/// is usable immediately after first run without manual setup.
/// Idempotent — safe to call on every startup.
/// </summary>
public static class DbSeeder
{
    public static async Task SeedAsync(AppDbContext db, ILogger logger)
    {
        await SeedMaterialsAsync(db, logger);
        await SeedDefaultMachineProfileAsync(db, logger);
        await SeedDefaultPrintProfileAsync(db, logger);
        await SeedDefaultToolsAsync(db, logger);
        await SeedDefaultBrandingAsync(db, logger);
    }

    private static async Task SeedMaterialsAsync(AppDbContext db, ILogger logger)
    {
        if (await db.Materials.AnyAsync()) return;

        var materials = new[]
        {
            Material.Create("PLA Generic",   "PLA",  190, 220, 0,  0,  1.24, 1.75),
            Material.Create("PETG Generic",  "PETG", 230, 250, 70, 85, 1.27, 1.75),
            Material.Create("ABS Generic",   "ABS",  230, 250, 90, 110, 1.05, 1.75),
            Material.Create("TPU 95A",       "TPU",  220, 240, 30, 60, 1.21, 1.75),
            Material.Create("Nylon PA12",    "PA",   250, 280, 70, 90, 1.01, 1.75),
        };

        await db.Materials.AddRangeAsync(materials);
        await db.SaveChangesAsync();
        logger.LogInformation("Seeded {Count} materials", materials.Length);
    }

    private static async Task SeedDefaultMachineProfileAsync(AppDbContext db, ILogger logger)
    {
        if (await db.MachineProfiles.AnyAsync()) return;

        var profile = MachineProfile.Create(
            name:           "Default Hybrid Machine",
            type:           MachineType.Hybrid,
            bedWidth:       177.5,
            bedDepth:       160.5,
            bedHeight:      350,
            extruderCount:  1);

        // Travel must contain both bed extents AND the origin marker. Two
        // 177.5 × 160.5 beds at (22.6, 74) and (231.9, 70.9) span up to
        // X=409.4, Y=234.5; origin sits at (218.9, 290). Round up with margin.
        profile.SetTravel(450, 320, 350);
        profile.SetOriginMode(Domain.ValueObjects.OriginMode.BedCenter);
        profile.SetBedPosition(22.6, 74);
        profile.SetOrigin(218.9, 290);
        profile.SetBedEdgeOffsets(left: 227.5, right: 0, front: 37, back: 0);
        profile.SetNetworkEndpoint("192.168.1.100", 8080);
        profile.UpdateCncOffset(new Domain.ValueObjects.MachineOffset(1, -71.9, 0, 0));

        // Multi-bed: 2 beds (positions in travel frame; both fit inside the 450×320 travel).
        profile.SetBeds(new[]
        {
            new Domain.ValueObjects.BedDefinition(0, 177.5, 160.5, 350, 22.6, 74),
            new Domain.ValueObjects.BedDefinition(1, 177.5, 160.5, 350, 231.9, 70.9),
        });

        await db.MachineProfiles.AddAsync(profile);
        await db.SaveChangesAsync();
        logger.LogInformation("Seeded default machine profile");
    }

    private static async Task SeedDefaultPrintProfileAsync(AppDbContext db, ILogger logger)
    {
        if (await db.PrintProfiles.AnyAsync()) return;

        var profiles = new[]
        {
            PrintProfile.Create("Standard — 0.2 mm")
                .WithLayerHeight(0.2)
                .WithLineWidth(0.4)
                .WithWallCount(3)
                .WithSpeeds(50, 150, 70, 30, 20)
                .WithTemperatures(210, 60)
                .WithInfill(20, "grid")
                .WithCooling(true, 100),

            PrintProfile.Create("Quality — 0.1 mm")
                .WithLayerHeight(0.1)
                .WithLineWidth(0.35)
                .WithWallCount(4)
                .WithSpeeds(35, 120, 50, 20, 15)
                .WithTemperatures(205, 60)
                .WithInfill(25, "grid")
                .WithCooling(true, 100),

            PrintProfile.Create("Draft — 0.3 mm")
                .WithLayerHeight(0.3)
                .WithLineWidth(0.5)
                .WithWallCount(2)
                .WithSpeeds(70, 180, 90, 40, 25)
                .WithTemperatures(215, 60)
                .WithInfill(15, "lines")
                .WithCooling(true, 100),
        };

        await db.PrintProfiles.AddRangeAsync(profiles);
        await db.SaveChangesAsync();
        logger.LogInformation("Seeded {Count} print profiles", profiles.Length);
    }

    private static async Task SeedDefaultToolsAsync(AppDbContext db, ILogger logger)
    {
        if (await db.CncTools.AnyAsync()) return;

        var tools = new[]
        {
            CncTool.Create("Ø3mm 2-Flute Flat",    ToolType.FlatEndMill,   3.0, 22, 3.175, 2, "Carbide", 0.5, 15000, 800),
            CncTool.Create("Ø6mm 2-Flute Flat",    ToolType.FlatEndMill,   6.0, 30, 6.0,   2, "Carbide", 1.0, 10000, 600),
            CncTool.Create("Ø3mm Ball Nose",        ToolType.BallEndMill,   3.0, 22, 3.175, 2, "Carbide", 0.3, 15000, 600),
            CncTool.Create("Ø1.5mm Engraver 30°",  ToolType.Engraver,      1.5, 15, 3.175, 1, "Carbide", 0.1, 20000, 300),
        };

        await db.CncTools.AddRangeAsync(tools);
        await db.SaveChangesAsync();
        logger.LogInformation("Seeded {Count} CNC tools", tools.Length);
    }

    private static async Task SeedDefaultBrandingAsync(AppDbContext db, ILogger logger)
    {
        if (await db.BrandingSettings.AnyAsync()) return;

        await db.BrandingSettings.AddAsync(BrandingSettings.Default());
        await db.SaveChangesAsync();
        logger.LogInformation("Seeded default branding settings");
    }
}
