namespace HybridSlicer.Application.Interfaces;

/// <summary>
/// How the application reaches the board.
/// </summary>
public enum MachineTransport
{
    /// <summary>
    /// Over the network — the board's rr_* HTTP API. Whether the board is wired
    /// by Ethernet or joined over Wi-Fi makes no difference here: either way it
    /// has an IP address and speaks the same HTTP API.
    /// </summary>
    Network = 0,

    /// <summary>Over USB, as a virtual COM port, using RepRapFirmware's serial channel.</summary>
    Serial = 1,
}

/// <summary>
/// Chooses and owns the live connection. Kept separate from
/// <see cref="IMachineDriver"/> so that callers which only send G-code don't need
/// to know how the link was established.
/// </summary>
public interface IMachineConnectionManager
{
    MachineConnectionStatus Status { get; }

    Task ConnectNetworkAsync(string host, int port, string? password, CancellationToken ct = default);

    Task ConnectSerialAsync(string portName, int baudRate, CancellationToken ct = default);

    Task DisconnectAsync();

    /// <summary>COM ports currently present on this machine, best-effort.</summary>
    IReadOnlyList<string> ListSerialPorts();
}

/// <summary>Snapshot of the current link, for display.</summary>
public sealed record MachineConnectionStatus(
    bool IsConnected,
    MachineTransport? Transport,
    /// <summary>"192.168.1.20:80" for network, "COM3 @ 115200" for serial.</summary>
    string? Endpoint,
    /// <summary>Firmware name/version reported by the board, when known.</summary>
    string? FirmwareDescription,
    /// <summary>
    /// False when the transport cannot serve the board's file endpoints —
    /// true over the network, false over USB (RepRapFirmware exposes rr_filelist,
    /// rr_download and rr_upload over HTTP only).
    /// </summary>
    bool SupportsFileTransfer)
{
    public static readonly MachineConnectionStatus Disconnected =
        new(false, null, null, null, false);
}
