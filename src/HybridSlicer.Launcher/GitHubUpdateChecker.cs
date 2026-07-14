using System.Diagnostics;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text.Json;

namespace HybridSlicer.Launcher;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
public sealed class GitHubUpdateChecker : IDisposable
{
    private const string Owner = "momoradik";
    private const string Repo = "hybrid-slicer";

    private readonly HttpClient _http;
    private readonly Version _currentVersion;
    private readonly System.Windows.Forms.Timer _timer;
    private readonly string _downloadDir;
    private readonly string? _token;

    private string? _pendingInstallerPath;

    public event Action<string>? UpdateAvailable;      // version string
    public event Action<int>?    DownloadProgress;      // 0-100
    public event Action<string>? UpdateReady;           // installer path
    public event Action<string>? UpdateError;           // error message

    public GitHubUpdateChecker()
    {
        _currentVersion = typeof(GitHubUpdateChecker).Assembly.GetName().Version
                          ?? new Version(1, 0, 0);

        _downloadDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "HybridSlicer", "updates");
        Directory.CreateDirectory(_downloadDir);

        _token = LoadEmbeddedToken();

        _http = new HttpClient();
        _http.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));
        if (_token is not null)
            _http.DefaultRequestHeaders.Authorization =
                new AuthenticationHeaderValue("Bearer", _token);

        _timer = new System.Windows.Forms.Timer { Interval = 30 * 60 * 1000 }; // 30 min
        _timer.Tick += async (_, _) => await CheckForUpdateAsync();
    }

    public string CurrentVersion => $"{_currentVersion.Major}.{_currentVersion.Minor}.{_currentVersion.Build}";

    public void Start()
    {
        _timer.Start();
        // Initial check after 5 seconds
        var delay = new System.Windows.Forms.Timer { Interval = 5000 };
        delay.Tick += async (_, _) =>
        {
            delay.Stop();
            delay.Dispose();
            await CheckForUpdateAsync();
        };
        delay.Start();
    }

    public async Task CheckForUpdateAsync()
    {
        try
        {
            var url = $"https://api.github.com/repos/{Owner}/{Repo}/releases/latest";
            var response = await _http.GetAsync(url);

            if (!response.IsSuccessStatusCode)
            {
                Log($"GitHub API returned {response.StatusCode}");
                return;
            }

            var json = await response.Content.ReadAsStringAsync();
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            var tagName = root.GetProperty("tag_name").GetString() ?? "";
            var versionStr = tagName.TrimStart('v', 'V');

            if (!Version.TryParse(versionStr, out var remoteVersion))
            {
                Log($"Could not parse version from tag: {tagName}");
                return;
            }

            Log($"Current: {_currentVersion}, Remote: {remoteVersion}");

            if (remoteVersion <= _currentVersion)
                return;

            // Find the .exe installer asset
            string? assetUrl = null;
            string? assetName = null;

            foreach (var asset in root.GetProperty("assets").EnumerateArray())
            {
                var name = asset.GetProperty("name").GetString() ?? "";
                if (name.EndsWith("-Setup.exe", StringComparison.OrdinalIgnoreCase) ||
                    name.EndsWith("-Installer.exe", StringComparison.OrdinalIgnoreCase) ||
                    (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.Contains("Setup", StringComparison.OrdinalIgnoreCase)))
                {
                    assetUrl = asset.GetProperty("browser_download_url").GetString();
                    assetName = name;
                    break;
                }
            }

            if (assetUrl is null)
            {
                Log("No installer asset found in release");
                return;
            }

            Log($"Update available: v{versionStr} ({assetName})");
            UpdateAvailable?.Invoke(versionStr);
        }
        catch (Exception ex)
        {
            Log($"Update check failed: {ex.Message}");
        }
    }

    public async Task DownloadUpdateAsync()
    {
        try
        {
            var url = $"https://api.github.com/repos/{Owner}/{Repo}/releases/latest";
            var response = await _http.GetAsync(url);
            if (!response.IsSuccessStatusCode) { UpdateError?.Invoke("Could not reach update server."); return; }

            var json = await response.Content.ReadAsStringAsync();
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            string? assetUrl = null;
            string? assetName = null;

            foreach (var asset in root.GetProperty("assets").EnumerateArray())
            {
                var name = asset.GetProperty("name").GetString() ?? "";
                if (name.EndsWith("-Setup.exe", StringComparison.OrdinalIgnoreCase) ||
                    name.EndsWith("-Installer.exe", StringComparison.OrdinalIgnoreCase) ||
                    (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.Contains("Setup", StringComparison.OrdinalIgnoreCase)))
                {
                    assetUrl = asset.GetProperty("url").GetString();
                    assetName = name;
                    break;
                }
            }

            if (assetUrl is null) { UpdateError?.Invoke("Installer not found in release."); return; }

            var destPath = Path.Combine(_downloadDir, assetName!);

            // Download via API URL with Accept: application/octet-stream (works for private repos)
            using var dlRequest = new HttpRequestMessage(HttpMethod.Get, assetUrl);
            dlRequest.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/octet-stream"));
            if (_token is not null)
                dlRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);
            dlRequest.Headers.UserAgent.Add(new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));
            using var dlResponse = await _http.SendAsync(dlRequest, HttpCompletionOption.ResponseHeadersRead);
            if (!dlResponse.IsSuccessStatusCode) { UpdateError?.Invoke($"Download failed: {dlResponse.StatusCode}"); return; }

            var totalBytes = dlResponse.Content.Headers.ContentLength ?? -1;
            await using var contentStream = await dlResponse.Content.ReadAsStreamAsync();
            await using var fileStream = new FileStream(destPath, FileMode.Create, FileAccess.Write, FileShare.None, 81920, true);

            var buffer = new byte[81920];
            long bytesRead = 0;
            int read;

            while ((read = await contentStream.ReadAsync(buffer)) > 0)
            {
                await fileStream.WriteAsync(buffer.AsMemory(0, read));
                bytesRead += read;
                if (totalBytes > 0)
                {
                    var percent = (int)(bytesRead * 100 / totalBytes);
                    DownloadProgress?.Invoke(percent);
                }
            }

            _pendingInstallerPath = destPath;
            Log($"Download complete: {destPath}");
            UpdateReady?.Invoke(destPath);
        }
        catch (Exception ex)
        {
            Log($"Download failed: {ex.Message}");
            UpdateError?.Invoke(ex.Message);
        }
    }

    public void InstallUpdate()
    {
        if (_pendingInstallerPath is null || !File.Exists(_pendingInstallerPath))
        {
            Log("InstallUpdate: no installer file found");
            UpdateError?.Invoke("No downloaded update to install.");
            return;
        }

        try
        {
            Log($"InstallUpdate: launching {_pendingInstallerPath}");

            var proc = Process.Start(new ProcessStartInfo
            {
                FileName = _pendingInstallerPath,
                Arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-",
                UseShellExecute = true,
            });

            Log($"InstallUpdate: installer launched (PID {proc?.Id}), exiting app in 2s...");

            // Wait for the installer to actually start before exiting
            Thread.Sleep(2000);

            Log("InstallUpdate: exiting app now");
            Environment.Exit(0);
        }
        catch (Exception ex)
        {
            Log($"InstallUpdate FAILED: {ex.Message}");
            UpdateError?.Invoke($"Could not launch installer: {ex.Message}");
        }
    }

    private static string? LoadEmbeddedToken()
    {
        try
        {
            using var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("update-token.txt");
            if (stream is null) return null;
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd().Trim();
        }
        catch { return null; }
    }

    private static void Log(string msg)
    {
        try
        {
            var logDir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "HybridSlicer");
            Directory.CreateDirectory(logDir);
            var logPath = Path.Combine(logDir, "update-log.txt");
            File.AppendAllText(logPath, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {msg}\n");
        }
        catch { }
    }

    public void Dispose()
    {
        _timer.Stop();
        _timer.Dispose();
        _http.Dispose();
    }
}
