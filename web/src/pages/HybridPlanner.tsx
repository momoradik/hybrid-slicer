import { useState, useMemo } from 'react'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { jobsApi, toolsApi, machineProfilesApi } from '../api/client'
import GCodePreview3D from '../components/viewer/GCodePreview3D'
import CncSimulation from '../components/viewer/CncSimulation'
import DisabledHint from '../components/DisabledHint'

type Tab = 'config' | 'gcode' | 'preview3d' | 'simulation'

interface UnmachinableRegion {
  zHeightMm: number
  reason: string
  bounds: { minX: number; minY: number; maxX: number; maxY: number }
}

export default function HybridPlanner() {
  const qc = useQueryClient()
  const { data: jobs      = [] } = useQuery({ queryKey: ['jobs'],     queryFn: jobsApi.getAll })
  const { data: tools     = [] } = useQuery({ queryKey: ['tools'],    queryFn: toolsApi.getAll })
  const { data: machines  = [] } = useQuery({ queryKey: ['machines'], queryFn: machineProfilesApi.getAll })

  const [jobId,                 setJobId]                 = useState('')
  const [toolId,                setToolId]                = useState('')
  const [machineEveryN,         setMachineEveryN]         = useState(10)
  const [machineInnerWalls,     setMachineInnerWalls]     = useState(false)
  const [avoidSupports,         setAvoidSupports]         = useState(false)
  const [supportClearanceMm,    setSupportClearanceMm]    = useState(2.0)
  const [autoMachiningFrequency, setAutoMachiningFrequency] = useState(false)
  const [zSafetyOffsetMm,       setZSafetyOffsetMm]       = useState(0.0)
  const [spindleRpmOverride,    setSpindleRpmOverride]    = useState<number | null>(null)
  const [spindleStartX,    setSpindleStartX]    = useState(0.0)
  const [spindleStartY,    setSpindleStartY]    = useState(0.0)
  const [spindleStartZ,    setSpindleStartZ]    = useState<number | null>(null)
  const [spindleEndSame,   setSpindleEndSame]   = useState(true)
  const [spindleEndX,      setSpindleEndX]      = useState(0.0)
  const [spindleEndY,      setSpindleEndY]      = useState(0.0)
  const [spindleEndZ,      setSpindleEndZ]      = useState<number | null>(null)
  const [activeTab,             setActiveTab]             = useState<Tab>('config')
  const [toolpathGCode,         setToolpathGCode]         = useState<string | null>(null)
  const [printGCode,            setPrintGCode]            = useState<string | null>(null)
  const [machinedLayers,        setMachinedLayers]        = useState<number[]>([])
  const [unmachinableRegions,   setUnmachinableRegions]   = useState<UnmachinableRegion[]>([])

  const readyJobs   = jobs.filter(j => j.status === 'SlicingComplete' || j.status === 'ToolpathsComplete' || j.status === 'Ready')
  const selectedJob  = jobs.find(j => j.id === jobId)
  const selectedTool = tools.find(t => t.id === toolId)

  const toolpathsMutation = useMutation({
    mutationFn: () =>
      jobsApi.generateToolpaths(
        jobId, toolId, machineEveryN, machineInnerWalls, avoidSupports,
        supportClearanceMm, autoMachiningFrequency, zSafetyOffsetMm, spindleRpmOverride,
        spindleStartX, spindleStartY, spindleStartZ,
        spindleEndSame ? spindleStartX : spindleEndX,
        spindleEndSame ? spindleStartY : spindleEndY,
        spindleEndSame ? spindleStartZ : spindleEndZ
      ),
    onSuccess: async (result: any) => {
      qc.invalidateQueries({ queryKey: ['jobs'] })
      if (result?.machinedAtLayers) setMachinedLayers(result.machinedAtLayers)
      if (result?.unmachinableRegions) setUnmachinableRegions(result.unmachinableRegions)
      else setUnmachinableRegions([])
      try {
        const gcode = await jobsApi.getToolpathGCode(jobId)
        setToolpathGCode(gcode)
        const pg = await jobsApi.getPrintGCode(jobId)
        setPrintGCode(pg)
        setActiveTab('gcode')
      } catch {
        // G-code fetch failed — toolpaths were generated but files aren't ready yet
      }
    },
  })

  // Build volume from the selected job's machine profile
  const selectedMachine = machines.find(m => m.id === selectedJob?.machineProfileId)
  const buildVolume = useMemo(() => ({
    width:  selectedMachine?.bedWidthMm  ?? 440,
    depth:  selectedMachine?.bedDepthMm  ?? 290,
    height: selectedMachine?.bedHeightMm ?? 350,
  }), [selectedMachine?.bedWidthMm, selectedMachine?.bedDepthMm, selectedMachine?.bedHeightMm])

  return (
    <div className="space-y-6">
      <h2 className="text-2xl font-semibold text-white">CNC Polishing</h2>

      {/* Tabs */}
      <div className="flex gap-1 border-b border-gray-800">
        <TabBtn active={activeTab === 'config'} onClick={() => setActiveTab('config')}>
          Configuration
        </TabBtn>
        <DisabledHint when={!toolpathGCode} reason="Generate toolpaths first to view the G-code.">
          <TabBtn
            active={activeTab === 'gcode'}
            disabled={!toolpathGCode}
            onClick={() => setActiveTab('gcode')}
          >
            G-code Text
            {toolpathGCode && (
              <span className="ml-2 px-1.5 py-0.5 text-xs bg-purple-700 rounded-full">CNC</span>
            )}
          </TabBtn>
        </DisabledHint>
        <DisabledHint when={!toolpathGCode} reason="Generate toolpaths first to see the 3D preview.">
          <TabBtn
            active={activeTab === 'preview3d'}
            disabled={!toolpathGCode}
            onClick={() => setActiveTab('preview3d')}
          >
            3D Preview
            {toolpathGCode && (
              <span className="ml-2 px-1.5 py-0.5 text-xs bg-cyan-700 rounded-full">NEW</span>
            )}
          </TabBtn>
        </DisabledHint>
        <DisabledHint when={!toolpathGCode || !printGCode} reason="Generate toolpaths first to run the simulation.">
          <TabBtn
            active={activeTab === 'simulation'}
            disabled={!toolpathGCode || !printGCode}
            onClick={() => setActiveTab('simulation')}
          >
            Simulation
            {toolpathGCode && printGCode && (
              <span className="ml-2 px-1.5 py-0.5 text-xs bg-green-700 rounded-full">SIM</span>
            )}
          </TabBtn>
        </DisabledHint>
      </div>

      {activeTab === 'config' && (
        <div className="grid grid-cols-2 gap-6">
          {/* Left: controls */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl p-6 space-y-5">
            <h3 className="font-medium text-white">Configuration</h3>

            <Field label="Job (must be sliced)">
              <select className="input" value={jobId} onChange={e => setJobId(e.target.value)}>
                <option value="">Select…</option>
                {readyJobs.map(j => (
                  <option key={j.id} value={j.id}>{j.name} ({j.status})</option>
                ))}
              </select>
            </Field>

            <Field label="CNC Tool">
              <select className="input" value={toolId} onChange={e => setToolId(e.target.value)}>
                <option value="">Select…</option>
                {tools.map(t => (
                  <option key={t.id} value={t.id}>
                    {t.name} (Ø{t.diameterMm} mm · flute {t.fluteLengthMm} mm · len {t.toolLengthMm ?? '?'} mm)
                  </option>
                ))}
              </select>
              {selectedTool && (
                <div className="flex gap-3 text-[11px] text-gray-500 mt-1 flex-wrap">
                  <span><span className="text-violet-400">Ø</span> {selectedTool.diameterMm} mm</span>
                  <span><span className="text-orange-400">flute</span> {selectedTool.fluteLengthMm} mm</span>
                  <span><span className="text-blue-400">length</span> {selectedTool.toolLengthMm ?? '—'} mm</span>
                  <span className="text-gray-600">RPM {selectedTool.recommendedRpm.toLocaleString()}</span>
                </div>
              )}
            </Field>

            {/* Z Safety Offset */}
            <Field label="Z Safety Offset (mm)">
              <div className="flex items-center gap-2">
                <input
                  type="number"
                  min={0} max={5} step={0.05}
                  value={zSafetyOffsetMm}
                  onChange={e => setZSafetyOffsetMm(Math.max(0, Math.min(5, +e.target.value)))}
                  className="input w-24 text-center"
                />
                <span className="text-xs text-gray-500">mm</span>
                <span className="text-xs text-gray-600">(raises all CNC passes above nominal layer)</span>
              </div>
              {zSafetyOffsetMm > 0 ? (
                <div className="text-xs text-amber-400 mt-1">
                  All machining layers raised by +{zSafetyOffsetMm.toFixed(2)} mm above nominal layer height
                </div>
              ) : (
                <div className="text-xs text-gray-600 mt-1">
                  Default 0 — machines exactly at layer surface
                </div>
              )}
            </Field>

            {!autoMachiningFrequency && (
              <Field label={`Machine every N part layers (N = ${machineEveryN})`}>
                <div className="flex items-center gap-2">
                  <input
                    type="range"
                    min={1} max={100} step={1}
                    value={machineEveryN}
                    onChange={e => setMachineEveryN(+e.target.value)}
                    className="flex-1 accent-primary"
                  />
                  <input
                    type="number"
                    min={1} max={100}
                    value={machineEveryN}
                    onChange={e => setMachineEveryN(Math.max(1, Math.min(100, +e.target.value)))}
                    className="input w-16 text-center"
                  />
                </div>
                <div className="flex justify-between text-xs text-gray-500 mt-1">
                  <span>Every part layer</span><span>Every 100 part layers</span>
                </div>
                <div className="text-xs text-gray-600 mt-1">
                  Support-only and non-geometry layers are excluded from the count.
                </div>
              </Field>
            )}

            {autoMachiningFrequency && selectedTool && (
              <div className="bg-blue-950/40 border border-blue-800 rounded-lg px-3 py-2 text-xs text-blue-300 space-y-1">
                <div className="font-medium">Dynamic frequency — interval varies by part geometry:</div>
                <ul className="list-disc list-inside space-y-0.5 text-blue-400">
                  <li>Flute reach: machine when pending height ≥ {(selectedTool.fluteLengthMm * 0.8).toFixed(1)} mm ({selectedTool.fluteLengthMm} mm × 80%)</li>
                  <li>Geometry change: machine when bounding area shifts &gt;25% since last pass</li>
                  <li>Access blocking: machine before next layer seals off current wall</li>
                </ul>
                <div className="text-blue-500">Cylinders → sparse intervals · Sphere tops → layer-by-layer</div>
              </div>
            )}

            {/* Spindle Speed */}
            {selectedTool && (
              <Field label="Spindle Speed (RPM)">
                <div className="flex items-center gap-2">
                  <input
                    type="number"
                    min={100} max={60000} step={100}
                    value={spindleRpmOverride ?? selectedTool.recommendedRpm}
                    onChange={e => setSpindleRpmOverride(+e.target.value)}
                    className="input w-28 text-center"
                  />
                  <span className="text-xs text-gray-500">RPM</span>
                  {spindleRpmOverride !== null && spindleRpmOverride !== selectedTool.recommendedRpm && (
                    <button
                      onClick={() => setSpindleRpmOverride(null)}
                      className="text-xs text-gray-500 hover:text-gray-300 underline"
                    >
                      reset to {selectedTool.recommendedRpm.toLocaleString()}
                    </button>
                  )}
                </div>
                <div className="text-xs text-gray-600 mt-1">
                  Tool default: {selectedTool.recommendedRpm.toLocaleString()} RPM
                </div>
              </Field>
            )}

            {/* CNC options */}
            <div className="space-y-3 border border-gray-700 rounded-lg p-4">
              <p className="text-xs text-gray-400 font-medium uppercase tracking-wide">CNC Options</p>

              <label className="flex items-center gap-3 cursor-pointer select-none">
                <input
                  type="checkbox"
                  checked={autoMachiningFrequency}
                  onChange={e => setAutoMachiningFrequency(e.target.checked)}
                  className="w-4 h-4 accent-blue-500"
                />
                <div>
                  <div className="text-sm text-gray-200">Auto Machining Frequency</div>
                  <div className="text-xs text-gray-500">Machine only when lower geometry becomes unreachable (uses flute length)</div>
                </div>
              </label>

              <label className="flex items-center gap-3 cursor-pointer select-none">
                <input
                  type="checkbox"
                  checked={machineInnerWalls}
                  onChange={e => setMachineInnerWalls(e.target.checked)}
                  className="w-4 h-4 accent-purple-500"
                />
                <div>
                  <div className="text-sm text-gray-200">Machine inner surfaces</div>
                  <div className="text-xs text-gray-500">Also run CNC on inner-wall paths (holes, pockets)</div>
                </div>
              </label>

              <label className="flex items-center gap-3 cursor-pointer select-none">
                <input
                  type="checkbox"
                  checked={avoidSupports}
                  onChange={e => setAvoidSupports(e.target.checked)}
                  className="w-4 h-4 accent-purple-500"
                />
                <div>
                  <div className="text-sm text-gray-200">Avoid supports</div>
                  <div className="text-xs text-gray-500">Clip toolpaths around support regions (no-cut zones)</div>
                </div>
              </label>

              {avoidSupports && (
                <div className="ml-7 flex items-center gap-3">
                  <label className="text-xs text-gray-400 whitespace-nowrap">Support XY clearance</label>
                  <input
                    type="number"
                    min={0.1} max={20} step={0.1}
                    value={supportClearanceMm}
                    onChange={e => setSupportClearanceMm(Math.max(0.1, Math.min(20, +e.target.value)))}
                    className="input w-20 text-center"
                  />
                  <span className="text-xs text-gray-500">mm</span>
                  <span className="text-xs text-gray-600">(tool stays this far from supports)</span>
                </div>
              )}

              {/* Spindle Positions */}
              <div className="border-t border-gray-700 pt-3 space-y-3">
                <p className="text-xs text-gray-500 font-medium uppercase tracking-wide">Spindle Positions</p>
                <div>
                  <p className="text-xs text-gray-400 mb-1.5">Start Position</p>
                  <div className="grid grid-cols-3 gap-2">
                    {(['X','Y'] as const).map((axis, i) => (
                      <div key={axis} className="space-y-0.5">
                        <label className="text-[10px] text-gray-500">{axis}</label>
                        <input type="number" step={0.1}
                          value={i === 0 ? spindleStartX : spindleStartY}
                          onChange={e => i === 0 ? setSpindleStartX(+e.target.value) : setSpindleStartY(+e.target.value)}
                          className="input text-xs py-1 text-center w-full" />
                      </div>
                    ))}
                    <div className="space-y-0.5">
                      <label className="text-[10px] text-gray-500">Z (blank=safe)</label>
                      <input type="number" step={0.5}
                        value={spindleStartZ ?? ''}
                        placeholder="auto"
                        onChange={e => setSpindleStartZ(e.target.value ? +e.target.value : null)}
                        className="input text-xs py-1 text-center w-full" />
                    </div>
                  </div>
                </div>
                <label className="flex items-center gap-2 cursor-pointer select-none">
                  <input type="checkbox" checked={spindleEndSame} onChange={e => setSpindleEndSame(e.target.checked)} className="w-3.5 h-3.5 accent-blue-500" />
                  <span className="text-xs text-gray-400">End = Start position</span>
                </label>
                {!spindleEndSame && (
                  <div>
                    <p className="text-xs text-gray-400 mb-1.5">End Position</p>
                    <div className="grid grid-cols-3 gap-2">
                      {(['X','Y'] as const).map((axis, i) => (
                        <div key={axis} className="space-y-0.5">
                          <label className="text-[10px] text-gray-500">{axis}</label>
                          <input type="number" step={0.1}
                            value={i === 0 ? spindleEndX : spindleEndY}
                            onChange={e => i === 0 ? setSpindleEndX(+e.target.value) : setSpindleEndY(+e.target.value)}
                            className="input text-xs py-1 text-center w-full" />
                        </div>
                      ))}
                      <div className="space-y-0.5">
                        <label className="text-[10px] text-gray-500">Z (blank=safe)</label>
                        <input type="number" step={0.5}
                          value={spindleEndZ ?? ''}
                          placeholder="auto"
                          onChange={e => setSpindleEndZ(e.target.value ? +e.target.value : null)}
                          className="input text-xs py-1 text-center w-full" />
                      </div>
                    </div>
                  </div>
                )}
              </div>
            </div>

            <div className="flex gap-3 pt-2">
              <DisabledHint when={!jobId || !toolId} reason={
                !jobId ? 'Select a sliced job above.' : 'Select a CNC tool above.'
              }>
                <button
                  onClick={() => toolpathsMutation.mutate()}
                  disabled={!jobId || !toolId || toolpathsMutation.isPending}
                  className="flex-1 py-2 bg-purple-700 hover:bg-purple-600 disabled:opacity-40 text-white rounded-lg text-sm transition"
                >
                  {toolpathsMutation.isPending ? 'Generating…' : 'Generate CNC Toolpaths'}
                </button>
              </DisabledHint>

            </div>

            {toolpathsMutation.isError && (
              <div className="bg-red-950/40 border border-red-800 rounded-lg px-3 py-2 text-xs text-red-400">
                {(toolpathsMutation.error as any)?.response?.data?.detail
                  ?? (toolpathsMutation.error as Error)?.message
                  ?? 'Toolpath generation failed'}
              </div>
            )}

            {unmachinableRegions.length > 0 && (
              <div className="bg-yellow-950/40 border border-yellow-700 rounded-lg px-3 py-2 text-xs text-yellow-300">
                <div className="font-semibold mb-1">
                  ⚠ {unmachinableRegions.length} region{unmachinableRegions.length !== 1 ? 's' : ''} could not be machined:{' '}
                  {(() => {
                    const counts: Record<string, number> = {}
                    for (const r of unmachinableRegions) counts[r.reason] = (counts[r.reason] ?? 0) + 1
                    return Object.entries(counts).map(([reason, count]) => `${count}× ${reason}`).join(', ')
                  })()}
                </div>
                <div className="text-yellow-500">
                  Check: tool diameter vs. feature width · flute length vs. part height · tool length vs. machine Z travel · support clearance settings.
                </div>
              </div>
            )}

            {toolpathsMutation.isSuccess && toolpathGCode && (
              <div className="flex gap-2">
                <button
                  onClick={() => setActiveTab('gcode')}
                  className="flex-1 py-2 bg-purple-900 hover:bg-purple-800 border border-purple-600 text-purple-300 rounded-lg text-sm transition"
                >
                  View G-code Text →
                </button>
                <button
                  onClick={() => setActiveTab('preview3d')}
                  className="flex-1 py-2 bg-cyan-900 hover:bg-cyan-800 border border-cyan-600 text-cyan-300 rounded-lg text-sm transition"
                >
                  3D Preview →
                </button>
              </div>
            )}
          </div>

          {/* Right: process plan visualiser */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl p-6">
            <h3 className="font-medium text-white mb-4">Process Preview</h3>
            {selectedJob ? (
              <div className="space-y-1 text-sm font-mono text-gray-400 max-h-96 overflow-y-auto">
                {machinedLayers.length > 0 ? (
                  // After toolpath generation: show actual (potentially irregular) schedule
                  (() => {
                    const rows: React.ReactNode[] = []
                    let printStart = 1
                    for (let i = 0; i < machinedLayers.length; i++) {
                      const ml = machinedLayers[i]
                      rows.push(
                        <div key={i} className="space-y-0.5">
                          <div className="text-blue-400">▣ Print L{printStart}–L{ml}</div>
                          <div className="pl-4 text-orange-400">⚙ CNC @ L{ml} ✓</div>
                        </div>
                      )
                      printStart = ml + 1
                    }
                    if (printStart <= (selectedJob.totalPrintLayers ?? 0)) {
                      rows.push(
                        <div key="final" className="text-blue-400">
                          ▣ Print L{printStart}–L{selectedJob.totalPrintLayers}
                        </div>
                      )
                    }
                    return rows
                  })()
                ) : autoMachiningFrequency ? (
                  <p className="text-gray-600 text-xs py-2">
                    Auto frequency — actual schedule will appear here after generating toolpaths.
                  </p>
                ) : (
                  <>
                    <p className="text-[11px] text-gray-600 mb-2">
                      Estimated schedule (support-only layers excluded after generation):
                    </p>
                    {selectedJob.totalPrintLayers && Array.from(
                      { length: Math.ceil(selectedJob.totalPrintLayers / machineEveryN) },
                      (_, i) => {
                        const end   = (i + 1) * machineEveryN
                        const start = i * machineEveryN + 1
                        return (
                          <div key={i} className="space-y-0.5">
                            <div className="text-blue-400">▣ Print L{start}–L{Math.min(end, selectedJob.totalPrintLayers!)}</div>
                            <div className="pl-4 text-gray-600">⚙ CNC @ {machineEveryN} part layers</div>
                          </div>
                        )
                      }
                    )}
                  </>
                )}
              </div>
            ) : (
              <p className="text-gray-600">Select a sliced job to preview the process plan.</p>
            )}
          </div>
        </div>
      )}

      {activeTab === 'gcode' && toolpathGCode && (
        <GCodeTextPanel
          gcode={toolpathGCode}
          machinedLayers={machinedLayers}
          jobName={selectedJob?.name ?? ''}
          onShow3D={() => setActiveTab('preview3d')}
        />
      )}

      {activeTab === 'preview3d' && toolpathGCode && (
        <div className="space-y-4">
          <div className="flex items-center justify-between">
            <h3 className="text-white font-medium">CNC Toolpath — 3D Preview</h3>
            <div className="flex gap-3 text-xs text-gray-500">
              <span><span className="text-cyan-400">■</span> Rapid (G0)</span>
              <span><span className="text-yellow-300">■</span> Cut (G1)</span>
              <span><span className="text-blue-400">■</span> Printed geometry</span>
              <span><span className="text-orange-400">■</span> Support material</span>
              <span><span className="text-red-400">■</span> Unmachinable</span>
              <span className="text-gray-600">{machinedLayers.length} layers machined</span>
            </div>
          </div>
          <div className="bg-gray-950 rounded-xl border border-gray-700 overflow-hidden" style={{ height: '70vh' }}>
            <GCodePreview3D
              gcode={toolpathGCode}
              buildVolume={buildVolume}
              lineWidth={0.4}
              className="w-full h-full"
            />
          </div>
        </div>
      )}

      {activeTab === 'simulation' && toolpathGCode && printGCode && (
        <CncSimulation
          toolpathGCode={toolpathGCode}
          printGCode={printGCode}
          buildVolume={buildVolume}
          toolDiameterMm={selectedTool?.diameterMm ?? 3}
          unmachinableRegions={unmachinableRegions}
          bedPositionX={selectedMachine?.bedPositionXMm}
          bedPositionY={selectedMachine?.bedPositionYMm}
          originX={selectedMachine?.originXMm}
          originY={selectedMachine?.originYMm}
          className="flex-1"
        />
      )}
    </div>
  )
}

// ── G-code Text Panel ────────────────────────────────────────────────────────

function GCodeTextPanel({
  gcode, machinedLayers, jobName, onShow3D,
}: {
  gcode: string
  machinedLayers: number[]
  jobName: string
  onShow3D: () => void
}) {
  const lines = useMemo(() => gcode.split('\n'), [gcode])

  const stats = useMemo(() => {
    const moves        = lines.filter(l => /^G0[01]\s/i.test(l)).length
    const spindleStarts = lines.filter(l => /^M0?3\b/i.test(l)).length
    return { totalLines: lines.length, moves, spindleStarts }
  }, [lines])

  const colorLine = (line: string): React.ReactNode => {
    const trimmed = line.trimEnd()
    if (!trimmed) return <span>&nbsp;</span>
    if (trimmed.startsWith(';'))
      return <span className="text-gray-500 italic">{trimmed}</span>

    const ci      = trimmed.indexOf(';')
    const cmd     = ci >= 0 ? trimmed.slice(0, ci).trimEnd() : trimmed
    const comment = ci >= 0 ? trimmed.slice(ci) : ''
    if (!cmd) return <span className="text-gray-500 italic">{trimmed}</span>

    const colored = cmd.split(/\s+/).map((token, i) => {
      const u = token.toUpperCase()
      if (/^G0[01]$/.test(u))          return <span key={i} className="text-cyan-400">{token} </span>
      if (/^G0[23]$/.test(u))          return <span key={i} className="text-cyan-300">{token} </span>
      if (/^G\d+$/.test(u))            return <span key={i} className="text-cyan-500">{token} </span>
      if (/^M0?3$/.test(u))            return <span key={i} className="text-green-400">{token} </span>
      if (/^M0?5$/.test(u))            return <span key={i} className="text-red-400">{token} </span>
      if (/^M\d+$/.test(u))            return <span key={i} className="text-purple-400">{token} </span>
      if (/^[XYZIJ]-?[\d.]+$/.test(u)) return <span key={i} className="text-yellow-300">{token} </span>
      if (/^F[\d.]+$/.test(u))         return <span key={i} className="text-orange-300">{token} </span>
      if (/^S[\d.]+$/.test(u))         return <span key={i} className="text-green-300">{token} </span>
      return <span key={i} className="text-gray-200">{token} </span>
    })

    return (
      <>
        {colored}
        {comment && <span className="text-gray-500 italic">{comment}</span>}
      </>
    )
  }

  return (
    <div className="space-y-4">
      <div className="grid grid-cols-4 gap-4">
        <StatCard label="Layers machined" value={machinedLayers.length}            color="purple" />
        <StatCard label="Total G-code lines" value={stats.totalLines.toLocaleString()} color="blue" />
        <StatCard label="Move commands"    value={stats.moves.toLocaleString()}    color="cyan" />
        <StatCard label="Spindle starts"   value={stats.spindleStarts}             color="green" />
      </div>

      <div className="flex justify-end">
        <button
          onClick={onShow3D}
          className="px-4 py-2 bg-cyan-800 hover:bg-cyan-700 text-cyan-200 rounded-lg text-sm border border-cyan-600 transition"
        >
          Switch to 3D Preview →
        </button>
      </div>

      <div className="bg-gray-950 border border-gray-800 rounded-xl overflow-hidden">
        <div className="flex items-center justify-between px-4 py-2 border-b border-gray-800 bg-gray-900">
          <span className="text-sm text-gray-400 font-mono">toolpath.gcode — {jobName}</span>
          <div className="flex gap-3 text-xs text-gray-600">
            <span><span className="text-cyan-400">■</span> G-code</span>
            <span><span className="text-yellow-300">■</span> Coords</span>
            <span><span className="text-green-400">■</span> Spindle on</span>
            <span><span className="text-red-400">■</span> Spindle off</span>
          </div>
        </div>
        <div className="overflow-auto max-h-[60vh] text-xs font-mono leading-5">
          <table className="w-full border-collapse">
            <tbody>
              {lines.map((line, i) => (
                <tr key={i} className="hover:bg-gray-900/50">
                  <td className="select-none text-right text-gray-700 px-3 py-px w-12 border-r border-gray-800/50">
                    {i + 1}
                  </td>
                  <td className="px-4 py-px whitespace-pre">{colorLine(line)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  )
}

function StatCard({ label, value, color }: { label: string; value: string | number; color: string }) {
  const colors: Record<string, string> = {
    purple: 'text-purple-400 border-purple-800',
    blue:   'text-blue-400   border-blue-800',
    cyan:   'text-cyan-400   border-cyan-800',
    green:  'text-green-400  border-green-800',
  }
  return (
    <div className={`bg-gray-900 border rounded-xl p-4 ${colors[color] ?? colors.blue}`}>
      <div className="text-2xl font-bold">{value}</div>
      <div className="text-xs text-gray-500 mt-1">{label}</div>
    </div>
  )
}

function TabBtn({ active, disabled, onClick, children }: {
  active: boolean; disabled?: boolean; onClick: () => void; children: React.ReactNode
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className={[
        'flex items-center gap-1 px-4 py-2 text-sm rounded-t-lg border-b-2 transition',
        active ? 'border-primary text-white' : 'border-transparent text-gray-500 hover:text-gray-300',
        disabled ? 'opacity-40 cursor-not-allowed' : '',
      ].join(' ')}
    >
      {children}
    </button>
  )
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <label className="text-sm text-gray-400">{label}</label>
      {children}
    </div>
  )
}
