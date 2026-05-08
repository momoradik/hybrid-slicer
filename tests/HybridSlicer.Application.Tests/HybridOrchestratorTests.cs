using FluentAssertions;
using HybridSlicer.Application.Interfaces;
using HybridSlicer.Domain.Entities;
using HybridSlicer.Domain.Enums;
using HybridSlicer.Infrastructure.Orchestration;
using Microsoft.Extensions.Logging.Abstractions;
using Xunit;

namespace HybridSlicer.Application.Tests;

public class HybridOrchestratorTests : IDisposable
{
    private readonly string _tempDir = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString());
    private readonly HybridOrchestrator _orchestrator = new(NullLogger<HybridOrchestrator>.Instance);

    public HybridOrchestratorTests() => Directory.CreateDirectory(_tempDir);

    [Fact]
    public async Task BuildPlanAsync_ProducesHybridGCode_WithCncInjectedEveryN()
    {
        // Arrange: 20-layer print G-code with ;LAYER:N markers
        var printGCode = string.Join("\n",
            Enumerable.Range(1, 20).Select(l => $";LAYER:{l}\nG1 X{l} Y{l} Z{l * 0.2}\n"));

        var printPath = Path.Combine(_tempDir, "print.gcode");
        await File.WriteAllTextAsync(printPath, printGCode);

        var outputPath = Path.Combine(_tempDir, "hybrid.gcode");
        var toolId = Guid.NewGuid();
        var jobId = Guid.NewGuid();

        var cncByLayer = new Dictionary<int, string>
        {
            [5]  = "; CNC at layer 5\nG0 Z10\nG1 X50 Y50 Z1\n",
            [10] = "; CNC at layer 10\nG0 Z10\nG1 X50 Y50 Z2\n",
            [15] = "; CNC at layer 15\nG0 Z10\nG1 X50 Y50 Z3\n",
            [20] = "; CNC at layer 20\nG0 Z10\nG1 X50 Y50 Z4\n",
        };

        var blocks = new List<CustomGCodeBlock>
        {
            CustomGCodeBlock.Create("Before Block", "M3 S12000", GCodeTrigger.BeforeMachining),
            CustomGCodeBlock.Create("After Block",  "M5",        GCodeTrigger.AfterMachining),
        };

        var request = new HybridPlanRequest(
            JobId: jobId,
            PrintGCodePath: printPath,
            CncGCodeByLayer: cncByLayer,
            CncPreamble: "",
            CncPostamble: "",
            MachineEveryNLayers: 5,
            TotalPrintLayers: 20,
            CncToolId: toolId,
            EnabledCustomBlocks: blocks,
            OutputGCodePath: outputPath);

        // Act
        var result = await _orchestrator.BuildPlanAsync(request);

        // Assert
        result.Should().NotBeNull();
        result.HybridGCodePath.Should().Be(outputPath);
        File.Exists(outputPath).Should().BeTrue();

        var hybridContent = await File.ReadAllTextAsync(outputPath);
        hybridContent.Should().Contain("CNC at layer 5");
        hybridContent.Should().Contain("M3 S12000");
        hybridContent.Should().Contain("M5");

        result.Plan.Steps.Should().NotBeEmpty();
        result.Plan.Steps.Any(s => s.OperationType == OperationType.Machining).Should().BeTrue();
        result.Plan.Steps.Any(s => s.OperationType == OperationType.CustomGCode).Should().BeTrue();
    }

    [Fact]
    public async Task BuildPlanAsync_EmitsAllBookendTriggers_InCorrectOrder()
    {
        // Cura-shaped input: pre-layer header (startup G-code) followed by ;LAYER:N markers.
        var printGCode =
            "M140 S60\nG28\n;PRE_LAYER_HEADER_SENTINEL\n" +
            string.Join("\n", Enumerable.Range(0, 6).Select(l => $";LAYER:{l}\nG1 X{l} Y{l}\n"));

        var printPath = Path.Combine(_tempDir, "print.gcode");
        await File.WriteAllTextAsync(printPath, printGCode);
        var outputPath = Path.Combine(_tempDir, "hybrid.gcode");

        var blocks = new List<CustomGCodeBlock>
        {
            CustomGCodeBlock.Create("JS",   "M_JOBSTART",       GCodeTrigger.JobStart),
            CustomGCodeBlock.Create("BP",   "M_BEFOREPRINTING", GCodeTrigger.BeforePrinting),
            CustomGCodeBlock.Create("BM",   "M_BEFOREMACHINING",GCodeTrigger.BeforeMachining),
            CustomGCodeBlock.Create("AM",   "M_AFTERMACHINING", GCodeTrigger.AfterMachining),
            CustomGCodeBlock.Create("AP",   "M_AFTERPRINTING",  GCodeTrigger.AfterPrinting),
            CustomGCodeBlock.Create("JE",   "M_JOBEND",         GCodeTrigger.JobEnd),
        };

        var request = new HybridPlanRequest(
            JobId: Guid.NewGuid(),
            PrintGCodePath: printPath,
            CncGCodeByLayer: new Dictionary<int, string> { [3] = "; CNC@3\nG0 Z10\n" },
            CncPreamble: "",
            CncPostamble: "",
            MachineEveryNLayers: 3,
            TotalPrintLayers: 5,
            CncToolId: Guid.NewGuid(),
            EnabledCustomBlocks: blocks,
            OutputGCodePath: outputPath);

        await _orchestrator.BuildPlanAsync(request);

        var content = await File.ReadAllTextAsync(outputPath);

        // All six triggers present
        content.Should().Contain("M_JOBSTART");
        content.Should().Contain("M_BEFOREPRINTING");
        content.Should().Contain("M_BEFOREMACHINING");
        content.Should().Contain("M_AFTERMACHINING");
        content.Should().Contain("M_AFTERPRINTING");
        content.Should().Contain("M_JOBEND");

        // Cura header preserved (was previously dropped by SplitByLayer)
        content.Should().Contain("PRE_LAYER_HEADER_SENTINEL");

        // Layer 0 is now flushed (was previously dropped because printStart=0 initial)
        content.Should().MatchRegex(@";LAYER:0\b");

        // Order: JobStart < BeforePrinting < CuraHeader < BeforeMachining < CNC < AfterMachining < AfterPrinting < JobEnd
        int IdxOf(string s) => content.IndexOf(s, StringComparison.Ordinal);
        IdxOf("M_JOBSTART").Should().BeLessThan(IdxOf("M_BEFOREPRINTING"));
        IdxOf("M_BEFOREPRINTING").Should().BeLessThan(IdxOf("PRE_LAYER_HEADER_SENTINEL"));
        IdxOf("PRE_LAYER_HEADER_SENTINEL").Should().BeLessThan(IdxOf("M_BEFOREMACHINING"));
        IdxOf("M_BEFOREMACHINING").Should().BeLessThan(IdxOf("CNC@3"));
        IdxOf("CNC@3").Should().BeLessThan(IdxOf("M_AFTERMACHINING"));
        IdxOf("M_AFTERMACHINING").Should().BeLessThan(IdxOf("M_AFTERPRINTING"));
        IdxOf("M_AFTERPRINTING").Should().BeLessThan(IdxOf("M_JOBEND"));
    }

    [Fact]
    public async Task BuildPlanAsync_DoesNotDuplicate_PreBakedWrappers()
    {
        // Simulate print.gcode that already has HSCB-wrapped JobStart blocks (as SlicePrintJobHandler emits).
        var printGCode =
            "; HSCB-START trigger=JobStart name=Pre-baked\n" +
            "M_JOBSTART\n" +
            "; HSCB-END\n" +
            "G28\n" +
            ";LAYER:0\nG1 X0 Y0\n" +
            ";LAYER:1\nG1 X1 Y1\n";

        var printPath = Path.Combine(_tempDir, "print.gcode");
        await File.WriteAllTextAsync(printPath, printGCode);
        var outputPath = Path.Combine(_tempDir, "hybrid.gcode");

        var blocks = new List<CustomGCodeBlock>
        {
            CustomGCodeBlock.Create("JS", "M_JOBSTART", GCodeTrigger.JobStart),
        };

        var request = new HybridPlanRequest(
            JobId: Guid.NewGuid(),
            PrintGCodePath: printPath,
            CncGCodeByLayer: new Dictionary<int, string>(),
            CncPreamble: "",
            CncPostamble: "",
            MachineEveryNLayers: 1,
            TotalPrintLayers: 1,
            CncToolId: Guid.NewGuid(),
            EnabledCustomBlocks: blocks,
            OutputGCodePath: outputPath);

        await _orchestrator.BuildPlanAsync(request);

        var content = await File.ReadAllTextAsync(outputPath);
        var occurrences = System.Text.RegularExpressions.Regex.Matches(content, "M_JOBSTART").Count;
        occurrences.Should().Be(1, "the pre-baked wrapper must be stripped before re-emission");
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, recursive: true);
    }
}
