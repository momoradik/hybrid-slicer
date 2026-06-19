using HybridSlicer.Application.Interfaces;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

/// <summary>
/// Proxies HTTP requests to the connected Duet (RepRapFirmware) board.
/// This avoids CORS issues when the frontend calls the Duet REST API.
/// </summary>
[ApiController]
[Route("api/duet")]
public class DuetProxyController : ControllerBase
{
    private readonly IMachineDriver _driver;

    public DuetProxyController(IMachineDriver driver) => _driver = driver;

    /// <summary>GET proxy — forwards any rr_* path to the Duet board.</summary>
    [HttpGet("{**path}")]
    public async Task<IActionResult> ProxyGet(string path, CancellationToken ct)
    {
        if (!_driver.IsConnected)
            return StatusCode(503, new { error = "Not connected to a Duet board" });

        var qs = Request.QueryString.Value ?? "";
        var fullPath = $"{path}{qs}";

        // Binary download for rr_download
        if (path.StartsWith("rr_download", StringComparison.OrdinalIgnoreCase))
        {
            var bytes = await _driver.ProxyGetBytesAsync(fullPath, ct);
            return File(bytes, "application/octet-stream");
        }

        var json = await _driver.ProxyGetAsync(fullPath, ct);
        return Content(json, "application/json");
    }

    /// <summary>POST proxy — used for rr_upload and similar.</summary>
    [HttpPost("{**path}")]
    [RequestSizeLimit(500_000_000)] // 500 MB max for G-code uploads
    public async Task<IActionResult> ProxyPost(string path, CancellationToken ct)
    {
        if (!_driver.IsConnected)
            return StatusCode(503, new { error = "Not connected to a Duet board" });

        var qs = Request.QueryString.Value ?? "";
        var fullPath = $"{path}{qs}";

        using var ms = new MemoryStream();
        await Request.Body.CopyToAsync(ms, ct);
        var body = ms.ToArray();
        var contentType = Request.ContentType ?? "application/octet-stream";

        var result = await _driver.ProxyPostAsync(fullPath, body, contentType, ct);
        return Content(result, "application/json");
    }
}
