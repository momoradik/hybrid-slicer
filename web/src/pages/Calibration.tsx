import { useState, useMemo, useEffect, useRef } from 'react'
import { useQuery } from '@tanstack/react-query'
import { printProfilesApi, machineProfilesApi } from '../api/client'
import GCodePreview3D, { type BedInfo } from '../components/viewer/GCodePreview3D'
import PelletCalibration from './PelletCalibration'

// ── G-code helpers ────────────────────────────────────────────────────────────

function calcE(nozzle: number, h: number, len: number, diam: number, flowPct: number): number {
  return ((nozzle * h * len) / ((Math.PI / 4) * diam * diam)) * (flowPct / 100)
}

// ── Print calibration G-code generators ───────────────────────────────────────

function flowLinesGCode(
  nozzle: number, h: number, speed: number, diam: number, flowPct: number,
  extTemp: number, bedTemp: number, ox: number, oy: number,
): string {
  const steps = [80, 85, 90, 95, 100, 105, 110, 115, 120]
  const lineLen = 80
  const yGap = 10
  const startX = 20 + ox
  const startY = 20 + oy

  const lines: string[] = [
    '; ============================================================',
    '; HybridSlicer — Flow Rate Calibration',
    `; Nozzle: ${nozzle} mm  Layer: ${h} mm  Speed: ${speed} mm/s`,
    `; Filament diameter: ${diam} mm  Base flow: ${flowPct}%`,
    `; Offset: X=${ox} Y=${oy}`,
    '; Print single lines at different flow rates.',
    '; Measure width with calipers — target = nozzle diameter.',
    '; ============================================================',
    '',
    `M104 S${extTemp}`, `M140 S${bedTemp}`,
    `M109 S${extTemp}`, `M190 S${bedTemp}`,
    'G28', 'G92 E0', 'G90', 'M82', '',
  ]

  steps.forEach((flow, i) => {
    const y = startY + i * yGap
    const e = calcE(nozzle, h, lineLen, diam, flow)
    lines.push(`; --- Flow ${flow}% ---`)
    lines.push(`G0 X${startX.toFixed(1)} Y${y.toFixed(1)} Z${h.toFixed(3)} F5000`)
    lines.push('G92 E0')
    lines.push(`G1 X${(startX + lineLen).toFixed(1)} Y${y.toFixed(1)} E${e.toFixed(4)} F${(speed * 60).toFixed(0)}`)

    // Print label: small zigzag after line as identifier (number of bumps = tens digit)
    const labelX = startX + lineLen + 5
    const bumps = Math.floor(flow / 10) - 7 // 80%=1 bump, 90%=2, 100%=3, etc.
    for (let b = 0; b < bumps; b++) {
      const bx = labelX + b * 3
      lines.push(`G0 X${bx.toFixed(1)} Y${(y - 1.5).toFixed(1)} Z${h.toFixed(3)} F5000`)
      lines.push('G92 E0')
      lines.push(`G1 X${(bx + 1.5).toFixed(1)} Y${(y + 1.5).toFixed(1)} E${calcE(nozzle, h, 4, diam, flow).toFixed(4)} F${(speed * 60).toFixed(0)}`)
    }

    lines.push(`G1 E${Math.max(0, e - 2).toFixed(4)} F2700 ; retract`)
    lines.push(`G0 Z${(h + 2).toFixed(2)} F3000`)
    lines.push('')
  })

  lines.push('M104 S0', 'M140 S0', 'G28 X0 Y0', 'M84', '; End of flow calibration')
  return lines.join('\n')
}

function tempTestGCode(
  nozzle: number, h: number, speed: number, diam: number, flowPct: number,
  tempMin: number, tempMax: number, tempStep: number, bedTemp: number,
  ox: number, oy: number,
): string {
  const side = 20
  const cx = 50 + ox, cy = 50 + oy
  const corners = [
    [cx - side / 2, cy - side / 2], [cx + side / 2, cy - side / 2],
    [cx + side / 2, cy + side / 2], [cx - side / 2, cy + side / 2],
  ]
  const layersPerSection = Math.max(1, Math.round(5 / h))
  const temps: number[] = []
  for (let t = tempMin; t <= tempMax; t += tempStep) temps.push(t)

  const lines: string[] = [
    '; ============================================================',
    '; HybridSlicer — Temperature Calibration Tower',
    `; Nozzle: ${nozzle} mm  Layer: ${h} mm`,
    `; Temps: ${temps.join(', ')} °C  (${layersPerSection} layers each = ${(layersPerSection * h).toFixed(1)} mm)`,
    `; Total height: ${(temps.length * layersPerSection * h).toFixed(1)} mm`,
    '; Inspect each section for stringing, surface quality, layer adhesion.',
    '; ============================================================',
    '',
    `M140 S${bedTemp}`, `M190 S${bedTemp}`,
    `M104 S${temps[0]}`, `M109 S${temps[0]}`,
    'G28', 'G92 E0', 'G90', 'M82', '',
  ]

  let layerNum = 0
  temps.forEach(temp => {
    lines.push('', `; === ${temp}°C section ===`)
    lines.push(`M104 S${temp}`, `M109 S${temp}`)

    for (let s = 0; s < layersPerSection; s++) {
      layerNum++
      const z = layerNum * h
      const ePerSide = calcE(nozzle, h, side, diam, flowPct)
      let eCum = 0

      lines.push(`; Layer ${layerNum} z=${z.toFixed(3)}`)
      lines.push(`G0 Z${z.toFixed(3)} F3000`)
      lines.push(`G0 X${corners[0][0].toFixed(1)} Y${corners[0][1].toFixed(1)} F5000`)
      lines.push('G92 E0')
      for (let c = 0; c < 4; c++) {
        const next = corners[(c + 1) % 4]
        eCum += ePerSide
        lines.push(`G1 X${next[0].toFixed(1)} Y${next[1].toFixed(1)} E${eCum.toFixed(4)} F${(speed * 60).toFixed(0)}`)
      }
      lines.push(`G1 E${Math.max(0, eCum - 2).toFixed(4)} F2700 ; retract`)
    }
  })

  lines.push('', 'M104 S0', 'M140 S0', 'G28 X0 Y0', 'M84', '; End of temperature test')
  return lines.join('\n')
}

function calibCubeGCode(
  nozzle: number, h: number, speed: number, diam: number, flowPct: number,
  extTemp: number, bedTemp: number, ox: number, oy: number,
): string {
  const side = 20
  const height = 20
  const cx = 50 + ox, cy = 50 + oy
  const corners = [
    [cx - side / 2, cy - side / 2], [cx + side / 2, cy - side / 2],
    [cx + side / 2, cy + side / 2], [cx - side / 2, cy + side / 2],
  ]
  const totalLayers = Math.round(height / h)
  const ePerSide = calcE(nozzle, h, side, diam, flowPct)

  const lines: string[] = [
    '; ============================================================',
    '; HybridSlicer — Calibration Cube (20×20×20 mm)',
    `; Nozzle: ${nozzle} mm  Layer: ${h} mm  Flow: ${flowPct}%`,
    `; ${totalLayers} layers, single perimeter`,
    '; Measure X, Y, Z dimensions with calipers.',
    '; Target: 20.00 mm on each axis.',
    '; ============================================================',
    '',
    `M104 S${extTemp}`, `M140 S${bedTemp}`,
    `M109 S${extTemp}`, `M190 S${bedTemp}`,
    'G28', 'G92 E0', 'G90', 'M82', '',
  ]

  for (let layer = 1; layer <= totalLayers; layer++) {
    const z = layer * h
    let eCum = 0
    lines.push(`; Layer ${layer}`)
    lines.push(`G0 Z${z.toFixed(3)} F3000`)
    lines.push(`G0 X${corners[0][0].toFixed(1)} Y${corners[0][1].toFixed(1)} F5000`)
    lines.push('G92 E0')
    for (let c = 0; c < 4; c++) {
      const next = corners[(c + 1) % 4]
      eCum += ePerSide
      lines.push(`G1 X${next[0].toFixed(1)} Y${next[1].toFixed(1)} E${eCum.toFixed(4)} F${(speed * 60).toFixed(0)}`)
    }
    lines.push(`G1 E${Math.max(0, eCum - 2).toFixed(4)} F2700 ; retract`)
  }

  lines.push('', 'M104 S0', 'M140 S0', 'G28 X0 Y0', 'M84', '; End of calibration cube')
  return lines.join('\n')
}

// ── Number input with proper clearing ─────────────────────────────────────────

function NumIn({ value, min, max, step = 1, onChange, className = 'input w-24 text-center' }: {
  value: number; min?: number; max?: number; step?: number
  onChange: (v: number) => void; className?: string
}) {
  const [raw, setRaw] = useState(String(value))
  const ref = useRef<HTMLInputElement>(null)
  useEffect(() => {
    if (document.activeElement !== ref.current) setRaw(String(value))
  }, [value])
  return (
    <input ref={ref} type="number" min={min} max={max} step={step} value={raw}
      onChange={e => { setRaw(e.target.value); const n = +e.target.value; if (e.target.value !== '' && !isNaN(n)) onChange(n) }}
      onBlur={() => { const n = +raw; if (raw === '' || isNaN(n)) { const fb = min ?? 0; setRaw(String(fb)); onChange(fb) } else { setRaw(String(n)); onChange(n) } }}
      className={className} />
  )
}

// ── Download helper ───────────────────────────────────────────────────────────

function downloadGCode(name: string, content: string) {
  const blob = new Blob([content], { type: 'text/plain' })
  const a = document.createElement('a')
  a.href = URL.createObjectURL(blob)
  a.download = name
  a.click()
  URL.revokeObjectURL(a.href)
}

// ── Tab: Print Calibration ────────────────────────────────────────────────────

function PrintCalibrationTab() {
  const { data: profiles = [] } = useQuery({ queryKey: ['printProfiles'], queryFn: printProfilesApi.getAll })
  const { data: machines = [] } = useQuery({ queryKey: ['machineProfiles'], queryFn: machineProfilesApi.getAll })

  const [profileId, setProfileId] = useState('')
  const [machineId, setMachineId] = useState('')
  const [previewGCode, setPreviewGCode] = useState<string | null>(null)

  // Temp test params
  const [tempMin, setTempMin] = useState(190)
  const [tempMax, setTempMax] = useState(230)
  const [tempStep, setTempStep] = useState(5)

  const profile = profiles.find(p => p.id === profileId)
  const machine = machines.find(m => m.id === machineId)

  // Auto-select first profile/machine
  useEffect(() => { if (!profileId && profiles.length) setProfileId(profiles[0].id) }, [profiles, profileId])
  useEffect(() => { if (!machineId && machines.length) setMachineId(machines[0].id) }, [machines, machineId])

  const nozzle = profile?.nozzleDiameterMm && profile.nozzleDiameterMm > 0
    ? profile.nozzleDiameterMm : 0.4
  const h = profile?.layerHeightMm ?? 0.2
  const speed = profile?.printSpeedMmS ?? 50
  const diam = profile?.pelletModeEnabled ? (profile.virtualFilamentDiameterMm ?? 1.0) : 1.75
  const flowPct = profile?.materialFlowPct ?? 100
  const extTemp = profile?.printTemperatureDegC ?? 210
  const bedTemp = profile?.bedTemperatureDegC ?? 60

  const ox = machine?.bedPositionXMm ?? 0
  const oy = machine?.bedPositionYMm ?? 0

  const previewBeds = useMemo<BedInfo[]>(() => {
    if (!machine) return [{ index: 0, widthMm: 220, depthMm: 220, positionXMm: 0, positionYMm: 0 }]
    try {
      const raw = JSON.parse(machine.bedsJson || '[]') as any[]
      if (raw.length > 0)
        return raw.map((b: any, i: number) => ({
          index: b.index ?? b.Index ?? i,
          widthMm: b.widthMm ?? b.WidthMm ?? machine.bedWidthMm,
          depthMm: b.depthMm ?? b.DepthMm ?? machine.bedDepthMm,
          positionXMm: b.positionXMm ?? b.PositionXMm ?? 0,
          positionYMm: b.positionYMm ?? b.PositionYMm ?? 0,
        }))
    } catch { /* fall through */ }
    return [{ index: 0, widthMm: machine.bedWidthMm, depthMm: machine.bedDepthMm, positionXMm: machine.bedPositionXMm ?? 0, positionYMm: machine.bedPositionYMm ?? 0 }]
  }, [machine])

  const previewVolume = useMemo(() => ({
    width: machine?.travelXMm ?? machine?.bedWidthMm ?? 220,
    depth: machine?.travelYMm ?? machine?.bedDepthMm ?? 220,
    height: machine?.travelZMm ?? machine?.bedHeightMm ?? 250,
  }), [machine])

  function doPreview(gcode: string) { setPreviewGCode(gcode) }
  function doDownload(name: string, gcode: string) { setPreviewGCode(gcode); downloadGCode(name, gcode) }

  return (
    <div className="space-y-5">
      {/* Selectors */}
      <div className="grid grid-cols-2 gap-4">
        <div className="space-y-1">
          <label className="text-xs text-gray-400">Print Profile</label>
          <select className="input w-full" value={profileId} onChange={e => setProfileId(e.target.value)}>
            <option value="">Select...</option>
            {profiles.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
        </div>
        <div className="space-y-1">
          <label className="text-xs text-gray-400">Machine</label>
          <select className="input w-full" value={machineId} onChange={e => setMachineId(e.target.value)}>
            <option value="">Select...</option>
            {machines.map(m => <option key={m.id} value={m.id}>{m.name}</option>)}
          </select>
        </div>
      </div>

      {/* Active settings summary */}
      {profile && (
        <div className="flex flex-wrap gap-2 text-xs">
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Nozzle: {nozzle} mm</span>
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Layer: {h} mm</span>
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Speed: {speed} mm/s</span>
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Flow: {flowPct}%</span>
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Ext: {extTemp}°C</span>
          <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-gray-300">Bed: {bedTemp}°C</span>
          {ox !== 0 || oy !== 0 ? (
            <span className="bg-gray-800 border border-gray-700 rounded px-2 py-1 text-amber-400">Offset: +{ox}, +{oy}</span>
          ) : null}
        </div>
      )}

      {/* ── Flow Rate Test ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-3">
        <h3 className="font-medium text-white">Flow Rate Calibration</h3>
        <p className="text-xs text-gray-500">
          Prints 9 lines at 80–120% flow. Measure each line's width with calipers.
          The line closest to your nozzle diameter ({nozzle} mm) is your ideal flow rate.
        </p>
        <div className="flex gap-2">
          <button onClick={() => { const g = flowLinesGCode(nozzle, h, speed, diam, flowPct, extTemp, bedTemp, ox, oy); doPreview(g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-gray-700 hover:bg-gray-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Preview
          </button>
          <button onClick={() => { const g = flowLinesGCode(nozzle, h, speed, diam, flowPct, extTemp, bedTemp, ox, oy); doDownload('flow_calibration.gcode', g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-amber-700 hover:bg-amber-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Download G-code
          </button>
        </div>
      </div>

      {/* ── Temperature Test ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-3">
        <h3 className="font-medium text-white">Temperature Calibration Tower</h3>
        <p className="text-xs text-gray-500">
          Prints a square tower with each section at a different temperature.
          Inspect for stringing, surface quality, and layer adhesion.
        </p>
        <div className="flex items-center gap-4 flex-wrap">
          <div className="flex items-center gap-2">
            <span className="text-xs text-gray-400">Min</span>
            <NumIn value={tempMin} min={150} max={350} step={5} onChange={setTempMin} />
          </div>
          <div className="flex items-center gap-2">
            <span className="text-xs text-gray-400">Max</span>
            <NumIn value={tempMax} min={150} max={350} step={5} onChange={setTempMax} />
          </div>
          <div className="flex items-center gap-2">
            <span className="text-xs text-gray-400">Step</span>
            <NumIn value={tempStep} min={1} max={20} step={1} onChange={setTempStep} />
          </div>
          <span className="text-xs text-gray-500">°C</span>
        </div>
        <div className="flex gap-2">
          <button onClick={() => { const g = tempTestGCode(nozzle, h, speed, diam, flowPct, tempMin, tempMax, tempStep, bedTemp, ox, oy); doPreview(g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-gray-700 hover:bg-gray-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Preview
          </button>
          <button onClick={() => { const g = tempTestGCode(nozzle, h, speed, diam, flowPct, tempMin, tempMax, tempStep, bedTemp, ox, oy); doDownload('temp_calibration.gcode', g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-blue-700 hover:bg-blue-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Download G-code
          </button>
        </div>
      </div>

      {/* ── Calibration Cube ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-3">
        <h3 className="font-medium text-white">Calibration Cube (20×20×20 mm)</h3>
        <p className="text-xs text-gray-500">
          Standard calibration cube for dimensional accuracy. Measure all three axes with calipers.
          Target: 20.00 mm. Deviation indicates steps/mm or flow issues.
        </p>
        <div className="flex gap-2">
          <button onClick={() => { const g = calibCubeGCode(nozzle, h, speed, diam, flowPct, extTemp, bedTemp, ox, oy); doPreview(g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-gray-700 hover:bg-gray-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Preview
          </button>
          <button onClick={() => { const g = calibCubeGCode(nozzle, h, speed, diam, flowPct, extTemp, bedTemp, ox, oy); doDownload('calibration_cube.gcode', g) }}
            disabled={!profile}
            className="px-4 py-2.5 bg-green-700 hover:bg-green-600 disabled:opacity-40 text-white rounded-lg text-sm font-medium transition flex-1">
            Download G-code
          </button>
        </div>
      </div>

      {/* ── G-code Preview ── */}
      {previewGCode && (
        <div className="bg-gray-900 border border-gray-800 rounded-xl overflow-hidden">
          <div className="flex items-center justify-between px-5 py-3 border-b border-gray-800">
            <h3 className="font-medium text-white">G-code Preview</h3>
            <button onClick={() => setPreviewGCode(null)}
              className="text-xs text-gray-500 hover:text-gray-300 transition">
              Close
            </button>
          </div>
          <div className="h-[500px]">
            <GCodePreview3D
              gcode={previewGCode}
              buildVolume={previewVolume}
              lineWidth={nozzle}
              className="w-full h-full"
              travelX={machine?.travelXMm}
              travelY={machine?.travelYMm}
              travelZ={machine?.travelZMm}
              beds={previewBeds}
            />
          </div>
        </div>
      )}
    </div>
  )
}

// ── Main Calibration Page ─────────────────────────────────────────────────────

type CalibTab = 'print' | 'pellet'

export default function Calibration() {
  const [tab, setTab] = useState<CalibTab>('print')

  const tabs: { key: CalibTab; label: string }[] = [
    { key: 'print',  label: 'Print Calibration' },
    { key: 'pellet', label: 'Pellet Calibration' },
  ]

  return (
    <div className="space-y-6">
      <h2 className="text-2xl font-semibold text-white">Calibration</h2>

      {/* Tab bar */}
      <div className="flex gap-1 bg-gray-900 border border-gray-800 rounded-xl p-1">
        {tabs.map(t => (
          <button key={t.key} onClick={() => setTab(t.key)}
            className={`flex-1 py-2.5 text-sm font-medium rounded-lg transition ${
              tab === t.key
                ? 'bg-primary/20 text-primary-300 border border-primary/30'
                : 'text-gray-400 hover:text-gray-200 hover:bg-gray-800 border border-transparent'
            }`}>
            {t.label}
          </button>
        ))}
      </div>

      {/* Tab content */}
      {tab === 'print'  && <PrintCalibrationTab />}
      {tab === 'pellet' && <PelletCalibration />}
    </div>
  )
}
