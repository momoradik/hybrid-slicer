using HybridSlicer.Api.Hubs;
using HybridSlicer.Api.Middleware;
using HybridSlicer.Api.Services;
using HybridSlicer.Infrastructure;
using HybridSlicer.Infrastructure.Persistence;
using MediatR;
using Microsoft.EntityFrameworkCore;
using Serilog;

// ── Ensure logs directory exists before Serilog tries to write ───────────────
Directory.CreateDirectory("logs");

// ── Bootstrap Serilog early ──────────────────────────────────────────────────
Log.Logger = new LoggerConfiguration()
    .WriteTo.Console(outputTemplate:
        "[{Timestamp:HH:mm:ss} {Level:u3}] {SourceContext} {Message:lj}{NewLine}{Exception}")
    .WriteTo.File("logs/hybridslicer-.log",
        rollingInterval: RollingInterval.Day,
        retainedFileCountLimit: 14)
    .MinimumLevel.Information()
    .CreateBootstrapLogger();

try
{
    var builder = WebApplication.CreateBuilder(args);

    // ── Serilog full pipeline ────────────────────────────────────────────────
    builder.Host.UseSerilog((ctx, _, cfg) =>
        cfg.ReadFrom.Configuration(ctx.Configuration)
           .WriteTo.Console()
           .WriteTo.File("logs/hybridslicer-.log", rollingInterval: RollingInterval.Day));

    // ── Infrastructure (DB, repos, slicing, toolpath, safety, machine) ───────
    builder.Services.AddInfrastructure(builder.Configuration);

    // ── MediatR — discover handlers in Application assembly ─────────────────
    builder.Services.AddMediatR(cfg =>
        cfg.RegisterServicesFromAssemblyContaining<
            HybridSlicer.Application.UseCases.ImportStl.ImportStlCommand>());

    // ── SignalR ──────────────────────────────────────────────────────────────
    builder.Services.AddSignalR();
    builder.Services.AddHostedService<MachineStatusBroadcaster>();

    // ── ASP.NET Core ─────────────────────────────────────────────────────────
    builder.Services.AddControllers()
        .AddJsonOptions(o =>
            o.JsonSerializerOptions.Converters.Add(
                new System.Text.Json.Serialization.JsonStringEnumConverter()));
    builder.Services.AddEndpointsApiExplorer();
    builder.Services.AddSwaggerGen(c =>
    {
        c.SwaggerDoc("v1", new Microsoft.OpenApi.Models.OpenApiInfo
        {
            Title   = "HybridSlicer API",
            Version = "v1",
            Description = "Hybrid 3D-printing + CNC manufacturing platform API"
        });
    });

    // CORS — allow local Vite dev server and any production origins in config
    builder.Services.AddCors(opts => opts.AddDefaultPolicy(policy =>
        policy
            .WithOrigins(
                "http://localhost:5173",
                "http://localhost:5174",
                "http://localhost:5175",
                "http://localhost:5176",
                "http://localhost:3000")
            .AllowAnyMethod()
            .AllowAnyHeader()
            .AllowCredentials()));

    var app = builder.Build();

    // ── Auto-migrate + seed ──────────────────────────────────────────────────
    using (var scope = app.Services.CreateScope())
    {
        var db     = scope.ServiceProvider.GetRequiredService<AppDbContext>();
        var logger = scope.ServiceProvider.GetRequiredService<ILogger<AppDbContext>>();

        if (db.Database.IsSqlite())
        {
            // Create the database if it doesn't exist; no-op if it already does.
            await db.Database.EnsureCreatedAsync();

            // EnsureCreated does not apply migrations to existing databases.
            // Add any new columns that may be missing from an older schema.
            string[] alters = [
                "ALTER TABLE PrintJobs ADD COLUMN AdhesionType TEXT NOT NULL DEFAULT 'none'",
                "ALTER TABLE PrintJobs ADD COLUMN GcodeHoming INTEGER NOT NULL DEFAULT 1",
                "ALTER TABLE PrintJobs ADD COLUMN GcodeLevelling INTEGER NOT NULL DEFAULT 0",
                // Existing jobs keep the previous behaviour: blocks are applied.
                "ALTER TABLE PrintJobs ADD COLUMN ApplyCustomGCodeBlocks INTEGER NOT NULL DEFAULT 1",
                // PrintProfile new columns
                "ALTER TABLE PrintProfiles ADD COLUMN TopBottomSpeedMmS REAL NOT NULL DEFAULT 30.0",
                "ALTER TABLE PrintProfiles ADD COLUMN RetractMinTravelMm REAL NOT NULL DEFAULT 1.5",
                "ALTER TABLE PrintProfiles ADD COLUMN MinLayerTimeSec REAL NOT NULL DEFAULT 5.0",
                "ALTER TABLE PrintProfiles ADD COLUMN MinSpeedMmS REAL NOT NULL DEFAULT 10.0",
                "ALTER TABLE PrintProfiles ADD COLUMN AccelerationControlEnabled INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE PrintProfiles ADD COLUMN JerkControlEnabled INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE PrintProfiles ADD COLUMN TopLayers INTEGER NOT NULL DEFAULT 4",
                "ALTER TABLE PrintProfiles ADD COLUMN BottomLayers INTEGER NOT NULL DEFAULT 4",
                "ALTER TABLE PrintProfiles ADD COLUMN RetractionEnabled INTEGER NOT NULL DEFAULT 1",
                "ALTER TABLE PrintProfiles ADD COLUMN SkinMonotonic INTEGER NOT NULL DEFAULT 1",
                "ALTER TABLE PrintProfiles ADD COLUMN FirstLayerTravelSpeedMmS REAL NOT NULL DEFAULT 50.0",
                // Pellet / high-flow tuning (mapped onto real CuraEngine keys)
                "ALTER TABLE PrintProfiles ADD COLUMN PressureAdvanceFactor REAL NOT NULL DEFAULT 0.05",
                "ALTER TABLE PrintProfiles ADD COLUMN FlowRateCompensationFactorPct REAL NOT NULL DEFAULT 100.0",
                "ALTER TABLE PrintProfiles ADD COLUMN FlowRateMaxExtrusionOffsetMm REAL NOT NULL DEFAULT 0.0",
                "ALTER TABLE PrintProfiles ADD COLUMN MaterialMaxFlowRateMm3S REAL NOT NULL DEFAULT 16.0",
                "ALTER TABLE PrintProfiles ADD COLUMN FlowEqualizationRatioPct REAL NOT NULL DEFAULT 100.0",
                "ALTER TABLE PrintProfiles ADD COLUMN InitialLayerFlowPct REAL NOT NULL DEFAULT 100.0",
                "ALTER TABLE PrintProfiles ADD COLUMN StandbyTemperatureDegC REAL NOT NULL DEFAULT 150.0",
                // CustomGCodeBlock: per-machine scoping + layer-interval firing
                "ALTER TABLE CustomGCodeBlocks ADD COLUMN MachineProfileId TEXT NULL",
                "ALTER TABLE CustomGCodeBlocks ADD COLUMN RepeatEveryNLayers INTEGER NOT NULL DEFAULT 0",
                "ALTER TABLE CustomGCodeBlocks ADD COLUMN StartLayer INTEGER NOT NULL DEFAULT 1",
                "ALTER TABLE CustomGCodeBlocks ADD COLUMN EndLayer INTEGER NULL",
            ];
            foreach (var sql in alters)
                try { await db.Database.ExecuteSqlRawAsync(sql); } catch { /* column already exists */ }
        }
        else
        {
            await db.Database.MigrateAsync();
        }
        await DbSeeder.SeedAsync(db, logger);
    }

    // ── Middleware pipeline ──────────────────────────────────────────────────
    app.UseMiddleware<GlobalExceptionMiddleware>();

    app.UseSwagger();
    app.UseSwaggerUI(c => c.SwaggerEndpoint("/swagger/v1/swagger.json", "HybridSlicer v1"));

    app.UseSerilogRequestLogging();
    app.UseCors();

    // Serve the built React SPA (placed in wwwroot by the Vite build).
    //
    // index.html must never be cached: it is the only thing naming the hashed asset
    // bundles, so a stale copy pins the app to an old build forever. The hashed
    // assets themselves are immutable — their name changes whenever the content
    // does — so they can be cached hard.
    //
    // These options are shared with MapFallbackToFile below. That matters: the
    // fallback does NOT inherit the options given to UseStaticFiles, so previously
    // "/" and deep links such as "/printer" — i.e. every URL a user actually opens —
    // were served with no Cache-Control at all and were free to be heuristically
    // cached, while only the literal "/index.html" got the no-store header.
    var spaFileOptions = new StaticFileOptions
    {
        OnPrepareResponse = ctx =>
        {
            var headers = ctx.Context.Response.Headers;

            if (ctx.File.Name.EndsWith(".html", StringComparison.OrdinalIgnoreCase))
            {
                headers["Cache-Control"] = "no-store, no-cache, must-revalidate, max-age=0";
                headers["Pragma"] = "no-cache";
                headers["Expires"] = "0";
            }
            else if (ctx.Context.Request.Path.StartsWithSegments("/assets"))
            {
                // Content-hashed filenames — safe to keep for a year.
                headers["Cache-Control"] = "public, max-age=31536000, immutable";
            }
        }
    };

    app.UseStaticFiles(spaFileOptions);

    app.UseAuthorization();
    app.MapControllers();
    app.MapHub<MachineHub>("/hubs/machine");
    app.MapFallbackToFile("index.html", spaFileOptions);

    Log.Information("HybridSlicer API ready");
    await app.RunAsync();
}
catch (Exception ex)
{
    Log.Fatal(ex, "Application startup failed");
    throw;
}
finally
{
    Log.CloseAndFlush();
}
