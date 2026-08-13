using HybridSlicer.Application.Interfaces;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Machine;

/// <summary>
/// The single <see cref="IMachineDriver"/> the rest of the app talks to. It owns
/// whichever concrete driver matches the transport the user chose and forwards
/// every call to it.
///
/// This is what makes transport a runtime choice: previously the driver was fixed
/// at startup by DI registration, so switching between network and USB meant
/// editing ServiceExtensions and rebuilding. Controllers, the SignalR hub and the
/// status broadcaster keep their existing <see cref="IMachineDriver"/> dependency
/// and are unaffected by a switch.
/// </summary>
public sealed class MachineDriverRouter : IMachineDriver, IMachineConnectionManager
{
    private readonly ILogger<MachineDriverRouter> _logger;
    private readonly Func<RepRapHttpDriver> _httpFactory;
    private readonly Func<RepRapSerialDriver> _serialFactory;
    private readonly SemaphoreSlim _switchLock = new(1, 1);

    private IMachineDriver? _active;
    private MachineTransport? _transport;
    private string? _endpoint;

    public MachineDriverRouter(
        ILogger<MachineDriverRouter> logger,
        Func<RepRapHttpDriver> httpFactory,
        Func<RepRapSerialDriver> serialFactory)
    {
        _logger = logger;
        _httpFactory = httpFactory;
        _serialFactory = serialFactory;
    }

    // ── Connection management ──────────────────────────────────────────────

    public MachineConnectionStatus Status =>
        _active is { IsConnected: true }
            ? new MachineConnectionStatus(
                true,
                _transport,
                _endpoint,
                _active is RepRapSerialDriver s ? s.FirmwareDescription : "RepRapFirmware (HTTP)",
                SupportsFileTransfer: _transport == MachineTransport.Network)
            : MachineConnectionStatus.Disconnected;

    public IReadOnlyList<string> ListSerialPorts() => RepRapSerialDriver.ListPorts();

    public async Task ConnectNetworkAsync(string host, int port, string? password, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(host))
            throw new IOException("Enter the machine's IP address or hostname.");

        await _switchLock.WaitAsync(ct);
        try
        {
            await TearDownAsync();

            var driver = _httpFactory();
            await driver.ConnectAsync(host.Trim(), port, password, ct);

            _active = driver;
            _transport = MachineTransport.Network;
            _endpoint = port is 80 or 0 ? host.Trim() : $"{host.Trim()}:{port}";
            Resubscribe();
            _logger.LogInformation("Machine connected over network: {Endpoint}", _endpoint);
        }
        finally { _switchLock.Release(); }
    }

    public async Task ConnectSerialAsync(string portName, int baudRate, CancellationToken ct = default)
    {
        await _switchLock.WaitAsync(ct);
        try
        {
            await TearDownAsync();

            var driver = _serialFactory();
            await driver.ConnectSerialAsync(portName, baudRate, ct);

            _active = driver;
            _transport = MachineTransport.Serial;
            _endpoint = $"{portName} @ {baudRate}";
            Resubscribe();
            _logger.LogInformation("Machine connected over USB: {Endpoint}", _endpoint);
        }
        finally { _switchLock.Release(); }
    }

    public async Task DisconnectAsync()
    {
        await _switchLock.WaitAsync();
        try { await TearDownAsync(); }
        finally { _switchLock.Release(); }
    }

    /// <summary>Closes and disposes the active driver, if any. Caller holds the lock.</summary>
    private async Task TearDownAsync()
    {
        if (_active is null) return;

        _active.StatusChanged -= Forward;
        try { await _active.DisconnectAsync(); } catch { /* best-effort */ }
        try { await _active.DisposeAsync(); } catch { /* best-effort */ }

        _active = null;
        _transport = null;
        _endpoint = null;
    }

    private void Resubscribe()
    {
        if (_active is null) return;
        _active.StatusChanged -= Forward;
        _active.StatusChanged += Forward;
    }

    private void Forward(object? sender, MachineStatusEvent e) => StatusChanged?.Invoke(this, e);

    // ── IMachineDriver — delegate everything ───────────────────────────────

    public bool IsConnected => _active?.IsConnected ?? false;

    public event EventHandler<MachineStatusEvent>? StatusChanged;

    /// <summary>Kept for the existing hub signature; routes to the network transport.</summary>
    public Task ConnectAsync(string host, int port, string? password = null, CancellationToken ct = default)
        => ConnectNetworkAsync(host, port, password, ct);

    public Task SendCommandAsync(string gcode, CancellationToken ct = default)
        => Active.SendCommandAsync(gcode, ct);

    public Task<string> SendCommandWithResponseAsync(string gcode, CancellationToken ct = default)
        => Active.SendCommandWithResponseAsync(gcode, ct);

    public IAsyncEnumerable<MachineProgress> StreamFileAsync(string gcodePath, CancellationToken ct = default)
        => Active.StreamFileAsync(gcodePath, ct);

    public string? ConnectedBaseUrl => _active?.ConnectedBaseUrl;

    public Task<string> ProxyGetAsync(string relativePath, CancellationToken ct = default)
        => Active.ProxyGetAsync(relativePath, ct);

    public Task<byte[]> ProxyGetBytesAsync(string relativePath, CancellationToken ct = default)
        => Active.ProxyGetBytesAsync(relativePath, ct);

    public Task<string> ProxyPostAsync(string relativePath, byte[] body, string contentType, CancellationToken ct = default)
        => Active.ProxyPostAsync(relativePath, body, contentType, ct);

    private IMachineDriver Active =>
        _active ?? throw new InvalidOperationException(
            "Not connected to a machine. Open Printer → Connection and connect first.");

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _switchLock.Dispose();
    }
}
