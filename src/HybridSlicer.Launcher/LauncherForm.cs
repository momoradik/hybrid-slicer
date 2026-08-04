using System.Diagnostics;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace HybridSlicer.Launcher;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
public sealed class LauncherForm : Form
{
    private readonly Process _server;
    private string  _networkIp;
    private readonly string? _curaPath;
    private readonly System.Windows.Forms.Timer _healthTimer;
    private readonly GitHubUpdateChecker _updater;
    private readonly WebView2 _webView;

    private string _serverStatus = "starting";
    private int _port = 8080;

    public LauncherForm(Process server, string networkIp, string? curaPath)
    {
        _server    = server;
        _networkIp = networkIp;
        _curaPath  = curaPath;

        _healthTimer = new System.Windows.Forms.Timer { Interval = 2000 };
        _healthTimer.Tick += (_, _) => PollServerStatus();

        _updater = new GitHubUpdateChecker();
        _updater.UpdateAvailable += OnUpdateAvailable;
        _updater.DownloadProgress += OnDownloadProgress;
        _updater.UpdateReady += OnUpdateReady;
        _updater.UpdateError += OnUpdateError;
        _updater.UpToDate += OnUpToDate;

        Text            = "HybridSlicer";
        AutoScaleMode   = AutoScaleMode.None;
        using (var g = CreateGraphics())
        {
            float scale = g.DpiX / 96f;
            ClientSize = new Size((int)(480 * scale), (int)(580 * scale));
        }
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MinimizeBox     = true;
        MaximizeBox     = false;
        StartPosition   = FormStartPosition.CenterScreen;
        BackColor       = Color.FromArgb(15, 23, 42);

        Icon = BuildAppIcon();
        EnableDarkTitleBar();

        _webView = new WebView2
        {
            Dock = DockStyle.Fill,
            DefaultBackgroundColor = Color.FromArgb(15, 23, 42),
        };
        Controls.Add(_webView);

        Load += async (_, _) => await InitWebView();

        FormClosed += (_, _) =>
        {
            _healthTimer.Stop();
            try { if (!_server.HasExited) _server.Kill(entireProcessTree: true); } catch { }
        };
    }

    private async Task InitWebView()
    {
        var userDataFolder = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "HybridSlicer", "WebView2");

        var env = await CoreWebView2Environment.CreateAsync(null, userDataFolder);
        await _webView.EnsureCoreWebView2Async(env);

        // Settings
        _webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
        _webView.CoreWebView2.Settings.AreDevToolsEnabled = false;
        _webView.CoreWebView2.Settings.IsStatusBarEnabled = false;
        _webView.CoreWebView2.Settings.IsZoomControlEnabled = false;

        // Handle messages from JS
        _webView.CoreWebView2.WebMessageReceived += OnWebMessage;

        // Load embedded HTML
        var html = LoadEmbeddedHtml();
        _webView.CoreWebView2.NavigateToString(html);

        // Wait for page load, then push initial state + logo
        _webView.CoreWebView2.NavigationCompleted += (_, _) =>
        {
            PushState();
            PushLogo();
            _healthTimer.Start();
            _updater.Start();
        };
    }

    private static string LoadEmbeddedHtml()
    {
        using var stream = Assembly.GetExecutingAssembly()
            .GetManifestResourceStream("launcher.html")!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    // ── C# → JS ─────────────────────────────────────────────────────────────

    private void PushState()
    {
        var state = new
        {
            lanIP = _networkIp,
            port = _port,
            url = $"http://{_networkIp}:{_port}",
            status = _serverStatus,
            curaPath = _curaPath ?? "",
            version = _updater.CurrentVersion,
        };
        var json = JsonSerializer.Serialize(state);
        ExecuteScript($"applyStatus({json})");
    }

    private void ExecuteScript(string script)
    {
        if (_webView.CoreWebView2 is null) return;
        try { _webView.CoreWebView2.ExecuteScriptAsync(script); } catch { }
    }

    // ── JS → C# ─────────────────────────────────────────────────────────────

    private async void OnWebMessage(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        try
        {
            var msg = JsonSerializer.Deserialize<JsonElement>(e.WebMessageAsJson);
            var action = msg.GetProperty("action").GetString();

            switch (action)
            {
                case "open-browser":
                    OpenUrl($"http://{_networkIp}:{_port}");
                    break;

                case "get-all-ips":
                    var ips = GetAllLanIPs();
                    var ipsJson = JsonSerializer.Serialize(ips);
                    ExecuteScript($"loadIPs({ipsJson})");
                    break;

                case "test-ip":
                    var testIp = msg.GetProperty("ip").GetString()!;
                    var testPort = msg.GetProperty("port").GetInt32();
                    var reachable = await TestIpAsync(testIp, testPort);
                    ExecuteScript($"ipTestResult('{testIp}', {(reachable ? "true" : "false")})");
                    break;

                case "set-ip":
                    var newIp = msg.GetProperty("ip").GetString()!;
                    _networkIp = newIp;
                    var newUrl = $"http://{_networkIp}:{_port}";
                    ExecuteScript($"ipChanged('{_networkIp}', '{newUrl}')");
                    break;

                case "check-for-update":
                    await _updater.CheckForUpdateAsync();
                    break;

                case "download-update":
                    await _updater.DownloadUpdateAsync();
                    break;

                case "install-update":
                    _updater.InstallUpdate();
                    break;
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"WebMessage error: {ex.Message}");
        }
    }

    // ── Update events → JS ──────────────────────────────────────────────────

    private void OnUpdateAvailable(string version)
    {
        if (InvokeRequired) { Invoke(() => OnUpdateAvailable(version)); return; }
        ExecuteScript($"onUpdateAvailable('{version}')");
    }

    private void OnDownloadProgress(int percent)
    {
        if (InvokeRequired) { Invoke(() => OnDownloadProgress(percent)); return; }
        ExecuteScript($"onDownloadProgress({percent})");
    }

    private void OnUpdateReady(string path)
    {
        if (InvokeRequired) { Invoke(() => OnUpdateReady(path)); return; }
        ExecuteScript("onUpdateDownloaded()");
    }

    private void OnUpdateError(string message)
    {
        if (InvokeRequired) { Invoke(() => OnUpdateError(message)); return; }
        var escaped = message.Replace("'", "\\'").Replace("\n", " ");
        ExecuteScript($"onUpdateError('{escaped}')");
    }

    private void OnUpToDate()
    {
        if (InvokeRequired) { Invoke(OnUpToDate); return; }
        ExecuteScript("onUpToDate()");
    }

    // ── Server health polling ───────────────────────────────────────────────

    private void PollServerStatus()
    {
        string newStatus;
        if (_server.HasExited)
        {
            newStatus = "error";
        }
        else
        {
            try
            {
                using var tcp = new TcpClient();
                var result = tcp.BeginConnect("127.0.0.1", _port, null, null);
                newStatus = result.AsyncWaitHandle.WaitOne(300) ? "running" : "starting";
                if (newStatus == "running") tcp.EndConnect(result);
            }
            catch { newStatus = "starting"; }
        }

        if (newStatus != _serverStatus)
        {
            _serverStatus = newStatus;
            PushState();
        }
    }

    // ── Network helpers ─────────────────────────────────────────────────────

    private static List<object> GetAllLanIPs()
    {
        var result = new List<object>();
        foreach (var iface in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (iface.OperationalStatus != OperationalStatus.Up) continue;
            foreach (var addr in iface.GetIPProperties().UnicastAddresses)
            {
                if (addr.Address.AddressFamily != AddressFamily.InterNetwork) continue;
                if (IPAddress.IsLoopback(addr.Address)) continue;
                result.Add(new { ip = addr.Address.ToString(), name = iface.Name });
            }
        }
        return result;
    }

    private static async Task<bool> TestIpAsync(string ip, int port)
    {
        try
        {
            using var tcp = new TcpClient();
            var connectTask = tcp.ConnectAsync(ip, port);
            return await Task.WhenAny(connectTask, Task.Delay(2000)) == connectTask
                   && connectTask.IsCompletedSuccessfully;
        }
        catch { return false; }
    }

    private static void OpenUrl(string url)
    {
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); } catch { }
    }

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    private void EnableDarkTitleBar()
    {
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 11+), fallback 19 (Windows 10 20H1+)
        int value = 1;
        if (DwmSetWindowAttribute(Handle, 20, ref value, sizeof(int)) != 0)
            DwmSetWindowAttribute(Handle, 19, ref value, sizeof(int));
    }

    private static Icon BuildAppIcon()
    {
        try
        {
            using var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("app-icon.ico");
            if (stream is not null)
                return new Icon(stream);
        }
        catch { }
        return SystemIcons.Application;
    }

    private void PushLogo()
    {
        try
        {
            using var stream = Assembly.GetExecutingAssembly()
                .GetManifestResourceStream("app-icon-b64.txt");
            if (stream is null) return;
            using var reader = new StreamReader(stream);
            var b64 = reader.ReadToEnd().Trim();
            ExecuteScript($"document.getElementById('appLogo').src='data:image/png;base64,{b64}'");
        }
        catch { }
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _healthTimer.Dispose();
            _updater.Dispose();
            _webView.Dispose();
        }
        base.Dispose(disposing);
    }
}
