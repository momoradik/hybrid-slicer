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
            try { await db.Database.ExecuteSqlRawAsync("ALTER TABLE PrintJobs ADD COLUMN AdhesionType TEXT NOT NULL DEFAULT 'none'"); }
            catch { /* column already exists */ }
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

    // Serve the built React SPA (placed in wwwroot by Vite build)
    // HTML files must not be cached so the browser always picks up new hashed asset filenames
    app.UseStaticFiles(new StaticFileOptions
    {
        OnPrepareResponse = ctx =>
        {
            if (ctx.File.Name.EndsWith(".html", StringComparison.OrdinalIgnoreCase))
                ctx.Context.Response.Headers["Cache-Control"] = "no-store, no-cache, must-revalidate";
        }
    });

    app.UseAuthorization();
    app.MapControllers();
    app.MapHub<MachineHub>("/hubs/machine");
    app.MapFallbackToFile("index.html");

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
