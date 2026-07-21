using System.Diagnostics;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

[assembly: System.Runtime.Versioning.SupportedOSPlatform("windows")]

namespace HybridSlicer.Launcher;

static class Program
{
    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")]
    private static extern bool IsIconic(IntPtr hWnd);

    private const string MutexName = "Global\\HybridSlicer_SingleInstance_B8F3A2E1";

    [STAThread]
    static void Main()
    {
        using var mutex = new Mutex(true, MutexName, out bool createdNew);
        if (!createdNew)
        {
            // Another instance is already running — bring it to front
            var existing = Process.GetProcessesByName("HybridSlicer")
                .FirstOrDefault(p => p.Id != Environment.ProcessId && p.MainWindowHandle != IntPtr.Zero);
            if (existing is not null)
            {
                var hwnd = existing.MainWindowHandle;
                if (IsIconic(hwnd)) ShowWindow(hwnd, 9); // SW_RESTORE
                SetForegroundWindow(hwnd);
            }
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);

        var baseDir = AppContext.BaseDirectory;

        // ── 1. Find a working CuraEngine (>= 5.12.0) ────────────────────────────
        var (curaExe, curaDef, curaExt, curaVersion) = FindValidCura();

        if (curaExe is null)
        {
            var (oldExe, _, _, oldVersion) = FindAnyCura();
            string reason = oldExe is not null
                ? $"CuraEngine {oldVersion} is installed but has a known bug that prevents slicing. " +
                  "HybridSlicer will now download and install Cura 5.12.0 (≈180 MB) alongside it."
                : "CuraEngine was not found on this PC. " +
                  "HybridSlicer will now download and install Cura 5.12.0 (≈180 MB) automatically.";

            using var setup = new CuraSetupForm(reason);
            setup.ShowDialog();

            if (setup.Succeeded)
                (curaExe, curaDef, curaExt, _) = FindValidCura();
        }

        if (curaExe is not null)
            PatchAppsettings(baseDir, curaExe, curaDef!, curaExt!);

        // ── 2. Kill any existing process on port 5000 ────────────────────────────
        KillOnPort(5000);

        // ── 3. Start the API server ──────────────────────────────────────────────
        var apiExe = Path.Combine(baseDir, "HybridSlicer.Api.exe");
        if (!File.Exists(apiExe))
        {
            MessageBox.Show(
                $"HybridSlicer.Api.exe not found next to the launcher.\n\nExpected:\n{apiExe}",
                "HybridSlicer — Startup Error",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return;
        }

        var server = Process.Start(new ProcessStartInfo
        {
            FileName         = apiExe,
            Arguments        = "--urls http://*:5000",
            WorkingDirectory = baseDir,
            UseShellExecute  = false,
            CreateNoWindow   = true,
        })!;

        var networkIp = GetLocalIp();
        using var form = new LauncherForm(server, networkIp, curaExe);
        Application.Run(form);

        try { if (!server.HasExited) server.Kill(entireProcessTree: true); } catch { }
    }

    static string GetLocalIp()
    {
        try
        {
            using var udp = new UdpClient();
            udp.Connect("8.8.8.8", 80);
            return ((System.Net.IPEndPoint)udp.Client.LocalEndPoint!).Address.ToString();
        }
        catch { return "localhost"; }
    }

    static (string? exe, string? def, string? ext, string? version) FindValidCura()
    {
        foreach (var (exe, def, ext, version, major, minor) in EnumerateCuraInstalls())
        {
            if (major > 5 || (major == 5 && minor >= 12))
                return (exe, def, ext, version);
        }
        return (null, null, null, null);
    }

    static (string? exe, string? def, string? ext, string? version) FindAnyCura()
    {
        foreach (var (exe, def, ext, version, _, _) in EnumerateCuraInstalls())
            return (exe, def, ext, version);
        return (null, null, null, null);
    }

    static IEnumerable<(string exe, string def, string ext, string version, int major, int minor)>
        EnumerateCuraInstalls()
    {
        var roots = new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            @"C:\Program Files",
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        }.Distinct().Where(Directory.Exists);

        var results = new List<(string exe, string def, string ext, string version, int major, int minor)>();

        foreach (var root in roots)
        {
            IEnumerable<string> dirs = Array.Empty<string>();
            try
            {
                dirs = Directory.GetDirectories(root, "UltiMaker Cura*")
                    .Concat(Directory.GetDirectories(root, "Ultimaker Cura*"));
            }
            catch { continue; }

            foreach (var dir in dirs)
            {
                var exe = Path.Combine(dir, "CuraEngine.exe");
                var def = Path.Combine(dir, @"share\cura\resources\definitions\fdmprinter.def.json");
                var ext = Path.Combine(dir, @"share\cura\resources\definitions\fdmextruder.def.json");

                if (!File.Exists(exe) || !File.Exists(def) || !File.Exists(ext)) continue;

                var m = Regex.Match(Path.GetFileName(dir), @"(\d+)\.(\d+)(?:\.(\d+))?");
                if (!m.Success) continue;

                int.TryParse(m.Groups[1].Value, out var major);
                int.TryParse(m.Groups[2].Value, out var minor);
                var patch   = m.Groups[3].Success ? m.Groups[3].Value : "0";
                var version = $"{major}.{minor}.{patch}";

                results.Add((exe, def, ext, version, major, minor));
            }
        }

        return results.OrderByDescending(r => r.major * 10000 + r.minor * 100);
    }

    static void PatchAppsettings(string baseDir, string exe, string def, string ext)
    {
        var path = Path.Combine(baseDir, "appsettings.json");
        if (!File.Exists(path)) return;

        try
        {
            var root = JsonNode.Parse(File.ReadAllText(path))!;
            root["CuraEngine"]!["ExecutablePath"]          = exe;
            root["CuraEngine"]!["DefinitionsPath"]          = def;
            root["CuraEngine"]!["ExtruderDefinitionsPath"]  = ext;
            root["Urls"] = "http://*:5000";
            File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { }
    }

    static void KillOnPort(int port)
    {
        try
        {
            var psi = new ProcessStartInfo("netstat", "-ano -p TCP")
            {
                UseShellExecute        = false,
                RedirectStandardOutput = true,
                CreateNoWindow         = true,
            };
            using var proc = Process.Start(psi)!;
            var output = proc.StandardOutput.ReadToEnd();
            proc.WaitForExit();

            foreach (var line in output.Split('\n'))
            {
                if (!line.Contains($":{port}") || !line.Contains("LISTENING")) continue;
                var parts = line.Trim().Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length > 0 && int.TryParse(parts[^1], out var pid))
                {
                    try { Process.GetProcessById(pid).Kill(entireProcessTree: true); } catch { }
                    Thread.Sleep(500);
                }
            }
        }
        catch { }
    }
}
