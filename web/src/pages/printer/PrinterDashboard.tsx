import { useDuetStore, useAxes, useTools, useJob, useMachineStatus, validHeaters, heaterLabel } from '../../store/duetStore'
import { statusLabel, statusColor, isPrinting, isPaused } from '../../services/duetApi'
import * as duetApi from '../../services/duetApi'
import { useState, useMemo } from 'react'
import { XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend, Area, AreaChart } from 'recharts'

const HEATER_COLORS = ['#ef4444', '#f59e0b', '#10b981', '#3b82f6', '#8b5cf6', '#ec4899', '#06b6d4', '#84cc16']

function TempChart() {
  const tempHistory = useDuetStore(s => s.tempHistory)
  const model = useDuetStore(s => s.model)

  const heaterEntries = useMemo(() => validHeaters(model), [model])
  const heaterLabels = useMemo(() => heaterEntries.map(e => e.label), [heaterEntries])

  if (!tempHistory.length || !heaterLabels.length) {
    return (
      <div className="h-64 flex items-center justify-center text-gray-600 text-sm">
        <div className="text-center">
          <div className="text-3xl mb-2 opacity-50">📊</div>
          <div>Waiting for temperature data...</div>
        </div>
      </div>
    )
  }

  return (
    <ResponsiveContainer width="100%" height={300}>
      <AreaChart data={tempHistory} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
        <defs>
          {heaterLabels.map((label, i) => (
            <linearGradient key={label} id={`grad-${i}`} x1="0" y1="0" x2="0" y2="1">
              <stop offset="5%" stopColor={HEATER_COLORS[i % HEATER_COLORS.length]} stopOpacity={0.3} />
              <stop offset="95%" stopColor={HEATER_COLORS[i % HEATER_COLORS.length]} stopOpacity={0} />
            </linearGradient>
          ))}
        </defs>
        <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
        <XAxis dataKey="label" stroke="#475569" tick={{ fontSize: 10 }} interval="preserveStartEnd" />
        <YAxis stroke="#475569" tick={{ fontSize: 10 }} domain={[0, 'auto']} unit="°C" width={45} />
        <Tooltip
          contentStyle={{ backgroundColor: '#0f172a', border: '1px solid #334155', borderRadius: '12px', boxShadow: '0 8px 24px rgba(0,0,0,0.4)' }}
          labelStyle={{ color: '#94a3b8', fontWeight: 600, marginBottom: 4 }}
          itemStyle={{ fontSize: 12 }}
        />
        <Legend wrapperStyle={{ fontSize: 12, paddingTop: 8 }} />
        {heaterLabels.map((label, i) => (
          <Area
            key={label}
            type="monotone"
            dataKey={`heaters.${label}`}
            name={label}
            stroke={HEATER_COLORS[i % HEATER_COLORS.length]}
            fill={`url(#grad-${i})`}
            strokeWidth={2}
            dot={false}
            isAnimationActive={false}
          />
        ))}
      </AreaChart>
    </ResponsiveContainer>
  )
}

function StatusBadge({ status }: { status: string }) {
  return (
    <span className={`px-4 py-1.5 rounded-full text-xs font-semibold tracking-wide ${statusColor(status)}`}>
      {statusLabel(status)}
    </span>
  )
}

function HeaterCard({ index, color }: { index: number; color: string }) {
  const model = useDuetStore(s => s.model)
  const tools = useTools()
  const heater = model?.heat?.heaters?.[index]
  const [editing, setEditing] = useState(false)
  const [editTemp, setEditTemp] = useState('')

  if (!heater) return null

  const label = heaterLabel(index, model)
  const isBed = (model?.heat?.bedHeaters ?? []).includes(index)
  const isChamber = (model?.heat?.chamberHeaters ?? []).includes(index)
  const current = heater.current ?? 0
  const active = heater.active ?? 0
  const state = heater.state ?? 'off'

  // Progress ring: 0-300°C mapped to 0-100%
  const maxTemp = isBed ? 120 : 300
  const pct = Math.min(100, Math.max(0, (current / maxTemp) * 100))
  const circumference = 2 * Math.PI * 38
  const offset = circumference - (pct / 100) * circumference

  const setTemp = async (temp: number) => {
    if (isBed) {
      await duetApi.sendGCode(`M140 S${temp}`)
    } else if (isChamber) {
      await duetApi.sendGCode(`M141 S${temp}`)
    } else {
      const tool = tools.find(t => t?.heaters?.includes(index))
      if (tool) {
        const hi = tool.heaters.indexOf(index)
        const temps = [...tool.active]
        temps[hi] = temp
        await duetApi.sendGCode(`M568 P${tool.number} S${temps.join(':')}`)
      }
    }
    setEditing(false)
  }

  const turnOff = () => {
    if (isBed) duetApi.sendGCode('M140 S-273.15')
    else if (isChamber) duetApi.sendGCode('M141 S-273.15')
    else {
      const tool = tools.find(t => t?.heaters?.includes(index))
      if (tool) duetApi.sendGCode(`M568 P${tool.number} R-273.15 S-273.15`)
    }
  }

  return (
    <div className="bg-gray-900/80 border border-gray-800 rounded-2xl p-4 flex flex-col items-center relative overflow-hidden group hover:border-gray-700 transition-colors">
      {/* Glow effect when active */}
      {state === 'active' && (
        <div className="absolute inset-0 opacity-10 rounded-2xl" style={{ background: `radial-gradient(circle at center, ${color}, transparent 70%)` }} />
      )}

      {/* Label */}
      <div className="text-xs font-semibold text-gray-400 mb-2 uppercase tracking-wider">{label}</div>

      {/* Circular gauge */}
      <div className="relative w-24 h-24 mb-2">
        <svg className="w-24 h-24 -rotate-90" viewBox="0 0 80 80">
          <circle cx="40" cy="40" r="38" fill="none" stroke="#1e293b" strokeWidth="4" />
          <circle cx="40" cy="40" r="38" fill="none" stroke={color} strokeWidth="4"
            strokeLinecap="round"
            strokeDasharray={circumference}
            strokeDashoffset={offset}
            className="transition-all duration-500"
          />
        </svg>
        <div className="absolute inset-0 flex flex-col items-center justify-center">
          <div className="text-xl font-bold text-white font-mono">{current.toFixed(1)}</div>
          <div className="text-[10px] text-gray-500">°C</div>
        </div>
      </div>

      {/* Target temp */}
      <div className="flex items-center gap-2 mb-2">
        <span className="text-[10px] text-gray-500 uppercase">Target</span>
        {editing ? (
          <input
            className="bg-gray-800 border border-gray-600 rounded px-2 py-0.5 w-16 text-center text-sm text-white font-mono focus:border-primary-400 outline-none"
            value={editTemp}
            onChange={e => setEditTemp(e.target.value)}
            onKeyDown={e => {
              if (e.key === 'Enter') setTemp(parseFloat(editTemp) || 0)
              if (e.key === 'Escape') setEditing(false)
            }}
            onBlur={() => setEditing(false)}
            autoFocus
          />
        ) : (
          <button
            onClick={() => { setEditing(true); setEditTemp(String(active)) }}
            className="text-sm font-mono text-gray-300 hover:text-white transition px-2 py-0.5 rounded hover:bg-gray-800"
          >
            {active.toFixed(0)}°C
          </button>
        )}
      </div>

      {/* State badge + on/off */}
      <div className="flex items-center gap-2">
        <span className={`text-[10px] px-2 py-0.5 rounded-full font-medium ${
          state === 'active' ? 'bg-red-900/60 text-red-300' :
          state === 'standby' ? 'bg-yellow-900/60 text-yellow-300' :
          state === 'fault' ? 'bg-red-600 text-white animate-pulse' :
          'bg-gray-800 text-gray-500'
        }`}>
          {state}
        </span>
        {state === 'off' ? (
          <button onClick={() => setTemp(active)} className="text-[10px] text-green-400 hover:text-green-300 font-medium">ON</button>
        ) : (
          <button onClick={turnOff} className="text-[10px] text-red-400 hover:text-red-300 font-medium">OFF</button>
        )}
      </div>
    </div>
  )
}

function PositionDisplay() {
  const axes = useAxes()
  const visibleAxes = axes.filter(a => a.visible !== false)

  return (
    <div className="grid grid-cols-3 sm:grid-cols-4 lg:grid-cols-6 gap-2">
      {visibleAxes.map(axis => (
        <div key={axis.letter} className="bg-gray-800/60 border border-gray-700/50 rounded-xl p-3 text-center group hover:border-gray-600 transition-colors">
          <div className="text-[10px] font-bold text-gray-500 uppercase tracking-widest mb-1">{axis.letter}</div>
          <div className="text-lg font-mono font-bold text-white tabular-nums">{axis.userPosition.toFixed(2)}</div>
          <div className={`text-[10px] mt-1 font-medium ${axis.homed ? 'text-green-400' : 'text-yellow-400'}`}>
            {axis.homed ? '● Homed' : '○ Not homed'}
          </div>
        </div>
      ))}
    </div>
  )
}

function SpeedFactors() {
  const model = useDuetStore(s => s.model)
  const speedFactorRaw = model?.move?.speedFactor ?? 1.0
  const speedPct = Math.round(speedFactorRaw * 100)
  const extruders = (model?.move?.extruders ?? []).filter(e => e != null)
  const babystep = model?.move?.babystepZ ?? 0

  const FactorRow = ({ label, value, onDec, onInc, unit = '%', decLabel = '-', incLabel = '+' }: {
    label: string; value: string; onDec: () => void; onInc: () => void; unit?: string; decLabel?: string; incLabel?: string
  }) => (
    <div className="flex items-center justify-between py-2">
      <span className="text-sm text-gray-400">{label}</span>
      <div className="flex items-center gap-1.5">
        <button onClick={onDec} className="w-8 h-8 flex items-center justify-center bg-gray-800 hover:bg-gray-700 rounded-lg text-gray-300 text-sm font-bold transition">{decLabel}</button>
        <span className="font-mono text-white text-sm w-16 text-center tabular-nums">{value}{unit}</span>
        <button onClick={onInc} className="w-8 h-8 flex items-center justify-center bg-gray-800 hover:bg-gray-700 rounded-lg text-gray-300 text-sm font-bold transition">{incLabel}</button>
      </div>
    </div>
  )

  return (
    <div className="divide-y divide-gray-800/50">
      <FactorRow
        label="Speed Factor"
        value={String(speedPct)}
        onDec={() => duetApi.sendGCode(`M220 S${Math.max(10, speedPct - 10)}`)}
        onInc={() => duetApi.sendGCode(`M220 S${Math.min(300, speedPct + 10)}`)}
        decLabel="-10" incLabel="+10"
      />
      {extruders.map((ext, i) => {
        const extPct = Math.round(ext.factor * 100)
        return (
          <FactorRow
            key={i}
            label={`E${i} Flow`}
            value={String(extPct)}
            onDec={() => duetApi.sendGCode(`M221 D${i} S${Math.max(50, extPct - 5)}`)}
            onInc={() => duetApi.sendGCode(`M221 D${i} S${Math.min(200, extPct + 5)}`)}
            decLabel="-5" incLabel="+5"
          />
        )
      })}
      <FactorRow
        label="Baby Step Z"
        value={babystep.toFixed(3)}
        unit="mm"
        onDec={() => duetApi.sendGCode('M290 R1 S-0.05')}
        onInc={() => duetApi.sendGCode('M290 R1 S0.05')}
        decLabel="-0.05" incLabel="+0.05"
      />
    </div>
  )
}

function JobProgress() {
  const job = useJob()
  const status = useMachineStatus()

  if (!job?.file?.fileName && !isPrinting(status)) {
    return (
      <div className="text-center text-gray-600 py-6 text-sm">
        <div className="text-2xl mb-2 opacity-40">📄</div>
        No job running
      </div>
    )
  }

  const progress = job?.filePosition && job?.file?.size
    ? Math.min(100, (job.filePosition / job.file.size) * 100)
    : 0

  const formatTime = (secs: number | null) => {
    if (!secs || secs <= 0) return '--:--:--'
    const h = Math.floor(secs / 3600)
    const m = Math.floor((secs % 3600) / 60)
    const s = Math.floor(secs % 60)
    return `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
  }

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between text-sm">
        <span className="text-gray-400 truncate flex-1">{job?.file?.fileName ?? 'Unknown'}</span>
        <span className="text-white font-mono font-bold ml-2">{progress.toFixed(1)}%</span>
      </div>
      {/* Progress bar */}
      <div className="w-full bg-gray-800 rounded-full h-2.5 overflow-hidden">
        <div
          className="h-full rounded-full transition-all duration-500 bg-gradient-to-r from-primary-500 to-primary-400"
          style={{ width: `${progress}%` }}
        />
      </div>
      <div className="grid grid-cols-3 gap-2 text-xs">
        <div className="bg-gray-800/40 rounded-lg p-2 text-center">
          <div className="text-gray-500 text-[10px] uppercase">Layer</div>
          <div className="text-white font-mono font-bold mt-0.5">{job?.layer ?? '-'}</div>
        </div>
        <div className="bg-gray-800/40 rounded-lg p-2 text-center">
          <div className="text-gray-500 text-[10px] uppercase">Elapsed</div>
          <div className="text-white font-mono font-bold mt-0.5">{formatTime(job?.duration ?? null)}</div>
        </div>
        <div className="bg-gray-800/40 rounded-lg p-2 text-center">
          <div className="text-gray-500 text-[10px] uppercase">Remaining</div>
          <div className="text-white font-mono font-bold mt-0.5">{formatTime(job?.timesLeft?.file ?? null)}</div>
        </div>
      </div>
      {isPrinting(status) && (
        <div className="flex gap-2">
          <button onClick={() => duetApi.sendGCode('M25')} className="flex-1 px-3 py-2 bg-yellow-700/60 hover:bg-yellow-600/60 text-yellow-300 rounded-xl text-xs font-semibold transition">Pause</button>
          <button onClick={() => { if (confirm('Cancel the current print?')) duetApi.sendGCode('M0') }} className="flex-1 px-3 py-2 bg-red-700/60 hover:bg-red-600/60 text-red-300 rounded-xl text-xs font-semibold transition">Cancel</button>
        </div>
      )}
      {isPaused(status) && (
        <button onClick={() => duetApi.sendGCode('M24')} className="w-full px-3 py-2 bg-green-700/60 hover:bg-green-600/60 text-green-300 rounded-xl text-xs font-semibold transition">Resume</button>
      )}
    </div>
  )
}

export default function PrinterDashboard() {
  const status = useMachineStatus()
  const model = useDuetStore(s => s.model)
  const heaterEntries = useMemo(() => validHeaters(model), [model])

  return (
    <div className="space-y-4">
      {/* Status bar */}
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-bold text-white">Dashboard</h2>
        <div className="flex items-center gap-3">
          {model?.network?.name && <span className="text-sm text-gray-400">{model.network.name}</span>}
          <StatusBadge status={status} />
        </div>
      </div>

      {/* Temperature Chart - full width */}
      <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
        <h3 className="text-sm font-semibold text-white mb-3 flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-red-400 animate-pulse" />
          Temperature
        </h3>
        <TempChart />
      </div>

      {/* Heater cards - circular gauges */}
      {heaterEntries.length > 0 && (
        <div className={`grid gap-3 ${
          heaterEntries.length <= 2 ? 'grid-cols-2' :
          heaterEntries.length <= 4 ? 'grid-cols-2 lg:grid-cols-4' :
          'grid-cols-2 lg:grid-cols-3 xl:grid-cols-4'
        }`}>
          {heaterEntries.map((entry, i) => (
            <HeaterCard key={entry.index} index={entry.index} color={HEATER_COLORS[i % HEATER_COLORS.length]} />
          ))}
        </div>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Position */}
        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Axis Positions</h3>
          <PositionDisplay />
        </div>

        {/* Job Progress */}
        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4">
          <h3 className="text-sm font-semibold text-white mb-3">Current Job</h3>
          <JobProgress />
        </div>

        {/* Speed & Factors */}
        <div className="bg-gray-900/60 border border-gray-800 rounded-2xl p-4 lg:col-span-2">
          <h3 className="text-sm font-semibold text-white mb-2">Speed & Factors</h3>
          <SpeedFactors />
        </div>
      </div>
    </div>
  )
}
