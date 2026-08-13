using HybridSlicer.Application.Interfaces;
using Microsoft.AspNetCore.SignalR;

namespace HybridSlicer.Api.Hubs;

/// <summary>
/// SignalR hub for real-time machine communication.
/// Status broadcasting is handled by MachineStatusBroadcaster (IHostedService).
/// </summary>
public sealed class MachineHub : Hub
{
    private readonly IMachineDriver _driver;
    private readonly IMachineConnectionManager _connection;
    private readonly ILogger<MachineHub> _logger;

    public MachineHub(
        IMachineDriver driver,
        IMachineConnectionManager connection,
        ILogger<MachineHub> logger)
    {
        _driver = driver;
        _connection = connection;
        _logger = logger;
    }

    /// <summary>
    /// Connect to a RepRapFirmware board over the network. The board's link —
    /// Ethernet cable or Wi-Fi — makes no difference; both give it an IP address.
    /// Password is optional.
    /// </summary>
    public async Task Connect(string host, int port, string? password = null)
    {
        _logger.LogInformation("Hub: Connect (network) {Host}:{Port}", host, port);
        await _connection.ConnectNetworkAsync(host, port, password, Context.ConnectionAborted);
        await Clients.Caller.SendAsync("Connected", _connection.Status);
    }

    /// <summary>Connect to a RepRapFirmware board over USB, as a virtual COM port.</summary>
    public async Task ConnectSerial(string portName, int baudRate)
    {
        _logger.LogInformation("Hub: Connect (serial) {Port} @ {Baud}", portName, baudRate);
        await _connection.ConnectSerialAsync(portName, baudRate <= 0 ? 115200 : baudRate, Context.ConnectionAborted);
        await Clients.Caller.SendAsync("Connected", _connection.Status);
    }

    public async Task Disconnect()
    {
        await _connection.DisconnectAsync();
        await Clients.Caller.SendAsync("Disconnected");
    }

    /// <summary>Current link state, so a client that joins late can render the right thing.</summary>
    public MachineConnectionStatus GetStatus() => _connection.Status;

    public async Task SendManualCommand(string gcode)
    {
        if (string.IsNullOrWhiteSpace(gcode)) return;
        _logger.LogInformation("Hub: Manual command '{Cmd}'", gcode);
        var response = await _driver.SendCommandWithResponseAsync(gcode, Context.ConnectionAborted);
        await Clients.Caller.SendAsync("CommandResponse", response);
    }

    public async IAsyncEnumerable<object> StreamJob(string gcodePath)
    {
        await foreach (var progress in _driver.StreamFileAsync(gcodePath, Context.ConnectionAborted))
        {
            yield return new
            {
                progress.LineNumber,
                progress.TotalLines,
                progress.LastResponse,
                PercentComplete = (double)progress.LineNumber / progress.TotalLines * 100,
            };
        }
    }
}
