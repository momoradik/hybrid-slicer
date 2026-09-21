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
    private const string ReleasesUrl = $"https://github.com/{Owner}/{Repo}/releases";

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

        // Token priority: AppData file (user-replaceable) > embedded resource (build-time)
        _token = LoadAppDataToken() ?? LoadEmbeddedToken();

        _http = new HttpClient();
        _http.DefaultRequestHeaders.UserAgent.Add(
            new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));

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

    /// <summary>Raised when the check completes and no update is needed.</summary>
    public event Action? UpToDate;

    public async Task CheckForUpdateAsync()
    {
        try
        {
            var url = $"https://api.github.com/repos/{Owner}/{Repo}/releases/latest";

            // Try with token first, then retry without if auth fails (handles expired tokens
            // and the case where the repo was made public after the build).
            var response = await FetchWithFallbackAsync(url);

            if (response is null)
                return; // FetchWithFallbackAsync already fired UpdateError

            var json = await response.Content.ReadAsStringAsync();
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            var tagName = root.GetProperty("tag_name").GetString() ?? "";
            var versionStr = tagName.TrimStart('v', 'V');

            if (!Version.TryParse(versionStr, out var remoteVersion))
            {
                Log($"Could not parse version from tag: {tagName}");
                UpdateError?.Invoke($"Could not parse version from release tag: {tagName}");
                return;
            }

            Log($"Current: {_currentVersion}, Remote: {remoteVersion}");

            if (remoteVersion <= _currentVersion)
            {
                Log("Already up to date");
                UpToDate?.Invoke();
                return;
            }

            // Find the .exe installer asset — use API url (works for private repos with token)
            string? assetUrl = null;
            string? assetName = null;

            foreach (var asset in root.GetProperty("assets").EnumerateArray())
            {
                var name = asset.GetProperty("name").GetString() ?? "";
                if (name.EndsWith("-Setup.exe", StringComparison.OrdinalIgnoreCase) ||
                    name.EndsWith("-Installer.exe", StringComparison.OrdinalIgnoreCase) ||
                    (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.Contains("Setup", StringComparison.OrdinalIgnoreCase)))
                {
                    // Use API url (not browser_download_url) — works with Bearer token for private repos
                    assetUrl = asset.GetProperty("url").GetString();
                    assetName = name;
                    break;
                }
            }

            if (assetUrl is null)
            {
                Log("No installer asset found in release");
                UpdateError?.Invoke("Update found but no installer file attached to the release.");
                return;
            }

            Log($"Update available: v{versionStr} ({assetName})");
            UpdateAvailable?.Invoke(versionStr);
        }
        catch (HttpRequestException ex)
        {
            Log($"Update check failed (network): {ex.Message}");
            UpdateError?.Invoke("Could not connect to the update server. Check your internet connection.");
        }
        catch (Exception ex)
        {
            Log($"Update check failed: {ex.Message}");
            UpdateError?.Invoke($"Update check failed: {ex.Message}");
        }
    }

    /// <summary>
    /// Tries the GitHub API with the configured token. If auth fails (401/403/404 on private repo),
    /// retries without auth in case the repo has since been made public. Returns null if both fail
    /// (and fires UpdateError with actionable guidance).
    /// </summary>
    private async Task<HttpResponseMessage?> FetchWithFallbackAsync(string url)
    {
        // Attempt 1: with token
        using var authedRequest = new HttpRequestMessage(HttpMethod.Get, url);
        authedRequest.Headers.UserAgent.Add(new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));
        if (_token is not null)
            authedRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);

        var response = await _http.SendAsync(authedRequest);

        if (response.IsSuccessStatusCode)
            return response;

        Log($"GitHub API returned {response.StatusCode} (with token)");

        // Attempt 2: without token (repo may have been made public)
        if (_token is not null)
        {
            Log("Retrying without token...");
            using var unauthRequest = new HttpRequestMessage(HttpMethod.Get, url);
            unauthRequest.Headers.UserAgent.Add(new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));

            var fallback = await _http.SendAsync(unauthRequest);
            if (fallback.IsSuccessStatusCode)
            {
                Log("Succeeded without token — repo may now be public");
                return fallback;
            }
            Log($"Fallback also failed: {fallback.StatusCode}");
        }

        // Both attempts failed — provide actionable error
        var tokenPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "HybridSlicer", "update-token.txt");

        var msg = response.StatusCode switch
        {
            System.Net.HttpStatusCode.Unauthorized or System.Net.HttpStatusCode.Forbidden =>
                $"Update token expired or revoked. Place a valid GitHub token in:\n{tokenPath}\nor download the latest version manually from:\n{ReleasesUrl}",
            System.Net.HttpStatusCode.NotFound =>
                $"Cannot access the update server (private repository). Place a valid GitHub token in:\n{tokenPath}\nor download manually from:\n{ReleasesUrl}",
            _ =>
                $"Update check failed (HTTP {(int)response.StatusCode}). Download manually from:\n{ReleasesUrl}",
        };

        UpdateError?.Invoke(msg);
        return null;
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

            string? assetApiUrl = null;
            string? assetBrowserUrl = null;
            string? assetName = null;

            foreach (var asset in root.GetProperty("assets").EnumerateArray())
            {
                var name = asset.GetProperty("name").GetString() ?? "";
                if (name.EndsWith("-Setup.exe", StringComparison.OrdinalIgnoreCase) ||
                    name.EndsWith("-Installer.exe", StringComparison.OrdinalIgnoreCase) ||
                    (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) && name.Contains("Setup", StringComparison.OrdinalIgnoreCase)))
                {
                    assetApiUrl = asset.GetProperty("url").GetString();
                    assetBrowserUrl = asset.GetProperty("browser_download_url").GetString();
                    assetName = name;
                    break;
                }
            }

            if (assetApiUrl is null && assetBrowserUrl is null) { UpdateError?.Invoke("Installer not found in release."); return; }

            var destPath = Path.Combine(_downloadDir, assetName!);

            // Try API URL first (works with token for private repos), then fall back to
            // browser_download_url (direct download, works for public repos without auth).
            HttpResponseMessage? dlResponse = null;

            if (assetApiUrl is not null)
            {
                using var dlRequest = new HttpRequestMessage(HttpMethod.Get, assetApiUrl);
                dlRequest.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/octet-stream"));
                if (_token is not null)
                    dlRequest.Headers.Authorization = new AuthenticationHeaderValue("Bearer", _token);
                dlRequest.Headers.UserAgent.Add(new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));
                dlResponse = await _http.SendAsync(dlRequest, HttpCompletionOption.ResponseHeadersRead);
                if (!dlResponse.IsSuccessStatusCode)
                {
                    Log($"API asset download failed: {dlResponse.StatusCode}, trying browser URL...");
                    dlResponse.Dispose();
                    dlResponse = null;
                }
            }

            // Fallback: direct browser download URL (no auth needed for public repos)
            if (dlResponse is null && assetBrowserUrl is not null)
            {
                using var fallbackRequest = new HttpRequestMessage(HttpMethod.Get, assetBrowserUrl);
                fallbackRequest.Headers.UserAgent.Add(new ProductInfoHeaderValue("HybridSlicer", _currentVersion.ToString()));
                dlResponse = await _http.SendAsync(fallbackRequest, HttpCompletionOption.ResponseHeadersRead);
            }

            if (dlResponse is null || !dlResponse.IsSuccessStatusCode)
            {
                UpdateError?.Invoke($"Download failed. Download manually from:\n{ReleasesUrl}");
                dlResponse?.Dispose();
                return;
            }

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

    /// <summary>
    /// Set by the launcher so we can kill the server process before the installer
    /// tries to overwrite DLLs. Environment.Exit(0) does NOT fire FormClosed,
    /// so the server would stay running and lock the files.
    /// </summary>
    public Process? ServerProcess { get; set; }

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
            // Kill the API server FIRST so it releases file locks on the DLLs.
            // Environment.Exit() does not fire FormClosed, so the server would
            // otherwise stay running and block the installer from overwriting files.
            if (ServerProcess is not null && !ServerProcess.HasExited)
            {
                Log("InstallUpdate: killing server process before install...");
                try { ServerProcess.Kill(entireProcessTree: true); } catch { }
                try { ServerProcess.WaitForExit(5000); } catch { }
                Log("InstallUpdate: server stopped");
            }

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

    /// <summary>
    /// Loads token from %LOCALAPPDATA%/HybridSlicer/update-token.txt.
    /// Users can place a fresh token here to fix auth without reinstalling.
    /// </summary>
    private static string? LoadAppDataToken()
    {
        try
        {
            var path = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "HybridSlicer", "update-token.txt");
            if (!File.Exists(path)) return null;
            var token = File.ReadAllText(path).Trim();
            return string.IsNullOrEmpty(token) ? null : token;
        }
        catch { return null; }
    }

    /// <summary>Loads token embedded at build time as a fallback.</summary>
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
