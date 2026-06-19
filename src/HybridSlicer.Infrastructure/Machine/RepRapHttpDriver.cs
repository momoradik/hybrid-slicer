using System.Net;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using HybridSlicer.Application.Interfaces;
using Microsoft.Extensions.Logging;

namespace HybridSlicer.Infrastructure.Machine;

/// <summary>
/// Machine driver for RepRapFirmware (Duet boards) over HTTP.
/// Uses the rr_* REST API: rr_connect, rr_gcode, rr_reply, rr_status, rr_disconnect.
/// </summary>
public sealed class RepRapHttpDriver : IMachineDriver
{
    private readonly ILogger<RepRapHttpDriver> _logger;
    private readonly HttpClient _http;
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private string? _baseUrl;

    public bool IsConnected { get; private set; }

    public event EventHandler<MachineStatusEvent>? StatusChanged;

    public RepRapHttpDriver(ILogger<RepRapHttpDriver> logger)
    {
        _logger = logger;
        var handler = new HttpClientHandler
        {
            UseCookies = true,
            CookieContainer = new CookieContainer(),
            // Accept self-signed certs on local network printers
            ServerCertificateCustomValidationCallback = (_, _, _, _) => true,
        };
        _http = new HttpClient(handler)
        {
            Timeout = TimeSpan.FromSeconds(15),
        };
    }

    // ── Connection ─────────────────────────────────────────────────────────

    public async Task ConnectAsync(string host, int port, string? password = null, CancellationToken ct = default)
    {
        // RepRapFirmware serves on port 80 by default; port 80 is the standard HTTP default.
        _baseUrl = port is 80 or 0
            ? $"http://{host}"
            : $"http://{host}:{port}";

        var pwd = Uri.EscapeDataString(password ?? "");
        var timestamp = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        var url = $"{_baseUrl}/rr_connect?password={pwd}&time={timestamp}";

        _logger.LogInformation("RepRap: connecting to {Url}", url);

        HttpResponseMessage resp;
        try
        {
            resp = await _http.GetAsync(url, ct);
        }
        catch (Exception ex)
        {
            throw new IOException(
                $"Cannot reach RepRapFirmware at {_baseUrl}. " +
                $"Check the machine IP and that it is powered on. ({ex.Message})", ex);
        }

        if (!resp.IsSuccessStatusCode)
            throw new IOException($"HTTP {resp.StatusCode} from {_baseUrl}. Is this a RepRapFirmware machine?");

        var json = await resp.Content.ReadAsStringAsync(ct);
        _logger.LogDebug("RepRap rr_connect response: {Json}", json);

        try
        {
            using var doc = JsonDocument.Parse(json);
            var err = doc.RootElement.GetProperty("err").GetInt32();
            if (err == 1)
                throw new IOException("RepRapFirmware rejected the connection: wrong password.");
            if (err == 2)
                throw new IOException("RepRapFirmware rejected the connection: too many sessions.");
            if (err != 0)
                throw new IOException($"RepRapFirmware connection error code {err}.");
        }
        catch (JsonException)
        {
            throw new IOException($"Unexpected response from {_baseUrl}: {json}");
        }

        IsConnected = true;
        _logger.LogInformation("Connected to RepRapFirmware at {BaseUrl}", _baseUrl);
    }

    public async Task DisconnectAsync()
    {
        IsConnected = false;
        if (_baseUrl is not null)
        {
            try { await _http.GetAsync($"{_baseUrl}/rr_disconnect"); }
            catch { /* best-effort */ }
        }
        _baseUrl = null;
        _logger.LogInformation("Disconnected from RepRapFirmware.");
    }

    // ── G-code commands ────────────────────────────────────────────────────

    public async Task SendCommandAsync(string gcode, CancellationToken ct = default)
        => await SendCommandWithResponseAsync(gcode, ct);

    public async Task<string> SendCommandWithResponseAsync(string gcode, CancellationToken ct = default)
    {
        EnsureConnected();
        await _sendLock.WaitAsync(ct);
        try
        {
            var encoded = Uri.EscapeDataString(gcode.Trim());
            await _http.GetAsync($"{_baseUrl}/rr_gcode?gcode={encoded}", ct);

            // Short delay to give firmware time to process the command
            await Task.Delay(80, ct);

            var replyResp = await _http.GetAsync($"{_baseUrl}/rr_reply", ct);
            var reply = await replyResp.Content.ReadAsStringAsync(ct);
            reply = reply.Trim();

            _logger.LogDebug("RepRap cmd={Cmd} reply={Reply}", gcode.Trim(), reply);

            RaiseStatusIfTemperature(reply);
            return reply;
        }
        finally
        {
            _sendLock.Release();
        }
    }

    // ── File streaming ─────────────────────────────────────────────────────

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
            var commentIdx = line.IndexOf(';');
            if (commentIdx > 0) line = line[..commentIdx].Trim();

            var response = await SendCommandWithResponseAsync(line, ct);
            yield return new MachineProgress(i + 1, total, response);
        }
    }

    // ── Status polling ─────────────────────────────────────────────────────

    /// <summary>
    /// Poll RepRapFirmware status endpoint (type=1 = basic status).
    /// Call this periodically from a background loop to push temperature events.
    /// </summary>
    public async Task PollStatusAsync(CancellationToken ct = default)
    {
        if (!IsConnected || _baseUrl is null) return;
        try
        {
            var resp = await _http.GetAsync($"{_baseUrl}/rr_status?type=1", ct);
            var json = await resp.Content.ReadAsStringAsync(ct);
            ParseAndRaiseStatus(json);
        }
        catch (Exception ex)
        {
            _logger.LogWarning("RepRap status poll failed: {Msg}", ex.Message);
        }
    }

    // ── Private helpers ────────────────────────────────────────────────────

    private void RaiseStatusIfTemperature(string reply)
    {
        // Marlin-style temperature strings in RRF replies, e.g. "T:210.0 B:60.0"
        double? extTemp = null, bedTemp = null;

        var tIdx = reply.IndexOf("T:", StringComparison.OrdinalIgnoreCase);
        if (tIdx >= 0 && double.TryParse(reply[(tIdx + 2)..].Split([' ', '\r', '\n'], 2)[0], out var t))
            extTemp = t;

        var bIdx = reply.IndexOf("B:", StringComparison.OrdinalIgnoreCase);
        if (bIdx >= 0 && double.TryParse(reply[(bIdx + 2)..].Split([' ', '\r', '\n'], 2)[0], out var b))
            bedTemp = b;

        if (extTemp.HasValue || bedTemp.HasValue)
            StatusChanged?.Invoke(this, new MachineStatusEvent(reply, extTemp, bedTemp, null, DateTime.UtcNow));
    }

    private void ParseAndRaiseStatus(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var root = doc.RootElement;

            double? extTemp = null, bedTemp = null;

            // RRF status JSON has "temps" → "current": [bed, extruder0, ...]
            if (root.TryGetProperty("temps", out var temps))
            {
                if (temps.TryGetProperty("bed", out var bed) &&
                    bed.TryGetProperty("current", out var bedCur))
                    bedTemp = bedCur.GetDouble();

                if (temps.TryGetProperty("current", out var cur) && cur.GetArrayLength() > 1)
                    extTemp = cur[1].GetDouble();
            }

            StatusChanged?.Invoke(this,
                new MachineStatusEvent(json, extTemp, bedTemp, null, DateTime.UtcNow));
        }
        catch
        {
            // JSON may not always be valid during status polling
        }
    }

    // ── Duet proxy support ────────────────────────────────────────────

    public string? ConnectedBaseUrl => _baseUrl;

    public async Task<string> ProxyGetAsync(string relativePath, CancellationToken ct = default)
    {
        EnsureConnected();
        var resp = await _http.GetAsync($"{_baseUrl}/{relativePath}", ct);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync(ct);
    }

    public async Task<byte[]> ProxyGetBytesAsync(string relativePath, CancellationToken ct = default)
    {
        EnsureConnected();
        var resp = await _http.GetAsync($"{_baseUrl}/{relativePath}", ct);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsByteArrayAsync(ct);
    }

    public async Task<string> ProxyPostAsync(string relativePath, byte[] body, string contentType, CancellationToken ct = default)
    {
        EnsureConnected();
        var content = new ByteArrayContent(body);
        content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue(contentType);
        var resp = await _http.PostAsync($"{_baseUrl}/{relativePath}", content, ct);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync(ct);
    }

    private void EnsureConnected()
    {
        if (!IsConnected)
            throw new InvalidOperationException("Not connected to machine. Call ConnectAsync first.");
    }

    public async ValueTask DisposeAsync()
    {
        await DisconnectAsync();
        _http.Dispose();
        _sendLock.Dispose();
    }
}
