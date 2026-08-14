using HybridSlicer.Application.Interfaces;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

/// <summary>
/// Establishes and reports the live link to the printer's controller board.
///
/// Exposed over REST (rather than only through the SignalR hub) so the Printer
/// page can connect, poll status and recover after a page reload without needing
/// a live hub session first.
/// </summary>
[ApiController]
[Route("api/machine")]
public sealed class MachineConnectionController : ControllerBase
{
    private readonly IMachineConnectionManager _connection;
    private readonly ILogger<MachineConnectionController> _logger;

    public MachineConnectionController(
        IMachineConnectionManager connection,
        ILogger<MachineConnectionController> logger)
    {
        _connection = connection;
        _logger = logger;
    }

    /// <summary>Current link state — what the Printer page renders from.</summary>
    [HttpGet("connection")]
    public IActionResult GetConnection() => Ok(_connection.Status);

    /// <summary>COM ports present on this computer, for the USB dropdown.</summary>
    [HttpGet("serial-ports")]
    public IActionResult GetSerialPorts() => Ok(new { ports = _connection.ListSerialPorts() });

    /// <summary>
    /// Firmware options. Only RepRapFirmware is wired up; the rest are listed so the
    /// choice is visible, and are rejected with an explanation rather than failing
    /// obscurely at connect time.
    /// </summary>
    [HttpGet("firmwares")]
    public IActionResult GetFirmwares() => Ok(new
    {
        firmwares = new object[]
        {
            new { id = "RepRapFirmware", name = "RepRapFirmware (Duet)", supported = true,
                  note = "Duet 2 / Duet 3 and other RepRapFirmware boards." },
            new { id = "Marlin",         name = "Marlin",               supported = false,
                  note = "Not implemented yet." },
            new { id = "Klipper",        name = "Klipper (Moonraker)",  supported = false,
                  note = "Not implemented yet." },
            new { id = "Smoothieware",   name = "Smoothieware",         supported = false,
                  note = "Not implemented yet." },
        }
    });

    [HttpPost("connect")]
    public async Task<IActionResult> Connect([FromBody] ConnectRequest req, CancellationToken ct)
    {
        if (!string.Equals(req.Firmware, "RepRapFirmware", StringComparison.OrdinalIgnoreCase)
            && !string.IsNullOrWhiteSpace(req.Firmware))
        {
            return BadRequest(new
            {
                code = "FIRMWARE_NOT_SUPPORTED",
                message = $"{req.Firmware} is not supported yet — only RepRapFirmware (Duet) is implemented. " +
                          "Select RepRapFirmware to connect.",
                field = "firmware",
                providedValue = req.Firmware,
                expected = "RepRapFirmware",
            });
        }

        try
        {
            if (req.Transport == MachineTransport.Serial)
                await _connection.ConnectSerialAsync(req.PortName ?? "", req.BaudRate <= 0 ? 115200 : req.BaudRate, ct);
            else
                await _connection.ConnectNetworkAsync(req.Host ?? "", req.Port <= 0 ? 80 : req.Port, req.Password, ct);

            return Ok(_connection.Status);
        }
        // Connecting to hardware fails in many ways — a wrong port, an unplugged
        // board, a busy port, a timeout. All of them are the user's problem to fix,
        // not server faults, so report the reason rather than letting the generic
        // 500 handler swallow it.
        catch (Exception ex) when (ex is IOException or InvalidOperationException
                                      or NotSupportedException or TimeoutException
                                      or UnauthorizedAccessException or ArgumentException)
        {
            // These carry a message written for the user (wrong port, board not
            // answering, port already in use); pass it through verbatim.
            _logger.LogWarning("Connect failed: {Message}", ex.Message);
            return BadRequest(new
            {
                code = "CONNECT_FAILED",
                message = ex.Message,
                field = req.Transport == MachineTransport.Serial ? "portName" : "host",
                providedValue = req.Transport == MachineTransport.Serial ? req.PortName : req.Host,
                expected = (string?)null,
            });
        }
    }

    [HttpPost("disconnect")]
    public async Task<IActionResult> Disconnect()
    {
        await _connection.DisconnectAsync();
        return Ok(_connection.Status);
    }
}

public sealed record ConnectRequest(
    MachineTransport Transport = MachineTransport.Network,
    string? Firmware = "RepRapFirmware",
    // Network
    string? Host = null,
    int Port = 80,
    string? Password = null,
    // Serial
    string? PortName = null,
    int BaudRate = 115200);
