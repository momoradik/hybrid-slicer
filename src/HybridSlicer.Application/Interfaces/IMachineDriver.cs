namespace HybridSlicer.Application.Interfaces;

/// <summary>
/// Port for physical machine communication (TCP, serial, etc.).
/// Implementations are swappable per firmware (Marlin, Grbl, RepRap, custom).
/// </summary>
public interface IMachineDriver : IAsyncDisposable
{
    bool IsConnected { get; }

    Task ConnectAsync(string host, int port, string? password = null, CancellationToken cancellationToken = default);
    Task DisconnectAsync();

    Task SendCommandAsync(string gcode, CancellationToken cancellationToken = default);
    Task<string> SendCommandWithResponseAsync(string gcode, CancellationToken cancellationToken = default);

    /// <summary>Streams lines from a complete G-code file, respecting flow control.</summary>
    IAsyncEnumerable<MachineProgress> StreamFileAsync(
        string gcodePath,
        CancellationToken cancellationToken = default);

    event EventHandler<MachineStatusEvent>? StatusChanged;

    // ── Duet proxy support ──────────────────────────────────────────────
    string? ConnectedBaseUrl { get; }
    Task<string> ProxyGetAsync(string relativePath, CancellationToken ct = default);
    Task<byte[]> ProxyGetBytesAsync(string relativePath, CancellationToken ct = default);
    Task<string> ProxyPostAsync(string relativePath, byte[] body, string contentType, CancellationToken ct = default);
}

public sealed record MachineProgress(int LineNumber, int TotalLines, string LastResponse);

public sealed record MachineStatusEvent(
    string RawResponse,
    double? ExtruderTempDegC,
    double? BedTempDegC,
    string? Position,
    DateTime ReceivedAt);
