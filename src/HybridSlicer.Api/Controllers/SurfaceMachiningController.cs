using HybridSlicer.Application.Interfaces;
using HybridSlicer.Application.Interfaces.Repositories;
using Microsoft.AspNetCore.Mvc;

namespace HybridSlicer.Api.Controllers;

[ApiController]
[Route("api/surface-machining")]
public sealed class SurfaceMachiningController : ControllerBase
{
    private readonly ISurfaceMachiningPlanner _planner;
    private readonly IMachineProfileRepository _machines;
    private readonly ICncToolRepository _tools;
    private readonly ILogger<SurfaceMachiningController> _logger;

    public SurfaceMachiningController(
        ISurfaceMachiningPlanner planner,
        IMachineProfileRepository machines,
        ICncToolRepository tools,
        ILogger<SurfaceMachiningController> logger)
    {
        _planner = planner;
        _machines = machines;
        _tools = tools;
        _logger = logger;
    }

    public sealed class GenerateRequest
    {
        public double[] PointPositions { get; set; } = Array.Empty<double>();
        public double[] PointDeviations { get; set; } = Array.Empty<double>();
        public double[] TransformMatrix { get; set; } = Array.Empty<double>();
        public Guid MachineProfileId { get; set; }
        public Guid CncToolId { get; set; }
        public double DeviationThreshold { get; set; } = 0.05;
        public double StepoverPercent { get; set; } = 40;
        public double MaxDepthPerPass { get; set; }
        public double FinishAllowance { get; set; }
        public string PatternType { get; set; } = "zigzag";
        public bool ClimbMilling { get; set; } = true;
        public int SpindleRpmOverride { get; set; }
        public double FeedRateOverride { get; set; }
    }

    [HttpPost("generate")]
    public async Task<IActionResult> Generate([FromBody] GenerateRequest req, CancellationToken ct)
    {
        var machine = await _machines.GetByIdAsync(req.MachineProfileId, ct);
        if (machine == null)
            return NotFound(new { error = "Machine profile not found." });

        var tool = await _tools.GetByIdAsync(req.CncToolId, ct);
        if (tool == null)
            return NotFound(new { error = "CNC tool not found." });

        var cncOffset = machine.CncOffset;
        var request = new SurfaceMachiningRequest(
            PointPositions: req.PointPositions,
            PointDeviations: req.PointDeviations,
            TransformMatrix: req.TransformMatrix,
            ToolDiameterMm: tool.DiameterMm,
            ToolMaxDepthOfCutMm: tool.MaxDepthOfCutMm,
            ToolRpm: req.SpindleRpmOverride > 0 ? req.SpindleRpmOverride : tool.RecommendedRpm,
            ToolFeedMmPerMin: req.FeedRateOverride > 0 ? req.FeedRateOverride : tool.RecommendedFeedMmPerMin,
            ToolTipShape: tool.TipShape,
            SafeClearanceHeightMm: machine.SafeClearanceHeightMm > 0 ? machine.SafeClearanceHeightMm : 5.0,
            CncOffsetX: cncOffset.X,
            CncOffsetY: cncOffset.Y,
            CncOffsetZ: cncOffset.Z,
            CncAxes: machine.CncAxes ?? "XYZ",
            MachineEnvelopeX: machine.TravelXMm,
            MachineEnvelopeY: machine.TravelYMm,
            MachineEnvelopeZ: machine.TravelZMm,
            DeviationThreshold: req.DeviationThreshold,
            StepoverPercent: req.StepoverPercent,
            MaxDepthPerPass: req.MaxDepthPerPass,
            FinishAllowance: req.FinishAllowance,
            PatternType: req.PatternType,
            ClimbMilling: req.ClimbMilling);

        var result = await _planner.GenerateAsync(request, ct);
        return Ok(result);
    }
}
