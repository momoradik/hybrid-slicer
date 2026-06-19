using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;
using HybridSlicer.Application.Interfaces;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Machine;

/// <summary>
/// TCP-based machine driver compatible with Marlin / RepRap firmware.
/// Sends G-code lines and waits for "ok" acknowledgement before proceeding.
/// </summary>
public sealed class TcpMachineDriver : IMachineDriver
{
    private TcpClient? _client;
    private NetworkStream? _stream;
    private readonly ILogger<TcpMachineDriver> _logger;
    private readonly SemaphoreSlim _sendLock = new(1, 1);

    public bool IsConnected => _client?.Connected ?? false;

    public event EventHandler<MachineStatusEvent>? StatusChanged;

    public TcpMachineDriver(ILogger<TcpMachineDriver> logger) => _logger = logger;

    public async Task ConnectAsync(string host, int port, string? password = null, CancellationToken ct = default)
    {
        _client = new TcpClient { SendTimeout = 5000, ReceiveTimeout = 30000 };
        await _client.ConnectAsync(host, port, ct);
        _stream = _client.GetStream();
        _logger.LogInformation("Connected to machine at {Host}:{Port}", host, port);
    }

    public Task DisconnectAsync()
    {
        _stream?.Dispose();
        _client?.Dispose();
        _stream = null;
        _client = null;
        _logger.LogInformation("Disconnected from machine.");
        return Task.CompletedTask;
    }

    public async Task SendCommandAsync(string gcode, CancellationToken ct = default)
    {
        await _sendLock.WaitAsync(ct);
        try
        {
            EnsureConnected();
            var line = gcode.Trim() + "\n";
            var bytes = Encoding.ASCII.GetBytes(line);
            await _stream!.WriteAsync(bytes, ct);
            await WaitForOkAsync(ct);
        }
        finally
        {
            _sendLock.Release();
        }
    }

    public async Task<string> SendCommandWithResponseAsync(string gcode, CancellationToken ct = default)
    {
        await _sendLock.WaitAsync(ct);
        try
        {
            EnsureConnected();
            var line = gcode.Trim() + "\n";
            await _stream!.WriteAsync(Encoding.ASCII.GetBytes(line), ct);
            return await WaitForOkAsync(ct);
        }
        finally
        {
            _sendLock.Release();
        }
    }

    public async IAsyncEnumerable<MachineProgress> StreamFileAsync(
        string gcodePath,
        [EnumeratorCancellation] CancellationToken ct = default)
    {
        var lines = await File.ReadAllLinesAsync(gcodePath, ct);
        var total = lines.Length;

        for (var i = 0; i < total; i++)
        {
            ct.ThrowIfCancellationRequested();

            var line = lines[i].Trim();
            if (string.IsNullOrEmpty(line) || line.StartsWith(';')) continue;

            // Strip inline comments before sending
            var commentIdx = line.IndexOf(';');
            if (commentIdx > 0) line = line[..commentIdx].Trim();

            var response = await SendCommandWithResponseAsync(line, ct);

            RaiseStatusIfTemperature(response);

            yield return new MachineProgress(i + 1, total, response);
        }
    }

    private async Task<string> WaitForOkAsync(CancellationToken ct)
    {
        var sb = new StringBuilder();
        var buffer = new byte[256];

        while (true)
        {
            var read = await _stream!.ReadAsync(buffer, ct);
            if (read == 0) throw new IOException("Machine connection closed unexpectedly.");

            var chunk = Encoding.ASCII.GetString(buffer, 0, read);
            sb.Append(chunk);

            var accumulated = sb.ToString();
            if (accumulated.Contains("ok", StringComparison.OrdinalIgnoreCase))
                return accumulated.Trim();

            if (accumulated.Contains("error", StringComparison.OrdinalIgnoreCase))
                throw new IOException($"Machine returned error: {accumulated.Trim()}");
        }
    }

    private void RaiseStatusIfTemperature(string response)
    {
        // Marlin temperature response: T:210.0 /210.0 B:60.0 /60.0
        double? extTemp = null, bedTemp = null;

        var tIdx = response.IndexOf("T:", StringComparison.OrdinalIgnoreCase);
        if (tIdx >= 0)
        {
            var slice = response[(tIdx + 2)..].Split(' ')[0];
            double.TryParse(slice, out var t);
            extTemp = t;
        }

        var bIdx = response.IndexOf("B:", StringComparison.OrdinalIgnoreCase);
        if (bIdx >= 0)
        {
            var slice = response[(bIdx + 2)..].Split(' ')[0];
            double.TryParse(slice, out var b);
            bedTemp = b;
        }

        StatusChanged?.Invoke(this, new MachineStatusEvent(response, extTemp, bedTemp, null, DateTime.UtcNow));
    }

    // ── Duet proxy (not supported over TCP) ────────────────────────────
    public string? ConnectedBaseUrl => null;
    public Task<string> ProxyGetAsync(string relativePath, CancellationToken ct = default)
        => throw new NotSupportedException("Duet proxy requires HTTP driver");
    public Task<byte[]> ProxyGetBytesAsync(string relativePath, CancellationToken ct = default)
        => throw new NotSupportedException("Duet proxy requires HTTP driver");
    public Task<string> ProxyPostAsync(string relativePath, byte[] body, string contentType, CancellationToken ct = default)
        => throw new NotSupportedException("Duet proxy requires HTTP driver");

    private void EnsureConnected()
    {
        if (!IsConnected)
            throw new InvalidOperationException("Not connected to machine. Call ConnectAsync first.");
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _sendLock.Dispose();
    }
}
