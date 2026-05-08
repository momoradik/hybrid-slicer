using System.Text;
using System.Text.RegularExpressions;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;

namespace HybridSlicer.Application.Common;

/// <summary>
/// Centralised injector for the four "wrapper" custom G-code triggers that frame
/// every output file: <see cref="GCodeTrigger.JobStart"/>, <see cref="GCodeTrigger.BeforePrinting"/>,
/// <see cref="GCodeTrigger.AfterPrinting"/>, <see cref="GCodeTrigger.JobEnd"/>.
///
/// Each emitted block is bracketed by HSCB markers (HybridSlicer Custom Block) so a
/// later pipeline stage can <see cref="Strip"/> them and re-wrap the file without
/// duplicating user content.
///
/// Per-machining (Before/AfterMachining) and per-extruder triggers are NOT handled here —
/// they are emitted in-line by the orchestrator and the multi-extruder emitter respectively,
/// because their position is structural rather than file-bookend.
/// </summary>
public static partial class CustomGCodeBlockApplier
{
    private const string StartPrefix = "; HSCB-START";
    private const string EndMarker   = "; HSCB-END";

    [GeneratedRegex(@"^; HSCB-START[^\r\n]*\r?\n[\s\S]*?^; HSCB-END[^\r\n]*\r?\n?",
        RegexOptions.Multiline)]
    private static partial Regex HscbBlockRegex();

    /// <summary>
    /// Returns <paramref name="content"/> wrapped with all enabled blocks for the four
    /// bookend triggers. Idempotent: any pre-existing HSCB blocks in <paramref name="content"/>
    /// are removed first so the result has each block exactly once.
    ///
    /// Order at top:    JobStart → BeforePrinting → [content].
    /// Order at bottom: [content] → AfterPrinting → JobEnd.
    /// </summary>
    public static string ApplyWrappers(string content, IReadOnlyList<CustomGCodeBlock> blocks)
    {
        var stripped = Strip(content);

        var sb = new StringBuilder(stripped.Length + 512);
        RenderTrigger(sb, blocks, GCodeTrigger.JobStart);
        RenderTrigger(sb, blocks, GCodeTrigger.BeforePrinting);
        sb.Append(stripped);
        if (!stripped.EndsWith('\n')) sb.AppendLine();
        RenderTrigger(sb, blocks, GCodeTrigger.AfterPrinting);
        RenderTrigger(sb, blocks, GCodeTrigger.JobEnd);
        return sb.ToString();
    }

    /// <summary>
    /// Removes every HSCB-marked block (markers and inner content) from <paramref name="content"/>.
    /// Used by the hybrid orchestrator to discard wrappers that were previously baked into
    /// <c>print.gcode</c> so the hybrid output isn't duplicated.
    /// </summary>
    public static string Strip(string content)
        => HscbBlockRegex().Replace(content, string.Empty);

    /// <summary>
    /// Appends every enabled block matching <paramref name="trigger"/> (sorted by SortOrder)
    /// into <paramref name="sb"/>, bracketed by HSCB markers.
    /// </summary>
    public static void RenderTrigger(
        StringBuilder sb,
        IReadOnlyList<CustomGCodeBlock> blocks,
        GCodeTrigger trigger)
    {
        foreach (var block in blocks
                     .Where(b => b.IsEnabled && b.Trigger == trigger)
                     .OrderBy(b => b.SortOrder))
        {
            // Sanitise block name in marker (no newlines)
            var name = block.Name.Replace('\r', ' ').Replace('\n', ' ');
            sb.Append(StartPrefix).Append(" trigger=").Append(trigger)
              .Append(" name=").AppendLine(name);
            sb.AppendLine(block.GCodeContent.TrimEnd());
            sb.AppendLine(EndMarker);
        }
    }
}
