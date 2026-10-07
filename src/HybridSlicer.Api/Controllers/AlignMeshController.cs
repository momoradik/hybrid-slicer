using HybridSlicer.Infrastructure.AlignMesh;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

/// <summary>
/// Proxies HTTP requests to the alignmesh_server sidecar process.
/// This keeps the frontend on a single origin (no CORS) and lets the
/// .NET layer add logging, auth, and path translation.
/// </summary>
[ApiController]
[Route("api/alignmesh")]
public class AlignMeshController : ControllerBase
{
    private readonly AlignMeshProcessManager _manager;
    private readonly ILogger<AlignMeshController> _logger;

    public AlignMeshController(AlignMeshProcessManager manager, ILogger<AlignMeshController> logger)
    {
        _manager = manager;
        _logger = logger;
    }

    [HttpGet("{**path}")]
    public async Task<IActionResult> ProxyGet(string path, CancellationToken ct)
    {
        try
        {
            var qs = Request.QueryString.Value ?? "";
            var resp = await _manager.Http.GetAsync($"/{path}{qs}", ct);
            var body = await resp.Content.ReadAsStringAsync(ct);
            return Content(body, resp.Content.Headers.ContentType?.MediaType ?? "application/json");
        }
        catch (HttpRequestException ex)
        {
            _logger.LogWarning("alignmesh proxy GET /{Path} failed: {Msg}", path, ex.Message);
            return StatusCode(502, new { error = $"alignmesh_server is not reachable: {ex.Message}" });
        }
    }

    [HttpPost("{**path}")]
    [RequestSizeLimit(500_000_000)]
    public async Task<IActionResult> ProxyPost(string path, CancellationToken ct)
    {
        try
        {
            var qs = Request.QueryString.Value ?? "";
            var fullPath = $"/{path}{qs}";

            using var ms = new MemoryStream();
            await Request.Body.CopyToAsync(ms, ct);
            var body = ms.ToArray();
            var contentType = Request.ContentType ?? "application/octet-stream";

            var content = new ByteArrayContent(body);
            content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue(contentType);

            var resp = await _manager.Http.PostAsync(fullPath, content, ct);
            var respBody = await resp.Content.ReadAsByteArrayAsync(ct);
            var respContentType = resp.Content.Headers.ContentType?.MediaType ?? "application/octet-stream";

            return File(respBody, respContentType);
        }
        catch (HttpRequestException ex)
        {
            _logger.LogWarning("alignmesh proxy POST /{Path} failed: {Msg}", path, ex.Message);
            return StatusCode(502, new { error = $"alignmesh_server is not reachable: {ex.Message}" });
        }
    }
}
