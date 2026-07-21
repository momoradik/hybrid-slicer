import { useState, useMemo } from 'react'
import { useAxes, useTools, useFans } from '../../store/duetStore'
import { useDuetStore, validHeaters } from '../../store/duetStore'
import * as duetApi from '../../services/duetApi'

const MOVE_STEPS = [0.1, 0.5, 1, 5, 10, 50, 100]

const AXIS_FEEDRATES: Record<string, number> = {
  X: 6000, Y: 6000, Z: 600,
  U: 3000, V: 3000, W: 600,
  A: 3000, B: 3000, C: 600,
}

const AXIS_COLORS: Record<string, string> = {
  X: '#ef4444', Y: '#22c55e', Z: '#3b82f6',
  U: '#f59e0b', V: '#8b5cf6', W: '#06b6d4',
  A: '#ec4899', B: '#84cc16', C: '#f97316',
}

function AxisJogControls() {
  const axes = useAxes()
  const visibleAxes = axes.filter(a => a.visible !== false)
  const [step, setStep] = useState(10)

  const jog = (axis: string, dir: number) => {
    const feedRate = AXIS_FEEDRATES[axis] ?? 3000
    duetApi.sendGCode(`M120\nG91\nG1 ${axis}${dir * step} F${feedRate}\nG90\nM121`)
  }

  const homeAxis = (axis: string) => duetApi.sendGCode(`G28 ${axis}`)
  const homeAll = () => duetApi.sendGCode('G28')

  return (
    <div className="space-y-4">
      {/* Step size selector */}
      <div>
        <div className="text-[10px] text-gray-500 mb-2 uppercase tracking-wider font-semibold">Step Size (mm)</div>
        <div className="flex flex-wrap gap-1">
          {MOVE_STEPS.map(s => (
            <button
              key={s}
              onClick={() => setStep(s)}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono font-bold transition ${
                step === s
                  ? 'bg-primary-500/20 text-primary-300 border border-primary-500/40 shadow-sm shadow-primary-500/10'
                  : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700 hover:text-gray-200'
              }`}
            >
              {s}
            </button>
          ))}
        </div>
      </div>

      {/* Dynamic axis jog controls - one row per axis */}
      <div>
        <div className="text-[10px] text-gray-500 mb-2 uppercase tracking-wider font-semibold">Axis Movement</div>
        <div className="space-y-2">
          {visibleAxes.map(axis => {
            const color = AXIS_COLORS[axis.letter] ?? '#94a3b8'
            return (
              <div key={axis.letter} className="flex items-center gap-2 bg-gray-800/40 rounded-xl p-2 border border-gray-700/30 hover:border-gray-600/50 transition">
                {/* Axis label */}
                <div className="w-10 text-center">
                  <div className="text-sm font-bold" style={{ color }}>{axis.letter}</div>
                  <div className={`text-[8px] ${axis.homed ? 'text-green-400' : 'text-yellow-400'}`}>
                    {axis.homed ? 'OK' : '!'}
                  </div>
                </div>

                {/* Negative jog buttons (decreasing step sizes) */}
                <button onClick={() => jog(axis.letter, -10)} className="h-9 px-2 bg-gray-700/60 hover:bg-gray-600 rounded-lg text-gray-300 text-[10px] font-mono font-bold transition hidden sm:block">-{step * 10}</button>
                <button onClick={() => jog(axis.letter, -1)} className="h-9 px-3 bg-gray-700 hover:bg-gray-600 rounded-lg text-white text-xs font-mono font-bold transition">
                  <span style={{ color }}>-</span>{step}
                </button>

                {/* Position display */}
                <div className="flex-1 text-center">
                  <div className="text-sm font-mono font-bold text-white tabular-nums">{axis.userPosition.toFixed(2)}</div>
                </div>

                {/* Positive jog buttons */}
                <button onClick={() => jog(axis.letter, 1)} className="h-9 px-3 bg-gray-700 hover:bg-gray-600 rounded-lg text-white text-xs font-mono font-bold transition">
                  <span style={{ color }}>+</span>{step}
                </button>
                <button onClick={() => jog(axis.letter, 10)} className="h-9 px-2 bg-gray-700/60 hover:bg-gray-600 rounded-lg text-gray-300 text-[10px] font-mono font-bold transition hidden sm:block">+{step * 10}</button>

                {/* Home button */}
                <button onClick={() => homeAxis(axis.letter)} className="h-9 px-2.5 bg-yellow-900/40 hover:bg-yellow-800/50 text-yellow-300 rounded-lg text-[10px] font-bold transition border border-yellow-700/30">
                  H
                </button>
              </div>
            )
          })}
        </div>
      </div>

      {/* Home all */}
      <button
        onClick={homeAll}
        className="w-full py-2.5 bg-yellow-900/30 hover:bg-yellow-800/40 text-yellow-300 rounded-xl text-sm font-semibold border border-yellow-700/30 transition"
      >
        Home All Axes
      </button>
    </div>
  )
}

function ExtruderControls() {
  const [amount, setAmount] = useState(10)
  const [feedrate, setFeedrate] = useState(5)
  const tools = useTools()
  const model = useDuetStore(s => s.model)
  const currentTool = model?.state?.currentTool ?? -1

  const extrude = (dir: number) => {
    duetApi.sendGCode(`M120\nG91\nG1 E${dir * amount} F${feedrate * 60}\nG90\nM121`)
  }

  const selectTool = (t: number) => duetApi.sendGCode(`T${t}`)

  return (
    <div className="space-y-4">
      {tools.length > 0 && (
        <div>
          <div className="text-[10px] text-gray-500 mb-2 uppercase tracking-wider font-semibold">Tool Selection</div>
          <div className="flex gap-2 flex-wrap">
            {tools.map(t => (
              <button
                key={t.number}
                onClick={() => selectTool(t.number)}
                className={`px-3 py-2 rounded-xl text-xs font-semibold transition ${
                  currentTool === t.number
                    ? 'bg-primary-500/20 text-primary-300 border border-primary-500/40'
                    : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                }`}
              >
                T{t.number}{t.name ? `: ${t.name}` : ''}
              </button>
            ))}
            <button
              onClick={() => duetApi.sendGCode('T-1')}
              className={`px-3 py-2 rounded-xl text-xs font-semibold transition ${
                currentTool === -1
                  ? 'bg-gray-600 text-white border border-gray-500'
                  : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}
            >
              Deselect
            </button>
          </div>
        </div>
      )}

      <div>
        <div className="text-[10px] text-gray-500 mb-2 uppercase tracking-wider font-semibold">Amount (mm)</div>
        <div className="flex gap-1">
          {[1, 5, 10, 20, 50, 100].map(a => (
            <button key={a} onClick={() => setAmount(a)}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono font-bold transition ${
                amount === a ? 'bg-primary-500/20 text-primary-300 border border-primary-500/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}>{a}</button>
          ))}
        </div>
      </div>

      <div>
        <div className="text-[10px] text-gray-500 mb-2 uppercase tracking-wider font-semibold">Feedrate (mm/s)</div>
        <div className="flex gap-1">
          {[1, 2, 5, 10, 20].map(f => (
            <button key={f} onClick={() => setFeedrate(f)}
              className={`px-3 py-1.5 rounded-lg text-xs font-mono font-bold transition ${
                feedrate === f ? 'bg-primary-500/20 text-primary-300 border border-primary-500/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}>{f}</button>
          ))}
        </div>
      </div>

      <div className="flex gap-2">
        <button onClick={() => extrude(-1)} className="flex-1 py-2.5 bg-orange-800/40 hover:bg-orange-700/50 text-orange-300 rounded-xl text-sm font-semibold border border-orange-700/30 transition">Retract</button>
        <button onClick={() => extrude(1)} className="flex-1 py-2.5 bg-blue-800/40 hover:bg-blue-700/50 text-blue-300 rounded-xl text-sm font-semibold border border-blue-700/30 transition">Extrude</button>
      </div>
    </div>
  )
}

function FanControls() {
  const fans = useFans()
  const [editIdx, setEditIdx] = useState<number | null>(null)
  const [editVal, setEditVal] = useState('')

  const setFan = (idx: number, pct: number) => {
    const val = Math.max(0, Math.min(100, pct)) / 100
    duetApi.sendGCode(`M106 P${idx} S${val.toFixed(2)}`)
    setEditIdx(null)
  }

  if (!fans.length) return <div className="text-sm text-gray-600 py-4 text-center">No fans configured</div>

  return (
    <div className="space-y-2">
      {fans.map((fan, i) => {
        const pct = Math.round(fan.requestedValue * 100)
        return (
          <div key={i} className="flex items-center justify-between bg-gray-800/30 rounded-xl p-2.5 border border-gray-700/20">
            <span className="text-sm text-gray-300 font-medium">{fan.name || `Fan ${i}`}</span>
            <div className="flex items-center gap-2">
              <button onClick={() => setFan(i, 0)} className="text-[10px] px-2 py-1 bg-gray-700/60 rounded-lg hover:bg-gray-600 text-gray-400 font-semibold transition">OFF</button>
              {/* Progress bar */}
              <div className="w-20 h-2 bg-gray-700 rounded-full overflow-hidden">
                <div className="h-full bg-cyan-400 transition-all rounded-full" style={{ width: `${pct}%` }} />
              </div>
              {editIdx === i ? (
                <input className="bg-gray-800 border border-gray-600 rounded-lg w-14 text-right text-xs py-1 px-2 text-white font-mono focus:border-primary-400 outline-none"
                  value={editVal}
                  onChange={e => setEditVal(e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') setFan(i, parseFloat(editVal) || 0); if (e.key === 'Escape') setEditIdx(null) }}
                  onBlur={() => setEditIdx(null)} autoFocus />
              ) : (
                <button onClick={() => { setEditIdx(i); setEditVal(String(pct)) }} className="font-mono text-white text-sm w-12 text-right hover:text-primary-300 transition">{pct}%</button>
              )}
              <button onClick={() => setFan(i, 100)} className="text-[10px] px-2 py-1 bg-gray-700/60 rounded-lg hover:bg-gray-600 text-gray-400 font-semibold transition">MAX</button>
            </div>
          </div>
        )
      })}
    </div>
  )
}

function HeaterPresets() {
  const model = useDuetStore(s => s.model)
  const tools = useTools()
  const heaterEntries = useMemo(() => validHeaters(model), [model])

  const presets = [
    { label: 'PLA', bed: 60, nozzle: 200 },
    { label: 'PETG', bed: 80, nozzle: 240 },
    { label: 'ABS', bed: 100, nozzle: 250 },
    { label: 'TPU', bed: 50, nozzle: 220 },
  ]

  const bedIdx = (model?.heat?.bedHeaters ?? [])[0]
  const nozzleIdx = heaterEntries.find(e => e.index !== bedIdx)?.index

  const applyPreset = (bed: number, nozzle: number) => {
    if (bedIdx !== undefined) duetApi.sendGCode(`M140 S${bed}`)
    if (nozzleIdx !== undefined) {
      const tool = tools.find(t => t?.heaters?.includes(nozzleIdx))
      if (tool) duetApi.sendGCode(`M568 P${tool.number} S${nozzle}`)
    }
  }

  return (
    <div className="space-y-3">
      <div className="text-[10px] text-gray-500 mb-1 uppercase tracking-wider font-semibold">Quick Presets</div>
      <div className="grid grid-cols-2 gap-2">
        {presets.map(p => (
          <button key={p.label} onClick={() => applyPreset(p.bed, p.nozzle)}
            className="py-2.5 px-3 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-xl text-xs font-medium border border-gray-700/30 hover:border-gray-600/50 transition text-left">
            <div className="font-semibold text-white">{p.label}</div>
            <div className="text-[10px] text-gray-500 mt-0.5">Bed {p.bed}° / Nozzle {p.nozzle}°</div>
          </button>
        ))}
      </div>
      <button
        onClick={() => {
          if (bedIdx !== undefined) duetApi.sendGCode('M140 S-273.15')
          tools.forEach(t => duetApi.sendGCode(`M568 P${t.number} R-273.15 S-273.15`))
        }}
        className="w-full py-2 bg-red-900/30 hover:bg-red-800/40 text-red-300 rounded-xl text-xs font-semibold border border-red-700/30 transition"
      >
        All Heaters Off
      </button>
    </div>
  )
}

function MachinePowerControls() {
  const model = useDuetStore(s => s.model)
  const atxPower = model?.state?.atxPower

  return (
    <div className="space-y-3">
      {atxPower !== undefined && atxPower !== null && (
        <div className="flex items-center justify-between">
          <span className="text-sm text-gray-400">ATX Power</span>
          <div className="flex gap-2">
            <button onClick={() => duetApi.sendGCode('M80')} className={`px-4 py-1.5 rounded-lg text-xs font-semibold transition ${atxPower ? 'bg-green-800/60 text-green-300 border border-green-700/40' : 'bg-gray-800 text-gray-400 hover:bg-gray-700 border border-gray-700'}`}>On</button>
            <button onClick={() => duetApi.sendGCode('M81')} className={`px-4 py-1.5 rounded-lg text-xs font-semibold transition ${!atxPower ? 'bg-red-800/60 text-red-300 border border-red-700/40' : 'bg-gray-800 text-gray-400 hover:bg-gray-700 border border-gray-700'}`}>Off</button>
          </div>
        </div>
      )}

      <button
        onClick={() => { if (confirm('EMERGENCY STOP - This will immediately halt the machine!')) duetApi.sendGCode('M112\nM999') }}
        className="w-full py-4 bg-red-700 hover:bg-red-600 text-white rounded-2xl text-sm font-black tracking-widest uppercase border-2 border-red-500 transition shadow-lg shadow-red-900/30"
      >
        EMERGENCY STOP
      </button>

      <div className="grid grid-cols-2 gap-2">
        <button onClick={() => duetApi.sendGCode('M84')} className="py-2.5 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-xl text-xs font-medium border border-gray-700/30 transition">Disable Motors</button>
        <button onClick={() => duetApi.sendGCode('M0 H1')} className="py-2.5 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-xl text-xs font-medium border border-gray-700/30 transition">Stop Heaters</button>
        <button onClick={() => duetApi.sendGCode('M106 S0')} className="py-2.5 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-xl text-xs font-medium border border-gray-700/30 transition">Fans Off</button>
        <button onClick={() => duetApi.sendGCode('M400')} className="py-2.5 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-xl text-xs font-medium border border-gray-700/30 transition">Wait for Moves</button>
      </div>
    </div>
  )
}

export default function PrinterControl() {
  return (
    <div className="space-y-4">
      <h2 className="text-xl font-bold text-white">Machine Control</h2>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Axis Jog Controls */}
        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4 lg:col-span-2">
          <h3 className="text-sm font-semibold text-white mb-3 flex items-center gap-2">
            <span className="w-2 h-2 rounded-full bg-blue-400" />
            Axis Movement
          </h3>
          <AxisJogControls />
        </div>

        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Extruder</h3>
          <ExtruderControls />
        </div>

        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Fans</h3>
          <FanControls />
        </div>

        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Heater Presets</h3>
          <HeaterPresets />
        </div>

        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Machine Power</h3>
          <MachinePowerControls />
        </div>
      </div>
    </div>
  )
}
