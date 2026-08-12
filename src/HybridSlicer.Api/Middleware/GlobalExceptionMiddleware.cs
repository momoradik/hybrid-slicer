using System.Text.Json;
using HybridSlicer.Domain.Exceptions;

namespace HybridSlicer.Api.Middleware;

public sealed class GlobalExceptionMiddleware
{
    private readonly RequestDelegate _next;
    private readonly ILogger<GlobalExceptionMiddleware> _logger;

    public GlobalExceptionMiddleware(RequestDelegate next, ILogger<GlobalExceptionMiddleware> logger)
    {
        _next = next;
        _logger = logger;
    }

    public async Task InvokeAsync(HttpContext context)
    {
        try
        {
            await _next(context);
        }
        catch (DomainException ex)
        {
            _logger.LogWarning(ex, "Domain exception: {Code}", ex.Code);
            context.Response.StatusCode = ex.Code switch
            {
                "JOB_NOT_FOUND" or "MACHINE_NOT_FOUND" or "PROFILE_NOT_FOUND" or "TOOL_NOT_FOUND" => 404,
                "SAFETY_VIOLATION" => 422,
                _ => 400
            };
            await WriteJsonErrorAsync(context, ex.Code, ex.Message, ex.Field, ex.ProvidedValue, ex.Expected);
        }
        catch (OperationCanceledException)
        {
            context.Response.StatusCode = 499; // Client closed request
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Unhandled exception");
            context.Response.StatusCode = 500;
            await WriteJsonErrorAsync(context, "INTERNAL_ERROR", "An unexpected error occurred.");
        }
    }

    /// <summary>
    /// Error envelope. <c>title</c> and <c>detail</c> duplicate <c>message</c> so the
    /// payload also satisfies clients that read the ProblemDetails shape.
    /// </summary>
    private static async Task WriteJsonErrorAsync(
        HttpContext context, string code, string message,
        string? field = null, string? providedValue = null, string? expected = null)
    {
        context.Response.ContentType = "application/json";
        var body = JsonSerializer.Serialize(new
        {
            code,
            message,
            title = message,
            detail = message,
            field,
            providedValue,
            expected
        });
        await context.Response.WriteAsync(body);
    }
}
