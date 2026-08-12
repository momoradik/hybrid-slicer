namespace HybridSlicer.Domain.Exceptions;

public class DomainException : Exception
{
    public string Code { get; }

    /// <summary>
    /// Name of the setting that caused the failure, in the exact casing the API/UI
    /// uses (e.g. "layerHeightMm"). Null when the failure isn't tied to one field.
    /// The UI uses this to point at the offending input instead of showing a
    /// generic "save failed" message.
    /// </summary>
    public string? Field { get; }

    /// <summary>The value the user actually supplied, rendered for display.</summary>
    public string? ProvidedValue { get; }

    /// <summary>Human-readable description of what the value must be.</summary>
    public string? Expected { get; }

    public DomainException(string code, string message) : base(message)
    {
        Code = code;
    }

    public DomainException(string code, string message, Exception inner)
        : base(message, inner)
    {
        Code = code;
    }

    public DomainException(
        string code,
        string message,
        string? field,
        string? providedValue = null,
        string? expected = null) : base(message)
    {
        Code = code;
        Field = field;
        ProvidedValue = providedValue;
        Expected = expected;
    }

    /// <summary>
    /// Builds a validation failure that names the field, echoes back what the user
    /// entered, and states the accepted range — everything needed to fix the input.
    /// </summary>
    public static DomainException Invalid(
        string code, string field, string label, object? provided, string expected)
        => new(code,
               $"{label} is invalid: you entered {Format(provided)}, but it must be {expected}.",
               field,
               Format(provided),
               expected);

    private static string Format(object? value) => value switch
    {
        null            => "(empty)",
        string s        => string.IsNullOrWhiteSpace(s) ? "(empty)" : $"\"{s}\"",
        double d        => d.ToString("0.####"),
        float f         => f.ToString("0.####"),
        _               => value.ToString() ?? "(empty)"
    };
}
