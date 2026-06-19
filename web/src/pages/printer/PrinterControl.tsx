import { useState } from 'react'
import { useAxes, useTools, useFans } from '../../store/duetStore'
import { useDuetStore } from '../../store/duetStore'
import * as duetApi from '../../services/duetApi'

const MOVE_STEPS = [0.1, 0.5, 1, 5, 10, 50, 100]
const FEED_RATES: Record<string, number> = { X: 6000, Y: 6000, Z: 600 }

function AxisJogControls() {
  const axes = useAxes()
  const [step, setStep] = useState(10)

  const jog = (axis: string, dir: number) => {
    const feedRate = FEED_RATES[axis] ?? 6000
    duetApi.sendGCode(`M120\nG91\nG1 ${axis}${dir * step} F${feedRate}\nG90\nM121`)
  }

  const homeAxis = (axis: string) => {
    duetApi.sendGCode(`G28 ${axis}`)
  }

  const homeAll = () => duetApi.sendGCode('G28')

  return (
    <div className="space-y-4">
      {/* Step size selector */}
      <div>
        <div className="text-xs text-gray-500 mb-2">Step Size (mm)</div>
        <div className="flex flex-wrap gap-1">
          {MOVE_STEPS.map(s => (
            <button
              key={s}
              onClick={() => setStep(s)}
              className={`px-2.5 py-1 rounded text-xs font-mono transition ${
                step === s ? 'bg-primary/30 text-primary-300 border border-primary/50' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}
            >
              {s}
            </button>
          ))}
        </div>
      </div>

      {/* XY Jog pad */}
      <div>
        <div className="text-xs text-gray-500 mb-2">XY Movement</div>
        <div className="grid grid-cols-3 gap-1.5 w-40 mx-auto">
          <div />
          <button onClick={() => jog('Y', 1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg py-3 text-gray-300 text-sm font-medium">Y+</button>
          <div />
          <button onClick={() => jog('X', -1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg py-3 text-gray-300 text-sm font-medium">X-</button>
          <button onClick={homeAll} className="bg-gray-700 hover:bg-gray-600 rounded-lg py-3 text-yellow-400 text-xs font-bold">HOME</button>
          <button onClick={() => jog('X', 1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg py-3 text-gray-300 text-sm font-medium">X+</button>
          <div />
          <button onClick={() => jog('Y', -1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg py-3 text-gray-300 text-sm font-medium">Y-</button>
          <div />
        </div>
      </div>

      {/* Z Jog */}
      <div>
        <div className="text-xs text-gray-500 mb-2">Z Movement</div>
        <div className="flex gap-2 justify-center">
          <button onClick={() => jog('Z', 1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg px-6 py-3 text-gray-300 text-sm font-medium">Z+</button>
          <button onClick={() => homeAxis('Z')} className="bg-gray-700 hover:bg-gray-600 rounded-lg px-4 py-3 text-yellow-400 text-xs font-bold">Home Z</button>
          <button onClick={() => jog('Z', -1)} className="bg-gray-800 hover:bg-gray-700 rounded-lg px-6 py-3 text-gray-300 text-sm font-medium">Z-</button>
        </div>
      </div>

      {/* Home individual axes */}
      <div>
        <div className="text-xs text-gray-500 mb-2">Home</div>
        <div className="flex gap-2">
          <button onClick={homeAll} className="flex-1 py-1.5 bg-yellow-900/50 hover:bg-yellow-800/50 text-yellow-300 rounded-lg text-xs font-medium">Home All</button>
          {axes.filter(a => a.visible !== false).map(a => (
            <button key={a.letter} onClick={() => homeAxis(a.letter)} className="px-3 py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">
              {a.letter}
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}

function ExtruderControls() {
  const [amount, setAmount] = useState(10)
  const [feedrate, setFeedrate] = useState(5) // mm/s
  const tools = useTools()
  const model = useDuetStore(s => s.model)
  const currentTool = model?.state?.currentTool ?? -1

  const extrude = (dir: number) => {
    duetApi.sendGCode(`M120\nG91\nG1 E${dir * amount} F${feedrate * 60}\nG90\nM121`)
  }

  const selectTool = (t: number) => {
    duetApi.sendGCode(`T${t}`)
  }

  return (
    <div className="space-y-4">
      {/* Tool selection */}
      {tools.length > 0 && (
        <div>
          <div className="text-xs text-gray-500 mb-2">Tool Selection</div>
          <div className="flex gap-2 flex-wrap">
            {tools.map(t => (
              <button
                key={t.number}
                onClick={() => selectTool(t.number)}
                className={`px-3 py-1.5 rounded-lg text-xs font-medium transition ${
                  currentTool === t.number
                    ? 'bg-primary/30 text-primary-300 border border-primary/50'
                    : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                }`}
              >
                T{t.number}{t.name ? `: ${t.name}` : ''}
              </button>
            ))}
            <button
              onClick={() => duetApi.sendGCode('T-1')}
              className={`px-3 py-1.5 rounded-lg text-xs font-medium transition ${
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

      {/* Extrude amount */}
      <div>
        <div className="text-xs text-gray-500 mb-2">Extrude Amount (mm)</div>
        <div className="flex gap-1">
          {[1, 5, 10, 20, 50, 100].map(a => (
            <button
              key={a}
              onClick={() => setAmount(a)}
              className={`px-2.5 py-1 rounded text-xs font-mono transition ${
                amount === a ? 'bg-primary/30 text-primary-300 border border-primary/50' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}
            >
              {a}
            </button>
          ))}
        </div>
      </div>

      {/* Feedrate */}
      <div>
        <div className="text-xs text-gray-500 mb-2">Feedrate (mm/s)</div>
        <div className="flex gap-1">
          {[1, 2, 5, 10, 20].map(f => (
            <button
              key={f}
              onClick={() => setFeedrate(f)}
              className={`px-2.5 py-1 rounded text-xs font-mono transition ${
                feedrate === f ? 'bg-primary/30 text-primary-300 border border-primary/50' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
              }`}
            >
              {f}
            </button>
          ))}
        </div>
      </div>

      {/* Extrude / Retract */}
      <div className="flex gap-2">
        <button onClick={() => extrude(-1)} className="flex-1 py-2 bg-orange-800/60 hover:bg-orange-700/60 text-orange-300 rounded-lg text-sm font-medium">Retract</button>
        <button onClick={() => extrude(1)} className="flex-1 py-2 bg-blue-800/60 hover:bg-blue-700/60 text-blue-300 rounded-lg text-sm font-medium">Extrude</button>
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

  if (!fans.length) return <div className="text-sm text-gray-600">No fans configured</div>

  return (
    <div className="space-y-2">
      {fans.map((fan, i) => (
        <div key={i} className="flex items-center justify-between">
          <span className="text-sm text-gray-400">{fan.name || `Fan ${i}`}</span>
          <div className="flex items-center gap-2">
            <button onClick={() => setFan(i, 0)} className="text-xs px-1.5 py-0.5 bg-gray-800 rounded hover:bg-gray-700 text-gray-400">Off</button>
            {editIdx === i ? (
              <input
                className="input w-14 text-right text-xs py-0.5"
                value={editVal}
                onChange={e => setEditVal(e.target.value)}
                onKeyDown={e => { if (e.key === 'Enter') setFan(i, parseFloat(editVal) || 0); if (e.key === 'Escape') setEditIdx(null) }}
                onBlur={() => setEditIdx(null)}
                autoFocus
              />
            ) : (
              <button onClick={() => { setEditIdx(i); setEditVal(String(Math.round(fan.requestedValue * 100))) }} className="font-mono text-white text-sm w-12 text-right hover:text-primary-300">
                {Math.round(fan.requestedValue * 100)}%
              </button>
            )}
            <button onClick={() => setFan(i, 100)} className="text-xs px-1.5 py-0.5 bg-gray-800 rounded hover:bg-gray-700 text-gray-400">Max</button>
          </div>
        </div>
      ))}
    </div>
  )
}

function MachinePowerControls() {
  const model = useDuetStore(s => s.model)
  const atxPower = model?.state?.atxPower

  return (
    <div className="space-y-3">
      {/* ATX Power */}
      {atxPower !== undefined && atxPower !== null && (
        <div className="flex items-center justify-between">
          <span className="text-sm text-gray-400">ATX Power</span>
          <div className="flex gap-2">
            <button onClick={() => duetApi.sendGCode('M80')} className={`px-3 py-1 rounded text-xs ${atxPower ? 'bg-green-800 text-green-300' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'}`}>On</button>
            <button onClick={() => duetApi.sendGCode('M81')} className={`px-3 py-1 rounded text-xs ${!atxPower ? 'bg-red-800 text-red-300' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'}`}>Off</button>
          </div>
        </div>
      )}

      {/* Emergency Stop */}
      <button
        onClick={() => { if (confirm('EMERGENCY STOP - This will immediately halt the machine!')) duetApi.sendGCode('M112\nM999') }}
        className="w-full py-3 bg-red-800 hover:bg-red-700 text-white rounded-xl text-sm font-bold tracking-wider border-2 border-red-600"
      >
        EMERGENCY STOP
      </button>

      {/* Quick actions */}
      <div className="grid grid-cols-2 gap-2">
        <button onClick={() => duetApi.sendGCode('M84')} className="py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Disable Motors</button>
        <button onClick={() => duetApi.sendGCode('M0 H1')} className="py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Stop Heaters</button>
        <button onClick={() => duetApi.sendGCode('M106 S0')} className="py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Fans Off</button>
        <button onClick={() => duetApi.sendGCode('M400')} className="py-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Wait for Moves</button>
      </div>
    </div>
  )
}

function HeaterControl() {
  const [presets] = useState([
    { label: 'PLA Bed', temp: 60, heater: 0 },
    { label: 'PLA Nozzle', temp: 200, heater: 1 },
    { label: 'PETG Bed', temp: 80, heater: 0 },
    { label: 'PETG Nozzle', temp: 240, heater: 1 },
    { label: 'ABS Bed', temp: 100, heater: 0 },
    { label: 'ABS Nozzle', temp: 250, heater: 1 },
  ])

  return (
    <div className="space-y-3">
      <div className="text-xs text-gray-500 mb-2">Temperature Presets</div>
      <div className="grid grid-cols-3 gap-1.5">
        {presets.map((p, i) => (
          <button
            key={i}
            onClick={() => {
              if (p.heater === 0) duetApi.sendGCode(`M140 S${p.temp}`)
              else duetApi.sendGCode(`M568 P0 S${p.temp}`)
            }}
            className="py-1.5 px-2 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs"
          >
            {p.label} ({p.temp}°C)
          </button>
        ))}
      </div>
      <button
        onClick={() => { duetApi.sendGCode('M140 S-273.15'); duetApi.sendGCode('M568 P0 R-273.15 S-273.15') }}
        className="w-full py-1.5 bg-red-900/40 hover:bg-red-800/40 text-red-300 rounded-lg text-xs"
      >
        All Heaters Off
      </button>
    </div>
  )
}

export default function PrinterControl() {
  return (
    <div className="space-y-4">
      <h2 className="text-xl font-semibold text-white">Machine Control</h2>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Axis Jog Controls */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Axis Movement</h3>
          <AxisJogControls />
        </div>

        {/* Extruder Controls */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Extruder</h3>
          <ExtruderControls />
        </div>

        {/* Fan Controls */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Fans</h3>
          <FanControls />
        </div>

        {/* Heater Presets */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Heater Presets</h3>
          <HeaterControl />
        </div>

        {/* Power & Emergency */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 lg:col-span-2">
          <h3 className="text-sm font-medium text-white mb-3">Machine Power</h3>
          <MachinePowerControls />
        </div>
      </div>
    </div>
  )
}
