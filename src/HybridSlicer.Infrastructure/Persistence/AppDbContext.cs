using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Storage.ValueConversion;

namespace HybridSlicer.Infrastructure.Persistence;

public sealed class AppDbContext : DbContext
{
    public AppDbContext(DbContextOptions<AppDbContext> options) : base(options) { }

    public DbSet<MachineProfile> MachineProfiles => Set<MachineProfile>();
    public DbSet<PrintProfile> PrintProfiles => Set<PrintProfile>();
    public DbSet<CncTool> CncTools => Set<CncTool>();
    public DbSet<Material> Materials => Set<Material>();
    public DbSet<PrintJob> PrintJobs => Set<PrintJob>();
    public DbSet<HybridProcessPlan> HybridProcessPlans => Set<HybridProcessPlan>();
    public DbSet<ProcessStep> ProcessSteps => Set<ProcessStep>();
    public DbSet<CustomGCodeBlock> CustomGCodeBlocks => Set<CustomGCodeBlock>();
    public DbSet<BrandingSettings> BrandingSettings => Set<BrandingSettings>();

    protected override void OnModelCreating(ModelBuilder model)
    {
        // ── MachineProfile ──────────────────────────────────────────────────
        model.Entity<MachineProfile>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(200).IsRequired();
            e.Property(x => x.Type).HasConversion<string>();
            e.Property(x => x.OriginMode).HasConversion<string>().HasDefaultValue(HybridSlicer.Domain.ValueObjects.OriginMode.BedCenter);
            e.Property(x => x.Version).HasMaxLength(20);
            e.HasQueryFilter(x => !x.IsDeleted);

            // CNC offset stored as owned entity (value object)
            e.OwnsOne(x => x.CncOffset, o =>
            {
                o.Property(p => p.X).HasColumnName("CncOffsetX");
                o.Property(p => p.Y).HasColumnName("CncOffsetY");
                o.Property(p => p.Z).HasColumnName("CncOffsetZ");
                o.Property(p => p.RotationDeg).HasColumnName("CncOffsetRotDeg");
            });

            // ToolOffsets stored as JSON column (SQLite / PG)
            e.OwnsMany(x => x.ToolOffsets, o =>
            {
                o.ToJson();
            });

            // Multi-bed definitions stored as JSON
            e.Property(x => x.BedsJson)
                .HasColumnName("Beds")
                .HasColumnType("TEXT")
                .HasDefaultValue("[]");
            e.Ignore(x => x.Beds);

            // Nozzle X offsets stored as JSON string
            e.Property(x => x.NozzleXOffsetsJson)
                .HasColumnName("NozzleXOffsets")
                .HasColumnType("TEXT")
                .HasDefaultValue("[]");
            e.Ignore(x => x.NozzleXOffsets);

            // Nozzle Y offsets stored as JSON string
            e.Property(x => x.NozzleYOffsetsJson)
                .HasColumnName("NozzleYOffsets")
                .HasColumnType("TEXT")
                .HasDefaultValue("[]");
            e.Ignore(x => x.NozzleYOffsets);

            // Extruder assignments stored as JSON
            e.OwnsMany(x => x.ExtruderAssignments, o =>
            {
                o.ToJson();
            });
        });

        // ── PrintProfile ─────────────────────────────────────────────────────
        model.Entity<PrintProfile>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(200).IsRequired();
            e.Property(x => x.Version).HasMaxLength(20);
            e.HasQueryFilter(x => !x.IsDeleted);
        });

        // ── CncTool ──────────────────────────────────────────────────────────
        model.Entity<CncTool>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(200).IsRequired();
            e.Property(x => x.Type).HasConversion<string>();
            e.HasQueryFilter(x => !x.IsDeleted);
            e.Ignore(x => x.RadiusMm); // computed property
        });

        // ── Material ─────────────────────────────────────────────────────────
        model.Entity<Material>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(200).IsRequired();
            e.Property(x => x.Type).HasMaxLength(50);
        });

        // ── PrintJob ─────────────────────────────────────────────────────────
        model.Entity<PrintJob>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(300).IsRequired();
            e.Property(x => x.StlFilePath).HasMaxLength(1000);
            e.Property(x => x.Status).HasConversion<string>();
            e.Property(x => x.PrintGCodePath).HasMaxLength(1000);
            e.Property(x => x.HybridGCodePath).HasMaxLength(1000);
        });

        // ── HybridProcessPlan ────────────────────────────────────────────────
        model.Entity<HybridProcessPlan>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.OverallSafetyStatus).HasConversion<string>();
            e.HasMany(x => x.Steps).WithOne().HasForeignKey(s => s.PlanId);
        });

        // ── ProcessStep ──────────────────────────────────────────────────────
        model.Entity<ProcessStep>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.OperationType).HasConversion<string>();
            e.Property(x => x.SafetyStatus).HasConversion<string>();
            e.Property(x => x.PrintGCodeFragment).HasMaxLength(int.MaxValue);
            e.Property(x => x.CncGCodeFragment).HasMaxLength(int.MaxValue);
        });

        // ── CustomGCodeBlock ─────────────────────────────────────────────────
        model.Entity<CustomGCodeBlock>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.Name).HasMaxLength(200).IsRequired();
            e.Property(x => x.Trigger).HasConversion<string>();
            e.Property(x => x.GCodeContent).HasMaxLength(int.MaxValue);

            // Machine scoping — null MachineProfileId means "shared by every machine".
            // Deliberately no FK constraint: deleting a machine profile is a soft
            // delete, and orphaned blocks stay visible in the shared list rather
            // than disappearing silently.
            e.Property(x => x.MachineProfileId);
            e.HasIndex(x => x.MachineProfileId);
            e.Property(x => x.RepeatEveryNLayers).HasDefaultValue(0);
            e.Property(x => x.StartLayer).HasDefaultValue(1);
            e.Property(x => x.EndLayer);
        });

        // ── BrandingSettings ─────────────────────────────────────────────────
        model.Entity<BrandingSettings>(e =>
        {
            e.HasKey(x => x.Id);
            e.Property(x => x.CompanyName).HasMaxLength(200);
            e.Property(x => x.AppTitle).HasMaxLength(200);
            e.Property(x => x.PrimaryColor).HasMaxLength(20);
            e.Property(x => x.AccentColor).HasMaxLength(20);
        });
    }
}
