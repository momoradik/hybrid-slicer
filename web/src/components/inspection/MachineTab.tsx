import { useState } from 'react'
import { useQuery, useMutation } from '@tanstack/react-query'
import { machineProfilesApi, toolsApi, surfaceMachiningApi, type SurfaceMachiningResult } from '../../api/client'
import type { InspectResponse } from '../../api/alignmesh-types.generated'

interface Props {
  result: InspectResponse
}

export default function MachineTab({ result }: Props) {
  const [machineId, setMachineId] = useState('')
  const [toolId, setToolId] = useState('')
  const [threshold, setThreshold] = useState(0.05)
  const [stepover, setStepover] = useState(40)
  const [maxDepth, setMaxDepth] = useState(0)
  const [finishAllowance, setFinishAllowance] = useState(0)
  const [pattern, setPattern] = useState('zigzag')
  const [climb, setClimb] = useState(true)
  const [rpmOverride, setRpmOverride] = useState(0)
  const [feedOverride, setFeedOverride] = useState(0)
  const [machResult, setMachResult] = useState<SurfaceMachiningResult | null>(null)
  const [error, setError] = useState<string | null>(null)

  const { data: machines = [] } = useQuery({
    queryKey: ['machines'], queryFn: machineProfilesApi.getAll,
  })
  const { data: tools = [] } = useQuery({
    queryKey: ['tools'], queryFn: toolsApi.getAll,
  })

  const selectedTool = tools.find(t => t.id === toolId)

  const generateMutation = useMutation({
    mutationFn: () => surfaceMachiningApi.generate({
      pointPositions: (result as any).point_positions ?? [],
      pointDeviations: result.point_deviations ?? [],
      transformMatrix: result.transform_matrix ?? [],
      machineProfileId: machineId,
      cncToolId: toolId,
      deviationThreshold: threshold,
      stepoverPercent: stepover,
      maxDepthPerPass: maxDepth,
      finishAllowance: finishAllowance,
      patternType: pattern,
      climbMilling: climb,
      spindleRpmOverride: rpmOverride,
      feedRateOverride: feedOverride,
    }),
    onSuccess: (data) => { setMachResult(data); setError(null) },
    onError: (err: Error) => { setError(err.message); setMachResult(null) },
  })

  const canGenerate = machineId && toolId && result.valid

  const excessCount = (result.point_deviations ?? []).filter(d => d > threshold).length
  const totalPoints = result.point_deviations?.length ?? 0

  const downloadGCode = () => {
    if (!machResult?.gCode) return
    const blob = new Blob([machResult.gCode], { type: 'text/plain' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'cmm-machining.gcode'
    a.click()
    URL.revokeObjectURL(url)
  }

  return (
    <div className="space-y-4">
      {/* Summary */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
        <h3 className="text-sm font-semibold text-white mb-3">Deviation Summary</h3>
        <div className="grid grid-cols-4 gap-3 text-center">
          <div>
            <p className="text-lg font-mono text-white">{totalPoints.toLocaleString()}</p>
            <p className="text-[10px] text-gray-500">Total points</p>
          </div>
          <div>
            <p className="text-lg font-mono text-yellow-400">{excessCount.toLocaleString()}</p>
            <p className="text-[10px] text-gray-500">Excess (&gt;{threshold}mm)</p>
          </div>
          <div>
            <p className="text-lg font-mono text-red-400">{result.stats.max.toFixed(3)}</p>
            <p className="text-[10px] text-gray-500">Max deviation (mm)</p>
          </div>
          <div>
            <p className="text-lg font-mono text-gray-300">{result.stats.rms.toFixed(3)}</p>
            <p className="text-[10px] text-gray-500">RMS (mm)</p>
          </div>
        </div>
      </div>

      {/* Machine + Tool selection */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
          <h3 className="text-sm font-semibold text-white">Machine</h3>
          <select
            className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200"
            value={machineId}
            onChange={e => setMachineId(e.target.value)}
          >
            <option value="">-- select machine --</option>
            {machines.map(m => (
              <option key={m.id} value={m.id}>{m.name}</option>
            ))}
          </select>
        </div>

        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
          <h3 className="text-sm font-semibold text-white">CNC Tool</h3>
          <select
            className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-2 text-sm text-gray-200"
            value={toolId}
            onChange={e => setToolId(e.target.value)}
          >
            <option value="">-- select tool --</option>
            {tools.map(t => (
              <option key={t.id} value={t.id}>
                {t.name} ({t.tipShape} {t.diameterMm}mm)
              </option>
            ))}
          </select>
          {selectedTool && (
            <div className="text-[10px] text-gray-500 grid grid-cols-3 gap-1">
              <span>RPM: {selectedTool.recommendedRpm}</span>
              <span>Feed: {selectedTool.recommendedFeedMmPerMin} mm/min</span>
              <span>Max DOC: {selectedTool.maxDepthOfCutMm}mm</span>
            </div>
          )}
        </div>
      </div>

      {/* Machining parameters */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-4">
        <h3 className="text-sm font-semibold text-white">Machining Parameters</h3>

        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Deviation threshold (mm)</label>
            <input type="number" step="0.01" min="0.001"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={threshold} onChange={e => setThreshold(Number(e.target.value))} />
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Stepover (%)</label>
            <input type="number" step="5" min="5" max="90"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={stepover} onChange={e => setStepover(Number(e.target.value))} />
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Max depth/pass (mm)</label>
            <input type="number" step="0.1" min="0" placeholder="auto"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={maxDepth || ''} onChange={e => setMaxDepth(Number(e.target.value))} />
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Finish allowance (mm)</label>
            <input type="number" step="0.01" min="0"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={finishAllowance} onChange={e => setFinishAllowance(Number(e.target.value))} />
          </div>
        </div>

        <div className="grid grid-cols-2 md:grid-cols-4 gap-4">
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Pattern</label>
            <select
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={pattern} onChange={e => setPattern(e.target.value)}
            >
              <option value="zigzag">Zigzag</option>
              <option value="raster">Raster</option>
            </select>
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Milling direction</label>
            <select
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={climb ? 'climb' : 'conventional'}
              onChange={e => setClimb(e.target.value === 'climb')}
            >
              <option value="climb">Climb</option>
              <option value="conventional">Conventional</option>
            </select>
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">RPM override</label>
            <input type="number" step="100" min="0" placeholder="tool default"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={rpmOverride || ''} onChange={e => setRpmOverride(Number(e.target.value))} />
          </div>
          <div className="space-y-1">
            <label className="text-xs text-gray-400">Feed override (mm/min)</label>
            <input type="number" step="10" min="0" placeholder="tool default"
              className="w-full bg-gray-950 border border-gray-700 rounded-lg px-3 py-1.5 text-sm text-gray-200"
              value={feedOverride || ''} onChange={e => setFeedOverride(Number(e.target.value))} />
          </div>
        </div>
      </div>

      {/* Error */}
      {error && (
        <div className="bg-red-950 border border-red-800 rounded-xl p-4 text-red-300 text-sm">{error}</div>
      )}

      {/* Generate button */}
      <button
        onClick={() => generateMutation.mutate()}
        disabled={!canGenerate || generateMutation.isPending}
        className="w-full px-5 py-3 bg-primary/80 hover:bg-primary text-white text-sm font-medium rounded-lg transition-colors disabled:opacity-40 disabled:cursor-not-allowed"
      >
        {generateMutation.isPending ? 'Generating G-code...' : `Generate Machining G-code (${excessCount} points)`}
      </button>

      {/* Results */}
      {machResult && !machResult.isEmpty && (
        <div className="space-y-4">
          {/* Stats */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
            <div className="flex items-center justify-between mb-3">
              <h3 className="text-sm font-semibold text-white">Machining Result</h3>
              <button onClick={downloadGCode}
                className="px-3 py-1.5 bg-primary/60 hover:bg-primary text-white text-xs rounded-lg transition">
                Download G-code
              </button>
            </div>
            <div className="grid grid-cols-3 gap-3 text-center">
              <div>
                <p className="text-lg font-mono text-white">{machResult.totalPasses}</p>
                <p className="text-[10px] text-gray-500">Passes</p>
              </div>
              <div>
                <p className="text-lg font-mono text-white">{machResult.cuttingMoves}</p>
                <p className="text-[10px] text-gray-500">Cutting moves</p>
              </div>
              <div>
                <p className="text-lg font-mono text-white">
                  {machResult.estimatedTimeSec > 60
                    ? (machResult.estimatedTimeSec / 60).toFixed(1) + ' min'
                    : machResult.estimatedTimeSec.toFixed(0) + 's'}
                </p>
                <p className="text-[10px] text-gray-500">Est. time</p>
              </div>
            </div>
            {machResult.warnings.length > 0 && (
              <div className="mt-3 space-y-1">
                {machResult.warnings.map((w, i) => (
                  <p key={i} className="text-xs text-yellow-400 bg-yellow-950/30 border border-yellow-800/30 rounded px-2 py-1">{w}</p>
                ))}
              </div>
            )}
          </div>

          {/* G-code preview */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
            <div className="px-4 py-2 border-b border-gray-800 flex items-center justify-between">
              <span className="text-xs text-gray-400 font-mono">
                {machResult.gCode.split('\n').length} lines
              </span>
            </div>
            <pre className="p-4 text-xs text-gray-300 font-mono overflow-auto max-h-96 whitespace-pre">
              {machResult.gCode}
            </pre>
          </div>
        </div>
      )}

      {machResult?.isEmpty && (
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 text-center text-gray-400 text-sm">
          No machining needed — all deviations below threshold.
        </div>
      )}
    </div>
  )
}
