using System.Diagnostics;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace HybridSlicer.Infrastructure.AlignMesh;

/// <summary>
/// Manages the alignmesh_server sidecar process lifecycle.
/// Starts the C++ HTTP server on a configured port, monitors it,
/// and restarts on crash. Provides an HttpClient for the proxy controller.
/// </summary>
public sealed class AlignMeshProcessManager : IHostedService, IDisposable
{
    private readonly AlignMeshOptions _opts;
    private readonly ILogger<AlignMeshProcessManager> _logger;
    private readonly HttpClient _http;
    private Process? _process;
    private CancellationTokenSource? _cts;
    private Task? _monitorTask;

    public AlignMeshProcessManager(
        IOptions<AlignMeshOptions> opts,
        ILogger<AlignMeshProcessManager> logger)
    {
        _opts = opts.Value;
        _logger = logger;
        _http = new HttpClient
        {
            BaseAddress = new Uri($"http://localhost:{_opts.Port}"),
            Timeout = Timeout.InfiniteTimeSpan,
        };
    }

    /// <summary>The HttpClient configured to talk to the sidecar.</summary>
    public HttpClient Http => _http;

    /// <summary>Whether the sidecar process is currently running.</summary>
    public bool IsRunning => _process is { HasExited: false };

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        var exePath = ResolveExecutable(_opts.ExecutablePath);
        if (exePath == null)
        {
            _logger.LogWarning(
                "alignmesh_server not found at '{Path}'. CMM features will be unavailable. " +
                "Build alignmesh with CMake and place the binary alongside the API.",
                _opts.ExecutablePath);
            return;
        }

        _cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        StartProcess(exePath);
        await WaitForHealthy(_cts.Token);
        _monitorTask = MonitorLoop(exePath, _cts.Token);
    }

    public async Task StopAsync(CancellationToken cancellationToken)
    {
        _cts?.Cancel();
        if (_monitorTask != null)
        {
            try { await _monitorTask; } catch (OperationCanceledException) { }
        }
        KillProcess();
    }

    public void Dispose()
    {
        _cts?.Cancel();
        KillProcess();
        _http.Dispose();
        _cts?.Dispose();
    }

    private void StartProcess(string exePath)
    {
        _logger.LogInformation("Starting alignmesh_server: {Exe} {Port}", exePath, _opts.Port);

        _process = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = exePath,
                Arguments = _opts.Port.ToString(),
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true,
            },
            EnableRaisingEvents = true,
        };

        _process.OutputDataReceived += (_, e) =>
        {
            if (e.Data != null) _logger.LogDebug("[alignmesh] {Line}", e.Data);
        };
        _process.ErrorDataReceived += (_, e) =>
        {
            if (e.Data != null) _logger.LogWarning("[alignmesh-err] {Line}", e.Data);
        };

        _process.Start();
        _process.BeginOutputReadLine();
        _process.BeginErrorReadLine();

        _logger.LogInformation("alignmesh_server started (PID {Pid})", _process.Id);
    }

    private async Task WaitForHealthy(CancellationToken ct)
    {
        var deadline = DateTime.UtcNow.AddSeconds(_opts.StartupTimeoutSeconds);
        while (DateTime.UtcNow < deadline && !ct.IsCancellationRequested)
        {
            try
            {
                var resp = await _http.GetAsync("/health", ct);
                if (resp.IsSuccessStatusCode)
                {
                    _logger.LogInformation("alignmesh_server is healthy on port {Port}", _opts.Port);
                    return;
                }
            }
            catch
            {
                // Not ready yet
            }
            await Task.Delay(500, ct);
        }

        _logger.LogWarning("alignmesh_server did not become healthy within {Sec}s", _opts.StartupTimeoutSeconds);
    }

    private async Task MonitorLoop(string exePath, CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(5000, ct);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            if (_process is { HasExited: true })
            {
                _logger.LogWarning("alignmesh_server exited (code {Code}). Restarting...",
                    _process.ExitCode);
                _process.Dispose();
                _process = null;

                StartProcess(exePath);
                await WaitForHealthy(ct);
            }
        }
    }

    private void KillProcess()
    {
        if (_process is { HasExited: false })
        {
            try
            {
                _process.Kill(entireProcessTree: true);
                _logger.LogInformation("alignmesh_server stopped");
            }
            catch (Exception ex)
            {
                _logger.LogDebug("Error stopping alignmesh_server: {Msg}", ex.Message);
            }
        }
        _process?.Dispose();
        _process = null;
    }

    private static string? ResolveExecutable(string nameOrPath)
    {
        if (string.IsNullOrWhiteSpace(nameOrPath)) return null;
        if (File.Exists(nameOrPath)) return Path.GetFullPath(nameOrPath);

        // Check next to the running assembly
        var baseDir = AppContext.BaseDirectory;
        var candidate = Path.Combine(baseDir, nameOrPath);
        if (File.Exists(candidate)) return candidate;

        return null;
    }
}
