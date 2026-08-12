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
    public async Task BuildPlanAsync_FiresLayerIntervalBlock_OnItsCadenceOnly()
    {
        // 30-layer print, no CNC at all — isolates the EveryNLayers cadence.
        var printGCode = string.Join("\n",
            Enumerable.Range(1, 30).Select(l => $";LAYER:{l}\nG1 X{l} Y{l} Z{l * 0.2}\n"));

        var printPath = Path.Combine(_tempDir, "print-periodic.gcode");
        await File.WriteAllTextAsync(printPath, printGCode);
        var outputPath = Path.Combine(_tempDir, "hybrid-periodic.gcode");

        var blocks = new List<CustomGCodeBlock>
        {
            // Every 10 layers, phased from layer 10, stopping after layer 25:
            // should fire on 10 and 20 — never on 30 (past EndLayer).
            CustomGCodeBlock.Create(
                "Periodic Wipe", "G1 X5 Y5 F3000 ; wipe", GCodeTrigger.EveryNLayers,
                machineProfileId: null, repeatEveryNLayers: 10, startLayer: 10, endLayer: 25),
        };

        var request = new HybridPlanRequest(
            JobId: Guid.NewGuid(),
            PrintGCodePath: printPath,
            CncGCodeByLayer: new Dictionary<int, string>(),
            CncPreamble: "",
            CncPostamble: "",
            MachineEveryNLayers: 5,   // unused here — CncGCodeByLayer is empty
            TotalPrintLayers: 30,
            CncToolId: Guid.NewGuid(),
            EnabledCustomBlocks: blocks,
            OutputGCodePath: outputPath);

        await _orchestrator.BuildPlanAsync(request);

        var content = await File.ReadAllTextAsync(outputPath);

        // Fires exactly twice: layers 10 and 20.
        content.Should().Contain("every 10 layers @ layer 10");
        content.Should().Contain("every 10 layers @ layer 20");
        content.Should().NotContain("@ layer 30");   // beyond EndLayer
        content.Should().NotContain("@ layer 1 ");   // before StartLayer
        System.Text.RegularExpressions.Regex
            .Matches(content, "Periodic Wipe' every").Count.Should().Be(2);
    }

    [Fact]
    public async Task BuildPlanAsync_DisabledLayerIntervalBlock_NeverFires()
    {
        var printGCode = string.Join("\n",
            Enumerable.Range(1, 12).Select(l => $";LAYER:{l}\nG1 X{l} Y{l}\n"));
        var printPath = Path.Combine(_tempDir, "print-disabled.gcode");
        await File.WriteAllTextAsync(printPath, printGCode);
        var outputPath = Path.Combine(_tempDir, "hybrid-disabled.gcode");

        var block = CustomGCodeBlock.Create(
            "Off Block", "M117 nope", GCodeTrigger.EveryNLayers,
            machineProfileId: null, repeatEveryNLayers: 5, startLayer: 5);
        block.Disable();

        await _orchestrator.BuildPlanAsync(new HybridPlanRequest(
            JobId: Guid.NewGuid(),
            PrintGCodePath: printPath,
            CncGCodeByLayer: new Dictionary<int, string>(),
            CncPreamble: "", CncPostamble: "",
            MachineEveryNLayers: 5,   // unused here — CncGCodeByLayer is empty
            TotalPrintLayers: 12,
            CncToolId: Guid.NewGuid(),
            EnabledCustomBlocks: [block],
            OutputGCodePath: outputPath));

        (await File.ReadAllTextAsync(outputPath)).Should().NotContain("M117 nope");
    }

    public void Dispose()
    {
        if (Directory.Exists(_tempDir))
            Directory.Delete(_tempDir, recursive: true);
    }
}
