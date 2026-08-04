import { useState, useMemo } from 'react'
import { Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import { printProfilesApi, machineProfilesApi } from '../api/client'
import type { PrintProfile } from '../types'

// ── G-code generation helpers ─────────────────────────────────────────────────

const RETRACT_MM   = 2     // mm retract after each line / perimeter
const RETRACT_FMPM = 2700  // retract feed rate mm/min (45 mm/s)

function calcE(b: number, h: number, len: number, dF: number, flowPct: number): number {
  return ((b * h * len) / ((Math.PI / 4) * dF * dF)) * (flowPct / 100)
}

function effectiveDiam(p: PrintProfile): number {
  return p.pelletModeEnabled ? p.virtualFilamentDiameterMm : 1.75
}

function effectiveNozzle(p: PrintProfile): number {
  return p.nozzleDiameterMm > 0 ? p.nozzleDiameterMm : 0.4
}

/** Shared startup G-code: heat, home, prime line */
function gcodeHeader(profile: PrintProfile, ox = 0, oy = 0): string {
  const nozzle = effectiveNozzle(profile)
  const dF     = effectiveDiam(profile)
  const h      = profile.layerHeightMm
  const primeE = calcE(nozzle, h, 60, dF, profile.materialFlowPct)

  return [
    '; ============================================================',
    '; HybridSlicer — Pellet Calibration G-code',
    `; Profile        : ${profile.name}`,
    `; Nozzle         : ${nozzle} mm`,
    `; Layer height   : ${profile.layerHeightMm} mm`,
    `; Virtual diam   : ${dF} mm  (pellet mode: ${profile.pelletModeEnabled ? 'ON' : 'OFF'})`,
    `; Base flow      : ${profile.materialFlowPct} %`,
    `; Machine offset : X=${ox} Y=${oy}`,
    '; ============================================================',
    '',
    `M104 S${profile.printTemperatureDegC}   ; set extruder temp (no wait)`,
    `M140 S${profile.bedTemperatureDegC}     ; set bed temp (no wait)`,
    `M109 S${profile.printTemperatureDegC}   ; wait for extruder`,
    `M190 S${profile.bedTemperatureDegC}     ; wait for bed`,
    'G28          ; home all axes',
    'G92 E0       ; reset extruder position',
    'G90          ; absolute XYZ positioning',
    'M82          ; absolute extrusion mode',
    '',
    '; --- Prime line ---',
    `G0 Z${(h * 3).toFixed(2)} F3000`,
    `G0 X${(10 + ox).toFixed(1)} Y${(5 + oy).toFixed(1)} F5000`,
    `G0 Z${h.toFixed(3)} F1000`,
    'G92 E0',
    `G1 X${(70 + ox).toFixed(1)} Y${(5 + oy).toFixed(1)} E${primeE.toFixed(4)} F${(profile.printSpeedMmS * 60).toFixed(0)}`,
    `G1 E${(primeE - RETRACT_MM).toFixed(4)} F${RETRACT_FMPM}   ; retract`,
    `G0 Z${(h * 3).toFixed(2)} F3000`,
    '',
  ].join('\n')
}

function gcodeFooter(): string {
  return [
    '',
    '; --- End ---',
    'M104 S0     ; turn off extruder',
    'M140 S0     ; turn off bed',
    'G28 X0 Y0   ; home X Y',
    'M84         ; disable motors',
    '; ============================================================',
    '; End of calibration print',
    '; ============================================================',
  ].join('\n')
}

function generateLineTest(profile: PrintProfile, flowSteps: number[], ox = 0, oy = 0): string {
  const nozzle  = effectiveNozzle(profile)
  const dF      = effectiveDiam(profile)
  const h       = profile.layerHeightMm
  const speed   = profile.printSpeedMmS
  const startX  = 10 + ox
  const lineLen = 100
  const yGap    = 8
  const yStart0 = 20 + oy
  const z       = h

  const out: string[] = [
    '; ── Flow Line Test ──',
    `; Lines: ${flowSteps.join(', ')} %`,
    `; Length: ${lineLen} mm each, spaced ${yGap} mm apart`,
    `; Measure wall width with calipers after printing.`,
    `; The line closest to ${nozzle} mm wide is your best flow %.`,
    '',
    `G0 Z${(z + 5).toFixed(2)} F3000`,
  ]

  flowSteps.forEach((flowPct, i) => {
    const y = yStart0 + i * yGap
    const e = calcE(nozzle, h, lineLen, dF, flowPct)
    const eRetract = Math.max(0, e - RETRACT_MM)

    out.push('')
    out.push(`; --- Flow ${flowPct}% ---`)
    out.push(`G0 X${startX.toFixed(1)} Y${y.toFixed(1)} F5000`)
    out.push(`G0 Z${z.toFixed(3)} F1000`)
    out.push('G92 E0')
    out.push(`G1 X${(startX + lineLen).toFixed(1)} Y${y.toFixed(1)} E${e.toFixed(4)} F${(speed * 60).toFixed(0)}`)
    out.push(`G1 E${eRetract.toFixed(4)} F${RETRACT_FMPM}   ; retract`)
    out.push(`G0 Z${(z + 2).toFixed(2)} F3000`)
  })

  return out.join('\n')
}

function generateFlowTower(profile: PrintProfile, flowSteps: number[], ox = 0, oy = 0): string {
  const nozzle        = effectiveNozzle(profile)
  const dF            = effectiveDiam(profile)
  const h             = profile.layerHeightMm
  const speed         = profile.printSpeedMmS
  const sectionLayers = Math.max(1, Math.round(5 / h))
  const side          = 20
  const cx = 50 + ox, cy = 50 + oy
  const corners = [
    [cx - side / 2, cy - side / 2],
    [cx + side / 2, cy - side / 2],
    [cx + side / 2, cy + side / 2],
    [cx - side / 2, cy + side / 2],
  ]

  const out: string[] = [
    `; Total tower height: ${(flowSteps.length * sectionLayers * h).toFixed(2)} mm`,
    `; (${flowSteps.length} sections × ${sectionLayers} layers × ${h} mm)`,
    '; ── Flow Tower ──',
    `; Sections: ${flowSteps.join(', ')} %`,
    '; Square perimeter 20×20 mm centered at (50,50)',
    '',
  ]

  let layerNum = 0
  flowSteps.forEach(flowPct => {
    out.push('')
    out.push(`; === Flow ${flowPct}% section ===`)
    for (let s = 0; s < sectionLayers; s++) {
      layerNum++
      const z         = layerNum * h
      const ePerSide  = calcE(nozzle, h, side, dF, flowPct)
      let eCum        = 0

      out.push(`; Layer ${layerNum}  z=${z.toFixed(3)}`)
      out.push(`G0 Z${z.toFixed(3)} F3000`)
      out.push(`G0 X${corners[0][0].toFixed(1)} Y${corners[0][1].toFixed(1)} F5000`)
      out.push('G92 E0')
      for (let c = 0; c < 4; c++) {
        const next = corners[(c + 1) % 4]
        eCum += ePerSide
        out.push(`G1 X${next[0].toFixed(1)} Y${next[1].toFixed(1)} E${eCum.toFixed(4)} F${(speed * 60).toFixed(0)}`)
      }
      const eRetract = Math.max(0, eCum - RETRACT_MM)
      out.push(`G1 E${eRetract.toFixed(4)} F${RETRACT_FMPM}   ; retract`)
    }
    out.push(`; ↑ Measure wall width at this section (target ≈ ${nozzle} mm)`)
  })

  return out.join('\n')
}

function generateCubeTest(profile: PrintProfile, ox = 0, oy = 0): string {
  const nozzle      = effectiveNozzle(profile)
  const dF          = effectiveDiam(profile)
  const h           = profile.layerHeightMm
  const speed       = profile.printSpeedMmS
  const totalLayers = Math.round(10 / h)
  const side        = 20
  const cx = 50 + ox, cy = 50 + oy
  const corners = [
    [cx - side / 2, cy - side / 2],
    [cx + side / 2, cy - side / 2],
    [cx + side / 2, cy + side / 2],
    [cx - side / 2, cy + side / 2],
  ]
  const ePerSide = calcE(nozzle, h, side, dF, profile.materialFlowPct)

  const out: string[] = [
    `; ── Cube Test (20×20×10 mm, single wall) ──`,
    `; ${totalLayers} layers @ ${h} mm, flow=${profile.materialFlowPct}%`,
    `; Wall thickness should be ≈ ${nozzle} mm after printing`,
    '',
  ]

  for (let layer = 1; layer <= totalLayers; layer++) {
    const z    = layer * h
    let eCum   = 0
    out.push(`; Layer ${layer}`)
    out.push(`G0 Z${z.toFixed(3)} F3000`)
    out.push(`G0 X${corners[0][0].toFixed(1)} Y${corners[0][1].toFixed(1)} F5000`)
    out.push('G92 E0')
    for (let c = 0; c < 4; c++) {
      const next = corners[(c + 1) % 4]
      eCum += ePerSide
      out.push(`G1 X${next[0].toFixed(1)} Y${next[1].toFixed(1)} E${eCum.toFixed(4)} F${(speed * 60).toFixed(0)}`)
    }
    const eRetract = Math.max(0, eCum - RETRACT_MM)
    out.push(`G1 E${eRetract.toFixed(4)} F${RETRACT_FMPM}   ; retract`)
  }

  return out.join('\n')
}

function downloadGCode(filename: string, content: string) {
  const blob = new Blob([content], { type: 'text/plain' })
  const url  = URL.createObjectURL(blob)
  const a    = Object.assign(document.createElement('a'), { href: url, download: filename })
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
}

// ── Component ─────────────────────────────────────────────────────────────────

type FlowConfig = { min: number; max: number; step: number }

function flowSteps({ min, max, step }: FlowConfig): number[] {
  const result: number[] = []
  for (let v = min; v <= max + 0.001; v += step)
    result.push(Math.round(v))
  return result
}

// Result advisor: given measured width vs target, suggest action
function getLineAdvice(measured: number, target: number, currentVDiam: number): {
  verdict: 'over' | 'under' | 'good'
  msg: string
  suggestedVDiam?: number
} {
  const ratio = measured / target
  if (ratio >= 0.95 && ratio <= 1.05) {
    return { verdict: 'good', msg: 'Good — this flow % is your target. Set it as Base Flow in your profile.' }
  }
  if (ratio > 1.05) {
    // Over-extrusion: increase virtual diameter to reduce E output
    const suggestedVDiam = parseFloat((currentVDiam * Math.sqrt(ratio)).toFixed(2))
    return {
      verdict: 'over',
      msg: `All lines too wide (${ratio.toFixed(2)}× target). Increase Virtual Diameter to reduce volumetric output.`,
      suggestedVDiam,
    }
  }
  // Under-extrusion: decrease virtual diameter to increase E output
  const suggestedVDiam = parseFloat((currentVDiam * Math.sqrt(ratio)).toFixed(2))
  return {
    verdict: 'under',
    msg: `All lines too narrow (${ratio.toFixed(2)}× target). Decrease Virtual Diameter to increase volumetric output.`,
    suggestedVDiam,
  }
}

export default function PelletCalibration() {
  const { data: profiles = [], isLoading } = useQuery({
    queryKey: ['printProfiles'],
    queryFn:  printProfilesApi.getAll,
  })
  const { data: machines = [] } = useQuery({
    queryKey: ['machineProfiles'],
    queryFn:  machineProfilesApi.getAll,
  })

  const pelletProfiles = profiles.filter(p => p.pelletModeEnabled)

  const [selectedId,    setSelectedId]    = useState<string>('')
  const [machineId,     setMachineId]     = useState<string>('')
  const [lineFlow,      setLineFlow]      = useState<FlowConfig>({ min: 80, max: 120, step: 5 })
  const [towerFlow,     setTowerFlow]     = useState<FlowConfig>({ min: 80, max: 120, step: 10 })
  const [tutorialOpen,  setTutorialOpen]  = useState(true)
  const [activeSection, setActiveSection] = useState<'concepts' | 'formula' | 'tips' | null>('concepts')

  // Result advisor state
  const [measuredWidth, setMeasuredWidth] = useState('')

  const profile    = useMemo(() => profiles.find(p => p.id === selectedId) ?? null, [profiles, selectedId])
  const machine    = useMemo(() => machines.find(m => m.id === machineId) ?? null, [machines, machineId])

  // Machine bed offset: coordinates where the bed starts in machine space
  const bedOffsetX = machine?.bedPositionXMm ?? 0
  const bedOffsetY = machine?.bedPositionYMm ?? 0
  const lineSteps  = useMemo(() => flowSteps(lineFlow),  [lineFlow])
  const towerSteps = useMemo(() => flowSteps(towerFlow), [towerFlow])

  const nozzle = profile ? effectiveNozzle(profile) : 0.4
  const vDiam  = profile ? effectiveDiam(profile) : 1.0

  const advice = useMemo(() => {
    if (!profile || !measuredWidth) return null
    const w = parseFloat(measuredWidth)
    if (isNaN(w) || w <= 0) return null
    return getLineAdvice(w, nozzle, vDiam)
  }, [measuredWidth, nozzle, vDiam, profile])

  function generate(type: 'line' | 'tower' | 'cube') {
    if (!profile) return
    const ox = bedOffsetX, oy = bedOffsetY
    const head = gcodeHeader(profile, ox, oy)
    const foot = gcodeFooter()
    let body = '', name = ''
    if (type === 'line') {
      body = generateLineTest(profile, lineSteps, ox, oy)
      name = `${profile.name}_flow_lines.gcode`
    } else if (type === 'tower') {
      body = generateFlowTower(profile, towerSteps, ox, oy)
      name = `${profile.name}_flow_tower.gcode`
    } else {
      body = generateCubeTest(profile, ox, oy)
      name = `${profile.name}_cube_test.gcode`
    }
    downloadGCode(name, head + '\n' + body + '\n' + foot)
  }

  return (
    <div className="space-y-6 max-w-3xl">

      {/* ── Header ── */}
      <div>
        <h2 className="text-2xl font-semibold text-white">Pellet Calibration</h2>
        <p className="text-sm text-gray-500 mt-1">
          Step-by-step guide to dial in your pellet extruder's virtual filament diameter and flow rate.
        </p>
      </div>

      {/* ── Tutorial Panel ── */}
      <div className="bg-gray-900 border border-gray-700 rounded-xl overflow-hidden">
        <button
          onClick={() => setTutorialOpen(o => !o)}
          className="w-full flex items-center justify-between px-5 py-3.5 text-left hover:bg-gray-800/60 transition"
        >
          <div className="flex items-center gap-2.5">
            <span className="text-base font-medium text-white">How Pellet Calibration Works</span>
            <span className="text-[10px] bg-blue-900/60 text-blue-300 border border-blue-700/50 rounded px-1.5 py-0.5 font-medium tracking-wide">TUTORIAL</span>
          </div>
          <span className="text-gray-500 text-lg select-none">{tutorialOpen ? '▲' : '▼'}</span>
        </button>

        {tutorialOpen && (
          <div className="border-t border-gray-800">
            {/* Sub-section tabs */}
            <div className="flex border-b border-gray-800">
              {(['concepts', 'formula', 'tips'] as const).map(sec => (
                <button
                  key={sec}
                  onClick={() => setActiveSection(s => s === sec ? null : sec)}
                  className={`px-4 py-2 text-xs font-medium transition border-b-2 -mb-px ${
                    activeSection === sec
                      ? 'border-blue-500 text-blue-300'
                      : 'border-transparent text-gray-500 hover:text-gray-300'
                  }`}
                >
                  {sec === 'concepts' ? 'Core Concepts' : sec === 'formula' ? 'The Formula' : 'Tips & Troubleshooting'}
                </button>
              ))}
            </div>

            {activeSection === 'concepts' && (
              <div className="p-5 space-y-4 text-sm text-gray-400">
                <div className="space-y-2">
                  <h4 className="text-white font-medium">What is Virtual Filament Diameter?</h4>
                  <p>
                    Pellet extruders don't use standard 1.75 mm filament. To control extrusion volume,
                    HybridSlicer uses a <span className="text-amber-300 font-medium">virtual filament diameter</span> — a fake
                    diameter value that tells the firmware how much material to push per mm of E-axis movement.
                  </p>
                  <p>
                    Think of it as a <span className="text-white">calibration knob</span> for your pellet extruder's volumetric output.
                    A smaller virtual diameter → more E steps per mm → more material extruded. A larger virtual diameter → fewer steps → less material.
                  </p>
                </div>

                <div className="space-y-2">
                  <h4 className="text-white font-medium">The Calibration Process (4 Steps)</h4>
                  <ol className="space-y-2 list-none">
                    {[
                      { num: '1', color: 'amber', title: 'Flow Line Test', desc: 'Print lines at different flow percentages. Measure each line\'s width with calipers. Find which flow % produces lines closest to your nozzle diameter.' },
                      { num: '2', color: 'orange', title: 'Adjust Virtual Diameter', desc: 'If ALL lines are systematically too wide or too narrow, adjust the virtual diameter in Print Settings and re-run the line test. Use the Result Advisor below to get a suggested value.' },
                      { num: '3', color: 'blue', title: 'Flow Tower', desc: 'Print a tower with different flow sections to confirm consistency over height. Look for even walls with no banding.' },
                      { num: '4', color: 'green', title: 'Cube Validation', desc: 'Final check: print a 20×20×10 mm cube at your dialed-in settings. Measure wall thickness — it should match your nozzle diameter.' },
                    ].map(step => (
                      <li key={step.num} className="flex gap-3">
                        <span className={`shrink-0 w-6 h-6 rounded-full flex items-center justify-center text-xs font-bold mt-0.5 ${
                          step.color === 'amber' ? 'bg-amber-900/50 text-amber-300 border border-amber-700'
                          : step.color === 'orange' ? 'bg-orange-900/50 text-orange-300 border border-orange-700'
                          : step.color === 'blue' ? 'bg-blue-900/50 text-blue-300 border border-blue-700'
                          : 'bg-green-900/50 text-green-300 border border-green-700'
                        }`}>{step.num}</span>
                        <div>
                          <span className="text-white font-medium">{step.title} — </span>
                          <span>{step.desc}</span>
                        </div>
                      </li>
                    ))}
                  </ol>
                </div>

                <div className="bg-blue-950/30 border border-blue-800/40 rounded-lg p-3 text-blue-300/80 text-xs">
                  <span className="font-medium text-blue-300">Tip: </span>
                  Always start with the Flow Line Test. You typically only need the Flow Tower and Cube after you've found a stable flow % from the line test.
                </div>
              </div>
            )}

            {activeSection === 'formula' && (
              <div className="p-5 space-y-4 text-sm text-gray-400">
                <h4 className="text-white font-medium">Extrusion Length Formula</h4>
                <p>Every E move in the calibration G-code is calculated as:</p>

                <div className="bg-gray-950 border border-gray-700 rounded-lg p-4 font-mono text-sm text-center space-y-1">
                  <div className="text-gray-300">
                    E = <span className="text-amber-300">(nozzle × layerH × distance)</span>
                    {' / '}
                    <span className="text-blue-300">(π/4 × virtualDiam²)</span>
                    {' × '}
                    <span className="text-green-300">flow%</span>
                  </div>
                  {profile && (
                    <div className="text-xs text-gray-600 pt-2 border-t border-gray-800 mt-2">
                      Example for 100 mm line at {profile.materialFlowPct}%:{' '}
                      <span className="text-gray-400">
                        E = ({nozzle.toFixed(2)} × {profile.layerHeightMm} × 100) / (π/4 × {vDiam.toFixed(2)}²) × {profile.materialFlowPct / 100}{' '}
                        = <span className="text-white">{calcE(nozzle, profile.layerHeightMm, 100, vDiam, profile.materialFlowPct).toFixed(4)} mm</span>
                      </span>
                    </div>
                  )}
                </div>

                <div className="space-y-2">
                  <h4 className="text-white font-medium">What each variable does</h4>
                  <table className="w-full text-xs border-collapse">
                    <thead>
                      <tr className="border-b border-gray-800">
                        <th className="text-left py-1.5 pr-4 text-gray-500 font-medium">Variable</th>
                        <th className="text-left py-1.5 pr-4 text-gray-500 font-medium">Source</th>
                        <th className="text-left py-1.5 text-gray-500 font-medium">Effect when increased</th>
                      </tr>
                    </thead>
                    <tbody className="divide-y divide-gray-800/50">
                      {[
                        ['nozzle', 'Profile → Nozzle Diameter', 'More material per mm'],
                        ['layerH', 'Profile → Layer Height', 'More material per mm'],
                        ['virtualDiam', 'Profile → Virtual Filament Diameter', 'Less material per mm (inverse square)'],
                        ['flow%', 'Base Flow + calibration sweep', 'Proportionally more material'],
                      ].map(([v, src, eff]) => (
                        <tr key={v}>
                          <td className="py-1.5 pr-4 font-mono text-amber-300">{v}</td>
                          <td className="py-1.5 pr-4 text-gray-400">{src}</td>
                          <td className="py-1.5 text-gray-400">{eff}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            )}

            {activeSection === 'tips' && (
              <div className="p-5 space-y-4 text-sm text-gray-400">
                <div className="space-y-3">
                  {[
                    {
                      problem: 'All lines are too wide',
                      cause: 'Virtual diameter too small → too many E steps',
                      fix: 'Increase virtual diameter. Use the Result Advisor in Step 1 to get a calculated suggestion.',
                    },
                    {
                      problem: 'All lines are too narrow / gappy',
                      cause: 'Virtual diameter too large → too few E steps',
                      fix: 'Decrease virtual diameter. Use the Result Advisor in Step 1.',
                    },
                    {
                      problem: 'Lines vary a lot in width',
                      cause: 'Feed inconsistency (pellet bridging, moisture, temperature)',
                      fix: 'Check pellet feed path, dry your pellets, and check extruder temperature is stable before measuring.',
                    },
                    {
                      problem: 'Tower sections look good but overall height is wrong',
                      cause: 'Layer height slightly off, or Z-steps not calibrated',
                      fix: 'Verify Z-axis steps per mm in firmware. Layer height mismatch doesn\'t affect flow calibration.',
                    },
                    {
                      problem: 'Cube walls are wavy or uneven',
                      cause: 'Print speed too high for pellet melt rate',
                      fix: 'Reduce print speed in your profile, or increase temperature slightly.',
                    },
                  ].map(item => (
                    <div key={item.problem} className="border border-gray-800 rounded-lg p-3 space-y-1">
                      <p className="text-white font-medium text-xs">{item.problem}</p>
                      <p className="text-[11px]"><span className="text-gray-500">Cause: </span>{item.cause}</p>
                      <p className="text-[11px]"><span className="text-green-400">Fix: </span>{item.fix}</p>
                    </div>
                  ))}
                </div>
                <div className="bg-amber-950/30 border border-amber-800/40 rounded-lg p-3 text-amber-300/80 text-xs">
                  <span className="font-medium text-amber-300">Measurement tip: </span>
                  Use digital calipers and measure each line at 3 points along its length. Average the readings. Lines should be measured after the print has cooled completely.
                </div>
              </div>
            )}
          </div>
        )}
      </div>

      {/* ── Machine Selector (for coordinate offset) ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-3">
        <h3 className="font-medium text-white">Machine (for coordinate offset)</h3>
        <select className="input w-full" value={machineId} onChange={e => setMachineId(e.target.value)}>
          <option value="">None (no offset)</option>
          {machines.map(m => (
            <option key={m.id} value={m.id}>{m.name} — bed at ({m.bedPositionXMm}, {m.bedPositionYMm})</option>
          ))}
        </select>
        {machine && (
          <p className="text-xs text-gray-500">
            All G-code coordinates will be offset by X+{bedOffsetX} Y+{bedOffsetY} mm
          </p>
        )}
      </div>

      {/* ── Profile Selector ── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-3">
        <div className="flex items-center justify-between">
          <h3 className="font-medium text-white">Select Pellet Profile</h3>
          <Link to="/print-settings" className="text-xs text-gray-500 hover:text-gray-300 transition">
            Manage profiles →
          </Link>
        </div>

        {isLoading ? (
          <p className="text-sm text-gray-500">Loading profiles…</p>
        ) : pelletProfiles.length === 0 ? (
          <div className="text-sm text-amber-400 bg-amber-950/30 border border-amber-800/40 rounded-lg p-3 space-y-1">
            <p className="font-medium">No profiles with Pellet Mode enabled.</p>
            <p className="text-amber-500">
              Go to{' '}
              <Link to="/print-settings" className="underline text-amber-300 hover:text-amber-100">
                Print Settings
              </Link>{' '}
              and enable Pellet Mode on a profile first, then set a Virtual Filament Diameter.
            </p>
          </div>
        ) : (
          <div className="grid grid-cols-2 gap-2">
            {pelletProfiles.map(p => (
              <button
                key={p.id}
                onClick={() => { setSelectedId(p.id); setMeasuredWidth('') }}
                className={`text-left p-3 rounded-lg border text-sm transition ${
                  selectedId === p.id
                    ? 'bg-amber-900/30 border-amber-700 text-amber-200'
                    : 'bg-gray-800 border-gray-700 text-gray-300 hover:border-gray-600'
                }`}
              >
                <p className="font-medium">{p.name}</p>
                <p className="text-[11px] text-gray-500 mt-0.5">
                  vDiam: {p.virtualFilamentDiameterMm} mm
                  {' · '}
                  nozzle: {p.nozzleDiameterMm > 0 ? `${p.nozzleDiameterMm} mm` : 'from machine'}
                  {' · '}
                  LH: {p.layerHeightMm} mm
                </p>
              </button>
            ))}
          </div>
        )}
      </div>

      {pelletProfiles.length > 0 && !profile && (
        <p className="text-sm text-gray-600 text-center py-2">
          Select a profile above to begin calibration.
        </p>
      )}

      {profile && (
        <>
          {/* ── Active Profile Summary ── */}
          <div className="bg-blue-950/20 border border-blue-800/40 rounded-xl p-4">
            <p className="text-xs font-medium text-blue-400 mb-2.5">Active Profile — {profile.name}</p>
            <div className="grid grid-cols-2 gap-x-8 gap-y-1.5 text-xs font-mono">
              <SummaryRow label="Virtual diam"  value={`${profile.virtualFilamentDiameterMm} mm`} highlight />
              <SummaryRow label="Nozzle"        value={`${effectiveNozzle(profile)} mm`} highlight />
              <SummaryRow label="Layer height"  value={`${profile.layerHeightMm} mm`} />
              <SummaryRow label="Base flow"     value={`${profile.materialFlowPct} %`} />
              <SummaryRow label="Print speed"   value={`${profile.printSpeedMmS} mm/s`} />
              <SummaryRow label="Temperature"   value={`${profile.printTemperatureDegC} °C`} />
            </div>
            <Link to="/print-settings" className="inline-block mt-2.5 text-[11px] text-blue-400 hover:text-blue-200 transition">
              Edit this profile →
            </Link>
          </div>

          {/* ── Step 1: Flow Line Test ── */}
          <StepCard step={1} title="Flow Line Test" color="amber">
            <div className="space-y-4">
              <div className="text-sm text-gray-400 space-y-1.5">
                <p>
                  Prints <span className="text-white">{lineSteps.length} parallel 100 mm lines</span>, each at a different flow %.
                  Lines start at Y=20 and are spaced 8 mm apart. A prime line is printed first.
                </p>
                <div className="bg-amber-950/20 border border-amber-800/30 rounded-lg p-3 text-xs space-y-1">
                  <p className="text-amber-300 font-medium">What to measure:</p>
                  <p>After printing, use digital calipers to measure the width of each line at the midpoint.
                    Write down the flow % next to each measurement.
                    The line closest to <span className="text-white font-mono">{effectiveNozzle(profile).toFixed(2)} mm</span> (your nozzle diameter) is your target flow %.
                  </p>
                </div>
              </div>

              <div>
                <p className="text-xs text-gray-500 mb-2">Flow range to sweep</p>
                <div className="grid grid-cols-3 gap-3">
                  <NumField label="Min flow (%)" value={lineFlow.min}  min={40}  max={200} onChange={v => setLineFlow(f => ({ ...f, min: v }))} />
                  <NumField label="Max flow (%)" value={lineFlow.max}  min={40}  max={200} onChange={v => setLineFlow(f => ({ ...f, max: v }))} />
                  <NumField label="Step (%)"     value={lineFlow.step} min={1}   max={20}  onChange={v => setLineFlow(f => ({ ...f, step: v }))} />
                </div>
                <div className="flex items-center justify-between mt-2">
                  <p className="text-xs text-gray-600">
                    {lineSteps.length} lines at: {lineSteps.join(', ')} %
                  </p>
                  <p className="text-xs text-gray-700">
                    bed area: ~{((lineSteps.length - 1) * 8 + 20 + 100).toFixed(0)} × 130 mm
                  </p>
                </div>
              </div>

              <button onClick={() => generate('line')}
                className="px-4 py-2.5 bg-amber-700 hover:bg-amber-600 text-white rounded-lg text-sm font-medium transition w-full">
                Download Flow Line Test G-code
              </button>

              {/* Result Advisor */}
              <div className="border-t border-gray-800 pt-4 space-y-3">
                <p className="text-xs font-medium text-gray-400">Result Advisor</p>
                <p className="text-xs text-gray-600">
                  After printing, enter the width of the best-looking line (the one closest to {effectiveNozzle(profile).toFixed(2)} mm).
                  If all lines are systematically too wide or narrow, the advisor will suggest a new virtual diameter.
                </p>
                <div className="flex gap-3 items-end">
                  <div className="flex-1">
                    <label className="text-xs text-gray-500 block mb-1">Measured line width (mm)</label>
                    <input
                      type="number" min={0.1} max={5} step={0.01}
                      value={measuredWidth}
                      onChange={e => setMeasuredWidth(e.target.value)}
                      placeholder={`target: ${effectiveNozzle(profile).toFixed(2)}`}
                      className="input w-full text-sm"
                    />
                  </div>
                  <div className="text-xs text-gray-600 pb-2">
                    target: <span className="font-mono text-gray-400">{effectiveNozzle(profile).toFixed(2)} mm</span>
                  </div>
                </div>

                {advice && (
                  <div className={`rounded-lg p-3 text-xs space-y-2 ${
                    advice.verdict === 'good'
                      ? 'bg-green-950/30 border border-green-800/40 text-green-300'
                      : advice.verdict === 'over'
                      ? 'bg-red-950/30 border border-red-800/40 text-red-300'
                      : 'bg-orange-950/30 border border-orange-800/40 text-orange-300'
                  }`}>
                    <p className="font-medium">
                      {advice.verdict === 'good' ? 'On target' : advice.verdict === 'over' ? 'Over-extruding' : 'Under-extruding'}
                    </p>
                    <p>{advice.msg}</p>
                    {advice.suggestedVDiam != null && (
                      <p>
                        Suggested virtual diameter:{' '}
                        <span className="font-mono text-white font-bold">{advice.suggestedVDiam} mm</span>
                        {' '}(current: {profile.virtualFilamentDiameterMm} mm).{' '}
                        <Link to="/print-settings" className="underline hover:opacity-80">
                          Update in Print Settings →
                        </Link>
                      </p>
                    )}
                  </div>
                )}
              </div>
            </div>
          </StepCard>

          {/* ── Step 2: Adjust Virtual Diameter ── */}
          <StepCard step={2} title="Adjust Virtual Diameter (if needed)" color="orange">
            <div className="text-sm text-gray-400 space-y-3">
              <p>
                If the Result Advisor above suggested a new virtual diameter, update it in your profile before continuing.
                Then re-run the Flow Line Test until lines are consistently close to {effectiveNozzle(profile).toFixed(2)} mm.
              </p>

              <div className="grid grid-cols-2 gap-3 text-xs">
                <div className="bg-red-950/20 border border-red-800/30 rounded-lg p-3 space-y-1">
                  <p className="text-red-300 font-medium">Lines too wide?</p>
                  <p className="text-gray-500">Virtual diameter is too small. Increase it — this reduces E output per move.</p>
                </div>
                <div className="bg-orange-950/20 border border-orange-800/30 rounded-lg p-3 space-y-1">
                  <p className="text-orange-300 font-medium">Lines too narrow?</p>
                  <p className="text-gray-500">Virtual diameter is too large. Decrease it — this increases E output per move.</p>
                </div>
              </div>

              <div className="bg-gray-800/50 border border-gray-700 rounded-lg p-3 flex items-center justify-between">
                <div className="text-xs">
                  <p className="text-gray-500">Current virtual diameter</p>
                  <p className="text-white font-mono text-base mt-0.5">{profile.virtualFilamentDiameterMm} mm</p>
                </div>
                <Link to="/print-settings"
                  className="px-3 py-1.5 bg-gray-700 hover:bg-gray-600 text-gray-200 rounded-lg text-xs transition">
                  Edit in Print Settings →
                </Link>
              </div>

              <p className="text-xs text-gray-600">
                After updating, come back, select the updated profile, and re-run the Flow Line Test.
                Repeat until the best line width is within ~0.05 mm of your nozzle diameter.
              </p>
            </div>
          </StepCard>

          {/* ── Step 3: Flow Tower ── */}
          <StepCard step={3} title="Flow Tower" color="blue">
            <div className="space-y-4">
              <div className="text-sm text-gray-400 space-y-1.5">
                <p>
                  Prints a <span className="text-white">20×20 mm single-wall square tower</span>, centered at (50, 50).
                  Each 5 mm section uses a different flow %, printed from bottom to top.
                </p>
                <div className="bg-blue-950/20 border border-blue-800/30 rounded-lg p-3 text-xs space-y-1">
                  <p className="text-blue-300 font-medium">What to look for:</p>
                  <ul className="space-y-0.5 list-disc list-inside text-gray-400">
                    <li>Even wall thickness across all sections — confirms your virtual diameter is correct</li>
                    <li>No banding, gaps, or bulging between sections</li>
                    <li>The section with the best-looking walls (no over/under) confirms your target flow %</li>
                  </ul>
                </div>
              </div>

              <div>
                <p className="text-xs text-gray-500 mb-2">Flow range to test</p>
                <div className="grid grid-cols-3 gap-3">
                  <NumField label="Min flow (%)" value={towerFlow.min}  min={40}  max={200} onChange={v => setTowerFlow(f => ({ ...f, min: v }))} />
                  <NumField label="Max flow (%)" value={towerFlow.max}  min={40}  max={200} onChange={v => setTowerFlow(f => ({ ...f, max: v }))} />
                  <NumField label="Step (%)"     value={towerFlow.step} min={1}   max={30}  onChange={v => setTowerFlow(f => ({ ...f, step: v }))} />
                </div>
                <div className="flex items-center justify-between mt-2">
                  <p className="text-xs text-gray-600">
                    {towerSteps.length} sections: {towerSteps.join(', ')} %
                  </p>
                  <p className="text-xs text-gray-700">
                    tower height: ~{(towerSteps.length * 5).toFixed(0)} mm
                  </p>
                </div>
              </div>

              <button onClick={() => generate('tower')}
                className="px-4 py-2.5 bg-blue-700 hover:bg-blue-600 text-white rounded-lg text-sm font-medium transition w-full">
                Download Flow Tower G-code
              </button>

              <div className="text-xs text-gray-600 bg-gray-800/40 rounded-lg p-3">
                After printing, set the best-performing flow % as your <span className="text-gray-400">Base Flow</span> in the profile.
                A typical starting range with pellets is 80–120% — use a narrower range once you're close.
              </div>
            </div>
          </StepCard>

          {/* ── Step 4: Cube Validation ── */}
          <StepCard step={4} title="Cube Validation" color="green">
            <div className="space-y-4">
              <div className="text-sm text-gray-400 space-y-1.5">
                <p>
                  Prints a <span className="text-white">20×20×10 mm single-wall cube</span> using your profile's current settings — no flow sweep.
                  This is your final real-world check before slicing actual parts.
                </p>
                <div className="bg-green-950/20 border border-green-800/30 rounded-lg p-3 text-xs space-y-2">
                  <p className="text-green-300 font-medium">Pass criteria:</p>
                  <ul className="space-y-0.5 list-disc list-inside text-gray-400">
                    <li>Wall thickness ≈ <span className="font-mono text-white">{effectiveNozzle(profile).toFixed(2)} mm</span> (your nozzle diameter)</li>
                    <li>No gaps between layers, no overhanging blobs</li>
                    <li>Consistent surface texture all around</li>
                    <li>Corners are sharp, not rounded from over-extrusion</li>
                  </ul>
                </div>
              </div>

              <div className="grid grid-cols-2 gap-x-8 gap-y-1.5 text-xs font-mono">
                <SummaryRow label="Size"       value="20 × 20 × 10 mm" />
                <SummaryRow label="Layers"     value={`${Math.round(10 / profile.layerHeightMm)} @ ${profile.layerHeightMm} mm`} />
                <SummaryRow label="Flow"       value={`${profile.materialFlowPct} %`} />
                <SummaryRow label="Wall target" value={`≈ ${effectiveNozzle(profile).toFixed(2)} mm`} highlight />
              </div>

              <button onClick={() => generate('cube')}
                className="px-4 py-2.5 bg-green-700 hover:bg-green-600 text-white rounded-lg text-sm font-medium transition w-full">
                Download Cube Validation G-code
              </button>

              <div className="text-xs text-gray-600 bg-green-950/10 border border-green-900/30 rounded-lg p-3">
                If the cube passes, your pellet profile is calibrated. You can now slice parts with this profile and
                expect accurate extrusion. Run this test again any time you change material, temperature, or virtual diameter.
              </div>
            </div>
          </StepCard>
        </>
      )}
    </div>
  )
}

// ── Sub-components ─────────────────────────────────────────────────────────────

function StepCard({
  step, title, color, children,
}: {
  step: number; title: string
  color: 'amber' | 'orange' | 'blue' | 'green'
  children: React.ReactNode
}) {
  const styles = {
    amber:  { border: 'border-amber-800/50',  badge: 'bg-amber-900/40 text-amber-300 border-amber-700',  title: 'text-amber-300'  },
    orange: { border: 'border-orange-800/50', badge: 'bg-orange-900/40 text-orange-300 border-orange-700', title: 'text-orange-300' },
    blue:   { border: 'border-blue-800/50',   badge: 'bg-blue-900/40 text-blue-300 border-blue-700',   title: 'text-blue-300'   },
    green:  { border: 'border-green-800/50',  badge: 'bg-green-900/40 text-green-300 border-green-700',  title: 'text-green-300'  },
  }
  const s = styles[color]
  return (
    <div className={`bg-gray-900 border rounded-xl p-5 space-y-4 ${s.border}`}>
      <div className="flex items-center gap-3">
        <span className={`w-7 h-7 rounded-full flex items-center justify-center text-xs font-bold border shrink-0 ${s.badge}`}>
          {step}
        </span>
        <h3 className={`font-medium text-base ${s.title}`}>{title}</h3>
      </div>
      {children}
    </div>
  )
}

function NumField({
  label, value, min, max, onChange,
}: {
  label: string; value: number; min?: number; max?: number
  onChange: (v: number) => void
}) {
  return (
    <div className="space-y-1">
      <label className="text-xs text-gray-500">{label}</label>
      <input type="number" min={min} max={max} value={value}
        onChange={e => onChange(+e.target.value)}
        className="input w-full text-sm" />
    </div>
  )
}

function SummaryRow({ label, value, highlight }: { label: string; value: string; highlight?: boolean }) {
  return (
    <>
      <span className="text-gray-600 text-[11px]">{label}:</span>
      <span className={`text-[11px] font-mono ${highlight ? 'text-white' : 'text-gray-400'}`}>{value}</span>
    </>
  )
}
