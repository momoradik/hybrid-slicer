using System.Collections.Concurrent;
using System.IO.Ports;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using System.Web;
using HybridSlicer.Application.Interfaces;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Machine;

/// <summary>
/// Machine driver for RepRapFirmware (Duet boards) over USB, i.e. a virtual COM port.
///
/// The front end talks to the board through the rr_* HTTP shapes, so this driver
/// translates those into the serial equivalents:
///
///   rr_model?key=K&amp;flags=F  ->  M409 K"K" F"F"   (returns the same {key,flags,result} JSON)
///   rr_status?type=N          ->  M408 S{N}
///   rr_gcode?gcode=X          ->  write X, reply cached for rr_reply
///   rr_reply                  ->  the cached reply
///
/// The file endpoints (rr_filelist, rr_fileinfo, rr_download, rr_upload, rr_delete,
/// rr_mkdir, rr_move) have no serial equivalent — RepRapFirmware serves those over
/// HTTP only — so they fail with a clear message rather than a confusing timeout.
/// </summary>
public sealed class RepRapSerialDriver : IMachineDriver
{
    private const int ReplyTimeoutMs = 8000;

    private readonly ILogger<RepRapSerialDriver> _logger;
    private readonly SemaphoreSlim _sendLock = new(1, 1);

    private SerialPort? _port;
    private string? _lastReply;

    public bool IsConnected { get; private set; }
    public string? FirmwareDescription { get; private set; }
    public string? PortName => _port?.PortName;
    public int BaudRate => _port?.BaudRate ?? 0;

    public event EventHandler<MachineStatusEvent>? StatusChanged;

    public RepRapSerialDriver(ILogger<RepRapSerialDriver> logger) => _logger = logger;

    public static IReadOnlyList<string> ListPorts()
    {
        try { return SerialPort.GetPortNames().Distinct().OrderBy(p => p).ToList(); }
        catch { return []; }
    }

    // ── Connection ─────────────────────────────────────────────────────────

    public Task ConnectAsync(string host, int port, string? password = null, CancellationToken ct = default)
        => ConnectSerialAsync(host, port <= 0 ? 115200 : port, ct);

    /// <param name="portName">e.g. "COM3".</param>
    /// <param name="baudRate">RepRapFirmware's USB channel ignores the rate, but the OS still requires one.</param>
    public async Task ConnectSerialAsync(string portName, int baudRate, CancellationToken ct = default)
    {
        if (string.IsNullOrWhiteSpace(portName))
            throw new IOException("No COM port selected.");

        var available = ListPorts();
        if (available.Count > 0 &&
            !available.Contains(portName, StringComparer.OrdinalIgnoreCase))
        {
            throw new IOException(
                $"{portName} is not present on this computer. " +
                $"Available ports: {string.Join(", ", available)}. " +
                "Check the USB cable and that the board is powered.");
        }

        var sp = new SerialPort(portName, baudRate)
        {
            NewLine = "\n",
            ReadTimeout = ReplyTimeoutMs,
            WriteTimeout = ReplyTimeoutMs,
            DtrEnable = true,   // Duet enumerates its USB CDC port with DTR asserted
            RtsEnable = true,
            Encoding = Encoding.ASCII,
        };

        try
        {
            sp.Open();
        }
        catch (UnauthorizedAccessException ex)
        {
            sp.Dispose();
            throw new IOException(
                $"{portName} is already in use. Close any other program connected to the board " +
                "(Duet Web Control over USB, a terminal, or PrusaSlicer) and try again.", ex);
        }
        catch (Exception ex)
        {
            sp.Dispose();
            throw new IOException($"Could not open {portName}: {ex.Message}", ex);
        }

        _port = sp;
        _port.DiscardInBuffer();
        _port.DiscardOutBuffer();

        // Confirm something is actually answering before reporting success — an open
        // COM port proves nothing on its own.
        try
        {
            var banner = await WriteAndReadAsync("M115", ct);
            if (string.IsNullOrWhiteSpace(banner))
                throw new IOException(
                    $"{portName} opened but the board did not answer M115. " +
                    "Check that this is the Duet's port and that the board is powered.");

            FirmwareDescription = ExtractFirmware(banner);

            if (banner.Contains("FIRMWARE_NAME", StringComparison.OrdinalIgnoreCase) &&
                !banner.Contains("RepRapFirmware", StringComparison.OrdinalIgnoreCase))
            {
                _logger.LogWarning(
                    "Board on {Port} does not report RepRapFirmware: {Banner}", portName, banner.Trim());
            }
        }
        catch
        {
            try { sp.Close(); } catch { /* ignore */ }
            sp.Dispose();
            _port = null;
            throw;
        }

        IsConnected = true;
        _logger.LogInformation(
            "Connected to {Port} @ {Baud} ({Firmware})", portName, baudRate, FirmwareDescription ?? "unknown firmware");
    }

    public Task DisconnectAsync()
    {
        IsConnected = false;
        FirmwareDescription = null;
        if (_port is not null)
        {
            try { if (_port.IsOpen) _port.Close(); } catch { /* best-effort */ }
            _port.Dispose();
            _port = null;
        }
        _logger.LogInformation("Serial connection closed.");
        return Task.CompletedTask;
    }

    // ── G-code ─────────────────────────────────────────────────────────────

    public async Task SendCommandAsync(string gcode, CancellationToken ct = default)
        => await SendCommandWithResponseAsync(gcode, ct);

    public async Task<string> SendCommandWithResponseAsync(string gcode, CancellationToken ct = default)
    {
        EnsureConnected();
        await _sendLock.WaitAsync(ct);
        try
        {
            var reply = await WriteAndReadAsync(gcode.Trim(), ct);
            _lastReply = reply;
            RaiseStatusIfTemperature(reply);
            return reply;
        }
        finally { _sendLock.Release(); }
    }

    public async IAsyncEnumerable<MachineProgress> StreamFileAsync(
        string gcodePath, [EnumeratorCancellation] CancellationToken ct = default)
    {
        var lines = await File.ReadAllLinesAsync(gcodePath, ct);
        var total = lines.Length;

        for (var i = 0; i < total; i++)
        {
            ct.ThrowIfCancellationRequested();
            var line = lines[i].Trim();
            if (string.IsNullOrEmpty(line) || line.StartsWith(';')) continue;
            var comment = line.IndexOf(';');
            if (comment > 0) line = line[..comment].Trim();
            if (line.Length == 0) continue;

            var response = await SendCommandWithResponseAsync(line, ct);
            yield return new MachineProgress(i + 1, total, response);
        }
    }

    // ── rr_* translation ───────────────────────────────────────────────────

    public string? ConnectedBaseUrl => IsConnected ? $"serial:{_port?.PortName}" : null;

    public async Task<string> ProxyGetAsync(string relativePath, CancellationToken ct = default)
    {
        EnsureConnected();

        var (path, query) = SplitPath(relativePath);

        switch (path.ToLowerInvariant())
        {
            case "rr_model":
            {
                var key   = query["key"] ?? "";
                var flags = query["flags"] ?? "";
                // M409 answers with exactly the {"key":…,"flags":…,"result":…} envelope
                // that rr_model returns over HTTP, so it can be passed straight through.
                var cmd = string.IsNullOrEmpty(flags)
                    ? $"M409 K\"{key}\""
                    : $"M409 K\"{key}\" F\"{flags}\"";
                return ExtractJson(await SendCommandWithResponseAsync(cmd, ct))
                       ?? throw new IOException($"Board returned no object model for key '{key}'.");
            }

            case "rr_status":
            {
                var type = query["type"] ?? "1";
                return ExtractJson(await SendCommandWithResponseAsync($"M408 S{type}", ct))
                       ?? throw new IOException("Board returned no status.");
            }

            case "rr_gcode":
            {
                var gcode = query["gcode"] ?? "";
                _lastReply = await SendCommandWithResponseAsync(gcode, ct);
                // DWC expects a remaining-buffer figure; the serial path is synchronous
                // (we already waited for "ok"), so report a healthy buffer.
                return "{\"buff\":255}";
            }

            case "rr_reply":
                return _lastReply ?? "";

            case "rr_connect":
                return "{\"err\":0}";

            case "rr_disconnect":
                return "{\"err\":0}";

            default:
                throw new NotSupportedException(FileEndpointMessage(path));
        }
    }

    public Task<byte[]> ProxyGetBytesAsync(string relativePath, CancellationToken ct = default)
    {
        EnsureConnected();
        var (path, _) = SplitPath(relativePath);
        throw new NotSupportedException(FileEndpointMessage(path));
    }

    public Task<string> ProxyPostAsync(string relativePath, byte[] body, string contentType, CancellationToken ct = default)
    {
        EnsureConnected();
        var (path, _) = SplitPath(relativePath);
        throw new NotSupportedException(FileEndpointMessage(path));
    }

    private static string FileEndpointMessage(string path) =>
        $"'{path}' is only available over a network connection. " +
        "RepRapFirmware serves file listing, upload and download over its HTTP interface, " +
        "which USB does not provide. Connect the board by IP address to use this feature.";

    // ── Serial plumbing ────────────────────────────────────────────────────

    /// <summary>
    /// Writes one line and collects the reply until the board answers "ok"
    /// (or an error), which is how RepRapFirmware terminates a serial response.
    /// </summary>
    private async Task<string> WriteAndReadAsync(string command, CancellationToken ct)
    {
        var port = _port ?? throw new InvalidOperationException("Serial port is not open.");

        port.DiscardInBuffer();

        try
        {
            port.WriteLine(command);
        }
        catch (TimeoutException ex)
        {
            // The port opened but the write never drained — the device on the other
            // end isn't reading. Typically a COM port belonging to something that is
            // not the printer (Bluetooth, a modem, a virtual port), or a cable that
            // carries power but no data. Surface that instead of a bare timeout.
            throw new IOException(
                $"{port.PortName} accepted the connection but did not respond. " +
                "That port is probably not the printer, or the USB cable is charge-only. " +
                "Pick a different port, or use a data-capable cable.", ex);
        }

        var sb = new StringBuilder();
        var deadline = DateTime.UtcNow.AddMilliseconds(ReplyTimeoutMs);

        while (DateTime.UtcNow < deadline)
        {
            ct.ThrowIfCancellationRequested();

            string? line = null;
            try { line = port.ReadLine(); }
            catch (TimeoutException) { break; }

            if (line is null) continue;
            var trimmed = line.Trim();

            // "ok" terminates the reply and is not part of it.
            if (trimmed.Equals("ok", StringComparison.OrdinalIgnoreCase)) break;
            if (trimmed.StartsWith("ok ", StringComparison.OrdinalIgnoreCase))
            {
                sb.AppendLine(trimmed[3..]);
                break;
            }

            sb.AppendLine(trimmed);

            if (trimmed.StartsWith("Error", StringComparison.OrdinalIgnoreCase) ||
                trimmed.StartsWith("!!", StringComparison.Ordinal))
                break;

            await Task.Yield();
        }

        return sb.ToString().Trim();
    }

    /// <summary>Pulls the first balanced JSON object out of a reply, ignoring any chatter around it.</summary>
    private static string? ExtractJson(string reply)
    {
        if (string.IsNullOrWhiteSpace(reply)) return null;

        var start = reply.IndexOf('{');
        if (start < 0) return null;

        var depth = 0;
        var inString = false;
        var escaped = false;

        for (var i = start; i < reply.Length; i++)
        {
            var c = reply[i];

            if (escaped) { escaped = false; continue; }
            if (c == '\\' && inString) { escaped = true; continue; }
            if (c == '"') { inString = !inString; continue; }
            if (inString) continue;

            if (c == '{') depth++;
            else if (c == '}')
            {
                depth--;
                if (depth == 0)
                {
                    var candidate = reply[start..(i + 1)];
                    try { using var _ = JsonDocument.Parse(candidate); return candidate; }
                    catch (JsonException) { return null; }
                }
            }
        }
        return null;
    }

    private static (string path, System.Collections.Specialized.NameValueCollection query) SplitPath(string relativePath)
    {
        var q = relativePath.IndexOf('?');
        return q < 0
            ? (relativePath, HttpUtility.ParseQueryString(""))
            : (relativePath[..q], HttpUtility.ParseQueryString(relativePath[(q + 1)..]));
    }

    private static string? ExtractFirmware(string banner)
    {
        var idx = banner.IndexOf("FIRMWARE_NAME:", StringComparison.OrdinalIgnoreCase);
        if (idx < 0) return banner.Split('\n').FirstOrDefault()?.Trim();

        var rest = banner[(idx + "FIRMWARE_NAME:".Length)..];
        var end = rest.IndexOf("ELECTRONICS", StringComparison.OrdinalIgnoreCase);
        return (end > 0 ? rest[..end] : rest).Split('\n')[0].Trim();
    }

    private void RaiseStatusIfTemperature(string reply)
    {
        double? ext = null, bed = null;

        var t = reply.IndexOf("T:", StringComparison.OrdinalIgnoreCase);
        if (t >= 0 && double.TryParse(reply[(t + 2)..].Split([' ', '\r', '\n', '/'], 2)[0], out var tv)) ext = tv;

        var b = reply.IndexOf("B:", StringComparison.OrdinalIgnoreCase);
        if (b >= 0 && double.TryParse(reply[(b + 2)..].Split([' ', '\r', '\n', '/'], 2)[0], out var bv)) bed = bv;

        if (ext.HasValue || bed.HasValue)
            StatusChanged?.Invoke(this, new MachineStatusEvent(reply, ext, bed, null, DateTime.UtcNow));
    }

    private void EnsureConnected()
    {
        if (!IsConnected || _port is null || !_port.IsOpen)
            throw new InvalidOperationException("Not connected to the board over USB.");
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _sendLock.Dispose();
    }
}
