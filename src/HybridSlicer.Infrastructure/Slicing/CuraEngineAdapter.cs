using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.Exceptions;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HybridSlicer.Infrastructure.Slicing;

/// <summary>
/// Wraps the CuraEngine CLI binary (5.x) to slice STL files.
///
/// Invocation (CuraEngine 5.x):
///   CuraEngine slice -v -p -j fdmprinter.def.json -s key=value ... -e0 -j fdmextruder.def.json -s key=value ... -l model.stl -o output.gcode
///
/// NOTE: CuraEngine 5.10.x crashes with STATUS_INTEGER_DIVIDE_BY_ZERO in GcodeWriter
/// during every slice regardless of settings. CuraEngine 5.12.0+ is required.
/// CheckCuraVersionAsync() rejects older versions with an actionable error message.
/// </summary>
public sealed class CuraEngineAdapter : ISlicingEngine
{
    private readonly CuraEngineOptions _opts;
    private readonly ILogger<CuraEngineAdapter> _logger;

    public CuraEngineAdapter(IOptions<CuraEngineOptions> opts, ILogger<CuraEngineAdapter> logger)
    {
        _opts = opts.Value;
        _logger = logger;
    }

    public async Task<SlicingResult> SliceAsync(
        string stlFilePath,
        SlicingParameters p,
        CancellationToken cancellationToken = default)
    {
        // Resolve executable (handles both full paths and PATH-only names)
        var exePath = ResolveExecutable(_opts.ExecutablePath)
            ?? throw new SlicingException(
                $"CuraEngine not found at '{_opts.ExecutablePath}'. " +
                "Set CuraEngine:ExecutablePath to the full path of CuraEngine.exe " +
                "(e.g. C:\\Program Files\\UltiMaker Cura 5.10.1\\CuraEngine.exe).");

        await CheckCuraVersionAsync(exePath);

        if (!File.Exists(stlFilePath))
            throw new SlicingException($"STL file not found: {stlFilePath}");

        var workDir   = Path.GetDirectoryName(stlFilePath)!;
        var gcodePath = Path.Combine(workDir, "print.gcode");

        // Build args: -j base definition + individual -s flags + -e0 extruder + -l model -o gcode
        var args = BuildArgs(_opts.DefinitionsPath, _opts.ExtruderDefinitionsPath, p, stlFilePath, gcodePath);
        _logger.LogInformation("CuraEngine: {Exe} {Args}", exePath, args);

        var stdout = new StringBuilder();
        var stderr = new StringBuilder();

        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromSeconds(_opts.TimeoutSeconds));

        using var proc = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName               = exePath,
                Arguments              = args,
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                UseShellExecute        = false,
                CreateNoWindow         = true,
                WorkingDirectory       = workDir,
            },
        };

        proc.OutputDataReceived += (_, e) => { if (e.Data != null) stdout.AppendLine(e.Data); };
        proc.ErrorDataReceived  += (_, e) => { if (e.Data != null) stderr.AppendLine(e.Data); };

        proc.Start();
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();

        try
        {
            await proc.WaitForExitAsync(cts.Token);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            proc.Kill(entireProcessTree: true);
            throw new SlicingException($"CuraEngine timed out after {_opts.TimeoutSeconds}s.");
        }

        var allOutput   = stdout + stderr.ToString();
        var gcodeExists = File.Exists(gcodePath) && new FileInfo(gcodePath).Length > 0;

        if (proc.ExitCode != 0)
        {
            if (gcodeExists)
            {
                // CuraEngine 5.10.x crashes in post-export cleanup after the G-code file is
                // fully written. The output is valid — log a warning and continue.
                _logger.LogWarning(
                    "CuraEngine exited {Code} but produced a valid G-code file. " +
                    "This is a known post-export crash in CuraEngine 5.10.x and is safe to ignore. " +
                    "Upgrade to Cura 5.12.0+ to eliminate this warning.", proc.ExitCode);
            }
            else
            {
                _logger.LogError("CuraEngine exit {Code}.\nSTDOUT:\n{Out}\nSTDERR:\n{Err}",
                    proc.ExitCode, stdout, stderr);
                throw new SlicingException(
                    $"CuraEngine failed (exit {proc.ExitCode}). See server logs for details.",
                    proc.ExitCode);
            }
        }

        if (!gcodeExists)
            throw new SlicingException("CuraEngine reported success but produced no G-code file.");

        // ParseSummary reads process stdout/stderr.  Layer count and time metadata are
        // emitted inside the G-code file itself (not on stdout), so supplement with a
        // scan of the first ~200 lines of the output file where Cura writes ;LAYER_COUNT.
        var gcodeHeader = ReadFirstLines(gcodePath, 200);
        var (totalLayers, timeSec, filamentMm) = ParseSummary(allOutput + "\n" + gcodeHeader);
        _logger.LogInformation("Sliced: {Layers} layers, {Time:F0}s, {Fil:F0} mm filament",
            totalLayers, timeSec, filamentMm);

        return new SlicingResult(gcodePath, totalLayers, timeSec, filamentMm);
    }

    // ── Private ────────────────────────────────────────────────────────────────

    /// <summary>
    /// Builds the CuraEngine slice command arguments using individual -s key=value flags.
    ///
    /// Compatibility notes for CuraEngine 5.x:
    ///  - The -r flag causes integer divide-by-zero in 5.10.x GcodeWriter — never use it.
    ///  - In 5.10.x the extruder context does NOT inherit global line_width variants
    ///    (wall_line_width_0/x, skin_line_width, infill_line_width). Each must be set
    ///    explicitly in both the global section AND the -e0 extruder section.
    ///  - layer_height and line_width must also be repeated in the -e0 section in 5.10.x,
    ///    otherwise GcodeWriter reads them as 0 and crashes with STATUS_INTEGER_DIVIDE_BY_ZERO.
    /// </summary>
    private static string BuildArgs(
        string? definitionsPath,
        string? extruderDefinitionsPath,
        SlicingParameters p,
        string stlPath,
        string gcodePath)
    {
        var ic      = System.Globalization.CultureInfo.InvariantCulture;
        var lw      = p.LineWidthMm.ToString("F4", ic);
        var lh      = p.LayerHeightMm.ToString("F4", ic);
        var minWall = (p.LineWidthMm * 0.85).ToString("F4", ic);

        var sb = new StringBuilder("slice -v -p");

        // ── Base machine definition ──────────────────────────────────────────
        if (!string.IsNullOrWhiteSpace(definitionsPath) && File.Exists(definitionsPath))
            sb.Append($" -j \"{definitionsPath}\"");

        // ── Global / machine-level settings ──────────────────────────────────
        sb.Append($" -s layer_height={lh}");
        sb.Append($" -s layer_height_0={lh}");
        sb.Append($" -s line_width={lw}");

        // Explicit line-width variants — in CuraEngine 5.10.x these do NOT fall back
        // to the global line_width at the extruder level, so we set them everywhere.
        sb.Append($" -s wall_line_width_0={lw}");
        sb.Append($" -s wall_line_width_x={lw}");
        sb.Append($" -s skin_line_width={lw}");
        sb.Append($" -s infill_line_width={lw}");
        sb.Append($" -s support_line_width={lw}");
        sb.Append($" -s skirt_brim_line_width={lw}");
        sb.Append($" -s min_wall_line_width={minWall}");
        sb.Append($" -s min_even_wall_line_width={minWall}");
        sb.Append($" -s min_odd_wall_line_width={minWall}");

        sb.Append($" -s wall_line_count={p.WallCount}");
        sb.Append($" -s top_layers={p.TopLayers}");
        sb.Append($" -s bottom_layers={p.BottomLayers}");
        sb.Append($" -s speed_print={p.PrintSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_travel={p.TravelSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_infill={p.InfillSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_wall_0={p.WallSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_wall_x={p.InnerWallSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_layer_0={p.FirstLayerSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_topbottom={p.TopBottomSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_travel_layer_0={p.FirstLayerTravelSpeedMmS.ToString("F1", ic)}");

        // Cooling: minimum layer time and minimum speed
        sb.Append($" -s cool_min_layer_time={p.MinLayerTimeSec.ToString("F1", ic)}");
        sb.Append($" -s cool_min_speed={p.MinSpeedMmS.ToString("F1", ic)}");

        // Motion control
        sb.Append($" -s acceleration_enabled={p.AccelerationControlEnabled.ToString().ToLowerInvariant()}");
        sb.Append($" -s jerk_enabled={p.JerkControlEnabled.ToString().ToLowerInvariant()}");

        // Surface quality
        sb.Append($" -s skin_monotonic={p.SkinMonotonic.ToString().ToLowerInvariant()}");

        // Infill: set both the density percentage AND the resolved line-distance so
        // CuraEngine cannot fall back to a stale formula value from the definition file.
        // infill_line_distance = (line_width * 100) / infill_sparse_density
        var infillLineDist = p.InfillDensityPct > 0
            ? (p.LineWidthMm * 100.0) / p.InfillDensityPct
            : 99999.0;  // near-zero density → very large spacing, avoids divide-by-zero in engine
        sb.Append($" -s infill_sparse_density={p.InfillDensityPct.ToString("F4", ic)}");
        sb.Append($" -s infill_line_distance={infillLineDist.ToString("F4", ic)}");
        sb.Append($" -s infill_pattern={p.InfillPattern}");

        sb.Append($" -s material_print_temperature={p.PrintTemperatureDegC}");
        sb.Append($" -s material_bed_temperature={p.BedTemperatureDegC}");
        sb.Append($" -s support_enable={p.SupportEnabled.ToString().ToLowerInvariant()}");
        if (p.SupportEnabled)
        {
            var placement = string.IsNullOrWhiteSpace(p.SupportPlacement) ? "everywhere" : p.SupportPlacement;
            sb.Append($" -s support_type={placement}");
            sb.Append($" -s support_structure={p.SupportType}");
            sb.Append($" -s support_infill_rate={p.SupportInfillDensityPct.ToString("F1", ic)}");
            if (!string.IsNullOrWhiteSpace(p.SupportInfillPattern))
                sb.Append($" -s support_pattern={p.SupportInfillPattern}");
        }
        sb.Append($" -s cool_fan_enabled={p.CoolingEnabled.ToString().ToLowerInvariant()}");
        sb.Append($" -s cool_fan_speed={p.CoolingFanSpeedPct.ToString("F1", ic)}");
        sb.Append($" -s machine_width={p.BedWidthMm.ToString("F1", ic)}");
        sb.Append($" -s machine_depth={p.BedDepthMm.ToString("F1", ic)}");
        sb.Append($" -s machine_height={p.BedHeightMm.ToString("F1", ic)}");
        sb.Append($" -s machine_nozzle_size={p.NozzleDiameterMm.ToString("F2", ic)}");
        // Origin mode: must match the STL viewer and G-code preview coordinate system.
        sb.Append($" -s machine_center_is_zero={p.OriginIsBedCenter.ToString().ToLowerInvariant()}");
        var adhesion = string.IsNullOrWhiteSpace(p.AdhesionType) ? "none" : p.AdhesionType;
        sb.Append($" -s adhesion_type={adhesion}");

        // ── Extruder-0 settings ───────────────────────────────────────────────
        // CuraEngine 5.x resolves most per-feature settings from the extruder context.
        // In 5.10.x, global values are NOT automatically propagated — each must be set
        // explicitly here to prevent "no value given" errors and divide-by-zero crashes.
        sb.Append(" -e0");
        if (!string.IsNullOrWhiteSpace(extruderDefinitionsPath) && File.Exists(extruderDefinitionsPath))
            sb.Append($" -j \"{extruderDefinitionsPath}\"");

        // Repeat layer height and ALL line-width variants in extruder context.
        // This is the primary fix for CuraEngine 5.10.x integer divide-by-zero crashes.
        sb.Append($" -s layer_height={lh}");
        sb.Append($" -s layer_height_0={lh}");
        sb.Append($" -s line_width={lw}");
        sb.Append($" -s wall_line_width_0={lw}");
        sb.Append($" -s wall_line_width_x={lw}");
        sb.Append($" -s skin_line_width={lw}");
        sb.Append($" -s infill_line_width={lw}");
        sb.Append($" -s support_line_width={lw}");
        sb.Append($" -s skirt_brim_line_width={lw}");
        sb.Append($" -s min_wall_line_width={minWall}");
        sb.Append($" -s min_even_wall_line_width={minWall}");
        sb.Append($" -s min_odd_wall_line_width={minWall}");

        sb.Append($" -s wall_line_count={p.WallCount}");
        sb.Append($" -s top_layers={p.TopLayers}");
        sb.Append($" -s bottom_layers={p.BottomLayers}");

        // Speeds — CuraEngine 5.x resolves these from extruder context
        sb.Append($" -s speed_print={p.PrintSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_travel={p.TravelSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_infill={p.InfillSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_wall_0={p.WallSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_wall_x={p.InnerWallSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_layer_0={p.FirstLayerSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_topbottom={p.TopBottomSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s speed_travel_layer_0={p.FirstLayerTravelSpeedMmS.ToString("F1", ic)}");

        // Cooling
        sb.Append($" -s cool_min_layer_time={p.MinLayerTimeSec.ToString("F1", ic)}");
        sb.Append($" -s cool_min_speed={p.MinSpeedMmS.ToString("F1", ic)}");

        // Motion control
        sb.Append($" -s acceleration_enabled={p.AccelerationControlEnabled.ToString().ToLowerInvariant()}");
        sb.Append($" -s jerk_enabled={p.JerkControlEnabled.ToString().ToLowerInvariant()}");

        // Surface quality
        sb.Append($" -s skin_monotonic={p.SkinMonotonic.ToString().ToLowerInvariant()}");

        // Retraction enable
        sb.Append($" -s retraction_enable={p.RetractionEnabled.ToString().ToLowerInvariant()}");

        // Infill
        sb.Append($" -s infill_sparse_density={p.InfillDensityPct.ToString("F4", ic)}");
        sb.Append($" -s infill_line_distance={infillLineDist.ToString("F4", ic)}");
        sb.Append($" -s infill_pattern={p.InfillPattern}");

        // Material
        sb.Append($" -s material_diameter={p.FilamentDiameterMm.ToString("F2", ic)}");
        sb.Append($" -s material_flow={p.MaterialFlowPct.ToString("F1", ic)}");
        sb.Append($" -s machine_nozzle_size={p.NozzleDiameterMm.ToString("F2", ic)}");
        sb.Append($" -s material_print_temperature={p.PrintTemperatureDegC}");
        sb.Append($" -s material_print_temperature_layer_0={p.PrintTemperatureDegC}");
        sb.Append($" -s material_bed_temperature={p.BedTemperatureDegC}");
        sb.Append($" -s material_bed_temperature_layer_0={p.BedTemperatureDegC}");

        // Retraction
        sb.Append($" -s retraction_amount={p.RetractLengthMm.ToString("F2", ic)}");
        sb.Append($" -s retraction_speed={p.RetractSpeedMmS.ToString("F1", ic)}");
        sb.Append($" -s retraction_min_travel={p.RetractMinTravelMm.ToString("F2", ic)}");

        // Support — repeat in extruder context
        sb.Append($" -s support_enable={p.SupportEnabled.ToString().ToLowerInvariant()}");
        if (p.SupportEnabled)
        {
            var extPlacement = string.IsNullOrWhiteSpace(p.SupportPlacement) ? "everywhere" : p.SupportPlacement;
            sb.Append($" -s support_type={extPlacement}");
            sb.Append($" -s support_structure={p.SupportType}");
            sb.Append($" -s support_infill_rate={p.SupportInfillDensityPct.ToString("F1", ic)}");
            if (!string.IsNullOrWhiteSpace(p.SupportInfillPattern))
                sb.Append($" -s support_pattern={p.SupportInfillPattern}");
        }

        // Cooling
        sb.Append($" -s cool_fan_enabled={p.CoolingEnabled.ToString().ToLowerInvariant()}");
        sb.Append($" -s cool_fan_speed={p.CoolingFanSpeedPct.ToString("F1", ic)}");

        // Settings with no default in fdmextruder.def.json — cause crashes if missing.
        sb.Append(" -s roofing_layer_count=0");
        sb.Append(" -s flooring_layer_count=0");
        sb.Append(" -s support_z_seam_away_from_model=false");

        // Input model and output G-code (filenames only; WorkingDirectory = job folder).
        sb.Append($" -l \"{Path.GetFileName(stlPath)}\"");
        sb.Append($" -o \"{Path.GetFileName(gcodePath)}\"");

        return sb.ToString();
    }

    private static string ReadFirstLines(string path, int maxLines)
    {
        var sb = new StringBuilder();
        using var sr = new StreamReader(path, Encoding.UTF8);
        for (var i = 0; i < maxLines; i++)
        {
            var line = sr.ReadLine();
            if (line is null) break;
            sb.AppendLine(line);
        }
        return sb.ToString();
    }

    private static (int layers, double timeSec, double filamentMm) ParseSummary(string output)
    {
        int    layers     = 0;
        double timeSec    = 0;
        double filamentMm = 0;

        foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var t = line.Trim();
            if (t.StartsWith(";LAYER_COUNT:", StringComparison.OrdinalIgnoreCase))
                int.TryParse(t[13..], out layers);
            else if (t.StartsWith(";TIME:", StringComparison.OrdinalIgnoreCase))
                double.TryParse(t[6..], out timeSec);
            else if (t.StartsWith(";Filament used:", StringComparison.OrdinalIgnoreCase))
            {
                var raw = t[15..].Trim().TrimEnd('m', 'M');
                if (double.TryParse(raw, out var metres)) filamentMm = metres * 1000;
            }
        }

        return (Math.Max(layers, 1), timeSec, filamentMm);
    }

    // CuraEngine prints its version in the help output (no dedicated "version" sub-command).
    // Running with no args outputs: "Cura_SteamEngine version 5.10.1\n[help text]"
    private async Task CheckCuraVersionAsync(string exePath)
    {
        try
        {
            using var proc = Process.Start(new ProcessStartInfo
            {
                FileName               = exePath,
                Arguments              = "",
                RedirectStandardOutput = true,
                RedirectStandardError  = true,
                UseShellExecute        = false,
                CreateNoWindow         = true,
            })!;

            var stdout = await proc.StandardOutput.ReadToEndAsync();
            var stderr = await proc.StandardError.ReadToEndAsync();
            await proc.WaitForExitAsync();

            var allOutput = stdout + " " + stderr;

            // Extract "Cura_SteamEngine version X.Y.Z"
            var match = System.Text.RegularExpressions.Regex.Match(
                allOutput, @"version\s+(\d+)\.(\d+)\.?(\d*)");

            if (!match.Success)
            {
                _logger.LogWarning("Could not parse CuraEngine version from output: {Out}", allOutput.Trim());
                return;
            }

            var major = int.Parse(match.Groups[1].Value);
            var minor = int.Parse(match.Groups[2].Value);
            var versionStr = $"{major}.{minor}" + (match.Groups[3].Success && match.Groups[3].Value != "" ? $".{match.Groups[3].Value}" : "");
            _logger.LogInformation("CuraEngine version: {Version}", versionStr);

            // CuraEngine 5.10.x and earlier have a confirmed STATUS_INTEGER_DIVIDE_BY_ZERO
            // bug in GcodeWriter that crashes every slice regardless of settings.
            // 5.12.0+ is required for stable operation.
            if (major < 5 || (major == 5 && minor <= 10))
            {
                throw new SlicingException(
                    $"CuraEngine {versionStr} has a known GcodeWriter bug that prevents slicing. " +
                    $"Please install UltiMaker Cura 5.12.0 or later from " +
                    $"https://ultimaker.com/software/ultimaker-cura/ — " +
                    $"your install at '{exePath}' will be detected automatically after upgrade.");
            }
        }
        catch (SlicingException)
        {
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogDebug("Could not read CuraEngine version: {Msg}", ex.Message);
        }
    }

    /// <summary>
    /// Resolves an executable name or path.
    /// Handles: full paths, relative paths, names on PATH, and common Cura install locations.
    /// </summary>
    private static string? ResolveExecutable(string nameOrPath)
    {
        if (string.IsNullOrWhiteSpace(nameOrPath)) return null;

        // Absolute or relative path with directory separator
        if (Path.IsPathRooted(nameOrPath) || nameOrPath.Contains(Path.DirectorySeparatorChar) || nameOrPath.Contains('/'))
            return File.Exists(nameOrPath) ? nameOrPath : null;

        // Name only — check current directory first
        if (File.Exists(nameOrPath)) return Path.GetFullPath(nameOrPath);

        // Add .exe on Windows
        var candidates = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) && !nameOrPath.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)
            ? [nameOrPath, nameOrPath + ".exe"]
            : new[] { nameOrPath };

        // Search PATH
        var pathEnv = Environment.GetEnvironmentVariable("PATH") ?? "";
        foreach (var dir in pathEnv.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        foreach (var name in candidates)
        {
            var full = Path.Combine(dir.Trim(), name);
            if (File.Exists(full)) return full;
        }

        // Common Cura install locations on Windows
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            var searchRoots = new[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs"),
            };

            foreach (var root in searchRoots.Where(Directory.Exists))
            foreach (var dir in Directory.GetDirectories(root)
                         .Where(d => Path.GetFileName(d).Contains("Cura", StringComparison.OrdinalIgnoreCase)
                                  || Path.GetFileName(d).Contains("UltiMaker", StringComparison.OrdinalIgnoreCase)))
            foreach (var name in candidates)
            {
                var full = Path.Combine(dir, name);
                if (File.Exists(full)) return full;
            }
        }

        return null;
    }
}

public sealed class CuraEngineOptions
{
    public const string Section = "CuraEngine";

    /// <summary>
    /// Full path to CuraEngine.exe, or just "CuraEngine" if it is on the system PATH.
    /// Example: C:\Program Files\UltiMaker Cura 5.10.1\CuraEngine.exe
    /// </summary>
    public string ExecutablePath { get; set; } = "CuraEngine";

    /// <summary>
    /// Path to the base fdmprinter.def.json required by CuraEngine 5.x.
    /// Example: C:\Program Files\UltiMaker Cura 5.12.0\share\cura\resources\definitions\fdmprinter.def.json
    /// </summary>
    public string DefinitionsPath { get; set; } = "";

    /// <summary>
    /// Path to fdmextruder.def.json for extruder-0 settings (required in CuraEngine 5.x).
    /// Example: C:\Program Files\UltiMaker Cura 5.12.0\share\cura\resources\definitions\fdmextruder.def.json
    /// </summary>
    public string ExtruderDefinitionsPath { get; set; } = "";

    /// <summary>Maximum time to wait for a slice operation before killing the process.</summary>
    public int TimeoutSeconds { get; set; } = 600;
}
