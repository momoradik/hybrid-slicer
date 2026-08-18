import { useState, useMemo, useRef, useEffect, useCallback } from 'react'
import { useAxes, useTools, useFans, useMachineStatus } from '../../store/duetStore'
import { useDuetStore, validHeaters, heaterLabel } from '../../store/duetStore'
import { statusLabel, statusColor } from '../../services/duetApi'
import * as duetApi from '../../services/duetApi'
import { Area, AreaChart, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer } from 'recharts'

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

// ── Inline Console ────────────────────────────────────────────────────────────

function InlineConsole() {
  const { consoleLines, pushConsoleLine, commandHistory, pushCommand } = useDuetStore()
  const [cmd, setCmd] = useState('')
  const [historyIdx, setHistoryIdx] = useState(-1)
  const logRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight
  }, [consoleLines])

  const sendCommand = async () => {
    const trimmed = cmd.trim()
    if (!trimmed) return
    pushCommand(trimmed)
    pushConsoleLine(`> ${trimmed}`)
    setCmd('')
    setHistoryIdx(-1)
    try {
      const reply = await duetApi.sendGCodeWithReply(trimmed)
      if (reply.trim()) pushConsoleLine(reply.trim())
    } catch (err: unknown) {
      pushConsoleLine(`[ERROR] ${err instanceof Error ? err.message : String(err)}`)
    }
  }

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Enter') sendCommand()
    else if (e.key === 'ArrowUp') {
      e.preventDefault()
      if (commandHistory.length === 0) return
      const newIdx = historyIdx < commandHistory.length - 1 ? historyIdx + 1 : historyIdx
      setHistoryIdx(newIdx)
      setCmd(commandHistory[commandHistory.length - 1 - newIdx] ?? '')
    } else if (e.key === 'ArrowDown') {
      e.preventDefault()
      if (historyIdx <= 0) { setHistoryIdx(-1); setCmd('') }
      else {
        const newIdx = historyIdx - 1
        setHistoryIdx(newIdx)
        setCmd(commandHistory[commandHistory.length - 1 - newIdx] ?? '')
      }
    }
  }

  const recentLines = consoleLines.slice(-50)

  return (
    <div className="flex flex-col h-full">
      <div ref={logRef}
        className="flex-1 min-h-0 bg-gray-950 rounded-t-lg border border-gray-800 border-b-0 p-2 overflow-y-auto font-mono text-[11px] leading-relaxed">
        {recentLines.length === 0 ? (
          <div className="text-gray-700 text-center py-4 text-xs">Type a G-code command below...</div>
        ) : recentLines.map((line, i) => (
          <div key={i} className={
            line.startsWith('>') ? 'text-primary/70'
            : line.startsWith('[ERROR]') ? 'text-red-400'
            : 'text-green-400/80'
          }>{line}</div>
        ))}
      </div>
      <div className="flex">
        <input ref={inputRef}
          className="flex-1 bg-gray-950 border border-gray-800 text-gray-100 text-xs font-mono px-2 py-1.5 rounded-bl-lg focus:outline-none focus:border-primary/50"
          value={cmd} onChange={e => setCmd(e.target.value)} onKeyDown={handleKeyDown}
          placeholder="G-code..." autoComplete="off" spellCheck={false} />
        <button onClick={sendCommand}
          className="px-3 py-1.5 bg-primary/80 hover:bg-primary text-white text-xs font-medium rounded-br-lg transition">
          Send
        </button>
      </div>
    </div>
  )
}

// ── Compact Heater Card ───────────────────────────────────────────────────────

function HeaterCard({ index }: { index: number }) {
  const model = useDuetStore(s => s.model)
  const tools = useTools()
  const heater = model?.heat?.heaters?.[index]
  const [editing, setEditing] = useState(false)
  const [editTemp, setEditTemp] = useState('')

  if (!heater) return null

  const label = heaterLabel(index, model)
  const isBed = (model?.heat?.bedHeaters ?? []).includes(index)
  const current = heater.current ?? 0
  const active = heater.active ?? 0
  const state = heater.state ?? 'off'
  const maxTemp = isBed ? 120 : 300
  const pct = Math.min(100, Math.max(0, (current / maxTemp) * 100))
  const isHeating = state === 'active' || state === 'standby'
  const color = isBed ? '#3b82f6' : '#ef4444'

  const setTemp = (temp: number) => {
    if (isBed) duetApi.sendGCode(`M140 S${temp}`)
    else {
      const tool = tools.find(t => t?.heaters?.includes(index))
      if (tool) duetApi.sendGCode(`M568 P${tool.number} S${temp}`)
    }
    setEditing(false)
  }

  return (
    <div className="bg-gray-800/40 rounded-xl p-3 border border-gray-700/30">
      <div className="flex items-center justify-between mb-1">
        <span className="text-xs font-medium text-gray-400">{label}</span>
        <span className={`text-[10px] px-1.5 py-0.5 rounded ${
          isHeating ? 'bg-orange-900/40 text-orange-300' : 'bg-gray-800 text-gray-500'
        }`}>{state}</span>
      </div>
      <div className="flex items-end gap-2">
        <span className="text-xl font-bold font-mono tabular-nums" style={{ color }}>
          {current.toFixed(1)}
        </span>
        <span className="text-xs text-gray-500 mb-0.5">/ {active}°C</span>
      </div>
      {/* Progress bar */}
      <div className="h-1 bg-gray-700 rounded-full mt-2 overflow-hidden">
        <div className="h-full rounded-full transition-all" style={{ width: `${pct}%`, backgroundColor: color }} />
      </div>
      {/* Quick set */}
      <div className="flex gap-1 mt-2">
        {(isBed ? [0, 50, 60, 80, 100] : [0, 180, 200, 220, 250]).map(t => (
          <button key={t} onClick={() => setTemp(t)}
            className={`flex-1 py-1 rounded text-[10px] font-mono transition ${
              active === t ? 'bg-primary/20 text-primary/70' : 'bg-gray-700/60 text-gray-400 hover:bg-gray-600/60 hover:text-gray-200'
            }`}>{t || 'Off'}</button>
        ))}
        {editing ? (
          <input autoFocus className="w-12 bg-gray-800 border border-primary/50 rounded text-center text-[10px] font-mono text-white focus:outline-none"
            value={editTemp} onChange={e => setEditTemp(e.target.value)}
            onKeyDown={e => { if (e.key === 'Enter') setTemp(parseFloat(editTemp) || 0); if (e.key === 'Escape') setEditing(false) }}
            onBlur={() => setEditing(false)} />
        ) : (
          <button onClick={() => { setEditing(true); setEditTemp(String(active)) }}
            className="w-12 py-1 rounded text-[10px] bg-gray-700/60 text-gray-400 hover:bg-gray-600/60 hover:text-gray-200 transition">Set</button>
        )}
      </div>
    </div>
  )
}

// ── Compact Temperature Chart ─────────────────────────────────────────────────

const HEATER_COLORS = ['#ef4444', '#f59e0b', '#10b981', '#3b82f6', '#8b5cf6', '#ec4899']

function TempChart() {
  const tempHistory = useDuetStore(s => s.tempHistory)
  const model = useDuetStore(s => s.model)
  const heaterEntries = useMemo(() => validHeaters(model), [model])
  const labels = useMemo(() => heaterEntries.map(e => e.label), [heaterEntries])

  if (!tempHistory.length || !labels.length) return null

  return (
    <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
      <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Temperature History</h3>
      <ResponsiveContainer width="100%" height={180}>
        <AreaChart data={tempHistory} margin={{ top: 5, right: 5, left: 0, bottom: 0 }}>
          <defs>
            {labels.map((_, i) => (
              <linearGradient key={i} id={`cg-${i}`} x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor={HEATER_COLORS[i % HEATER_COLORS.length]} stopOpacity={0.25} />
                <stop offset="95%" stopColor={HEATER_COLORS[i % HEATER_COLORS.length]} stopOpacity={0} />
              </linearGradient>
            ))}
          </defs>
          <CartesianGrid strokeDasharray="3 3" stroke="#1e293b" />
          <XAxis dataKey="label" stroke="#475569" tick={{ fontSize: 9 }} interval="preserveStartEnd" />
          <YAxis stroke="#475569" tick={{ fontSize: 9 }} domain={[0, 'auto']} unit="°" width={35} />
          <Tooltip
            contentStyle={{ backgroundColor: '#0f172a', border: '1px solid #334155', borderRadius: '8px', fontSize: 11 }}
            labelStyle={{ color: '#94a3b8', fontWeight: 600 }}
          />
          {labels.map((lbl, i) => (
            <Area key={lbl} type="monotone" dataKey={`heaters.${lbl}`} name={lbl}
              stroke={HEATER_COLORS[i % HEATER_COLORS.length]} strokeWidth={1.5}
              fill={`url(#cg-${i})`} dot={false} isAnimationActive={false} />
          ))}
        </AreaChart>
      </ResponsiveContainer>
    </div>
  )
}

// ── Inline File Browser ───────────────────────────────────────────────────────

function InlineFileBrowser({ onRun }: { onRun: (path: string) => void }) {
  const [files, setFiles] = useState<{ name: string; type: 'd' | 'f'; size: number }[]>([])
  const [dir, setDir] = useState('0:/gcodes')
  const [loading, setLoading] = useState(false)

  const loadFiles = useCallback(async (path: string) => {
    setLoading(true)
    try {
      const data = await duetApi.listFiles(path)
      setFiles(data.files ?? [])
      setDir(path)
    } catch {
      setFiles([])
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => { loadFiles(dir) }, []) // eslint-disable-line react-hooks/exhaustive-deps

  const goUp = () => {
    const parent = dir.substring(0, dir.lastIndexOf('/')) || '0:'
    loadFiles(parent)
  }

  return (
    <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
      <div className="flex items-center justify-between mb-2">
        <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold">Files</h3>
        <div className="flex gap-1 items-center">
          <button onClick={goUp} className="text-[10px] px-2 py-0.5 bg-gray-800 hover:bg-gray-700 text-gray-400 rounded transition">Up</button>
          <button onClick={() => loadFiles(dir)} className="text-[10px] px-2 py-0.5 bg-gray-800 hover:bg-gray-700 text-gray-400 rounded transition">Refresh</button>
        </div>
      </div>
      <div className="text-[10px] text-gray-600 mb-1 font-mono truncate">{dir}</div>
      <div className="max-h-40 overflow-y-auto space-y-0.5">
        {loading ? (
          <div className="text-[10px] text-gray-600 text-center py-3">Loading...</div>
        ) : files.length === 0 ? (
          <div className="text-[10px] text-gray-600 text-center py-3">No files</div>
        ) : files.map(f => (
          <div key={f.name}
            className="flex items-center justify-between py-1 px-1.5 rounded hover:bg-gray-800/60 transition group">
            <button
              onClick={() => f.type === 'd' ? loadFiles(`${dir}/${f.name}`) : onRun(`${dir}/${f.name}`)}
              className="text-[11px] text-gray-300 truncate text-left flex-1 min-w-0">
              {f.type === 'd' ? `📁 ${f.name}` : f.name}
            </button>
            {f.type !== 'd' && (
              <button onClick={() => { if (confirm(`Start printing ${f.name}?`)) duetApi.sendGCode(`M32 "${dir}/${f.name}"`) }}
                className="text-[9px] px-1.5 py-0.5 bg-green-900/40 text-green-300 rounded opacity-0 group-hover:opacity-100 transition border border-green-700/30">
                Print
              </button>
            )}
          </div>
        ))}
      </div>
    </div>
  )
}

// ── Override slider ───────────────────────────────────────────────────────────

function OverrideSlider({ label, value, unit, onChange, min, max }: {
  label: string; value: number; unit: string; onChange: (v: number) => void; min: number; max: number
}) {
  return (
    <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
      <div className="flex items-center justify-between mb-1.5">
        <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold">{label}</h3>
        <span className="text-sm font-mono font-bold text-white">{value}{unit}</span>
      </div>
      <input type="range" min={min} max={max} value={value}
        onChange={e => onChange(+e.target.value)}
        className="w-full h-1.5 bg-gray-700 rounded-full appearance-none cursor-pointer accent-primary"
        style={{ accentColor: 'rgb(var(--color-primary))' }} />
      <div className="flex justify-between text-[9px] text-gray-600 mt-0.5">
        <span>{min}{unit}</span>
        <button onClick={() => onChange(100)} className="text-gray-500 hover:text-gray-300 transition">Reset</button>
        <span>{max}{unit}</span>
      </div>
    </div>
  )
}

// ── Main unified control page ─────────────────────────────────────────────────

export default function PrinterControl() {
  const axes = useAxes()
  const tools = useTools()
  const fans = useFans()
  const model = useDuetStore(s => s.model)
  const status = useMachineStatus()
  const heaterEntries = useMemo(() => validHeaters(model), [model])

  const [step, setStep] = useState(10)
  const [extAmount, setExtAmount] = useState(10)
  const [extFeed, setExtFeed] = useState(5)
  const currentTool = model?.state?.currentTool ?? -1
  const visibleAxes = axes.filter(a => a.visible !== false)

  const jog = (axis: string, dir: number) => {
    const feedRate = AXIS_FEEDRATES[axis] ?? 3000
    duetApi.sendGCode(`M120\nG91\nG1 ${axis}${dir * step} F${feedRate}\nG90\nM121`)
  }

  const runMacro = (path: string) => duetApi.sendGCode(`M98 P"${path}"`)

  return (
    <div className="space-y-4">
      {/* ── Status bar ── */}
      <div className="flex items-center justify-between bg-gray-900/60 border border-gray-800 rounded-xl px-4 py-2.5">
        <div className="flex items-center gap-3">
          <span className={`px-3 py-1 rounded-full text-xs font-semibold ${statusColor(status)}`}>{statusLabel(status)}</span>
          {currentTool >= 0 && <span className="text-xs text-gray-400">Tool: T{currentTool}</span>}
          {/* Inline pause/resume when printing */}
          {(status === 'processing' || status === 'paused' || status === 'pausing' || status === 'resuming') && (
            <div className="flex gap-1.5 ml-2">
              <button onClick={() => duetApi.sendGCode('M25')}
                className="px-2.5 py-1 bg-yellow-900/40 hover:bg-yellow-800/50 text-yellow-300 rounded text-[10px] font-semibold transition border border-yellow-700/30">Pause</button>
              <button onClick={() => duetApi.sendGCode('M24')}
                className="px-2.5 py-1 bg-green-900/40 hover:bg-green-800/50 text-green-300 rounded text-[10px] font-semibold transition border border-green-700/30">Resume</button>
            </div>
          )}
        </div>
        <button
          onClick={() => { if (confirm('EMERGENCY STOP?')) duetApi.sendGCode('M112\nM999') }}
          className="px-4 py-1.5 bg-red-700 hover:bg-red-600 text-white rounded-lg text-xs font-black tracking-wider uppercase border border-red-500 transition shadow-sm shadow-red-900/30"
        >
          E-STOP
        </button>
      </div>

      {/* ── Print progress (visible during printing) ── */}
      {model?.job?.file?.fileName && (
        <div className="bg-gray-900/60 border border-gray-800 rounded-xl px-4 py-3">
          <div className="flex items-center justify-between mb-1.5">
            <span className="text-xs text-gray-400 truncate flex-1">{model.job.file.fileName}</span>
            <span className="text-xs font-mono text-white ml-2">
              {model.job.layer != null && `Layer ${model.job.layer}`}
            </span>
          </div>
          <div className="h-2 bg-gray-700 rounded-full overflow-hidden">
            <div className="h-full bg-green-500 rounded-full transition-all"
              style={{ width: `${Math.min(100, (model.job.filePosition ?? 0) / Math.max(1, model.job.file.size ?? 1) * 100)}%` }} />
          </div>
          <div className="flex justify-between mt-1.5 text-[10px] text-gray-500">
            <span>{model.job.duration != null ? `${Math.floor(model.job.duration / 60)}m elapsed` : ''}</span>
            <span>{model.job.timesLeft?.file != null ? `~${Math.ceil(model.job.timesLeft.file / 60)}m left` : ''}</span>
          </div>
        </div>
      )}

      {/* ── Speed / Flow / Baby Step overrides ── */}
      <div className="grid grid-cols-3 gap-3">
        <OverrideSlider label="Speed" value={model?.move?.speedFactor ?? 100} unit="%"
          onChange={v => duetApi.sendGCode(`M220 S${v}`)} min={10} max={300} />
        <OverrideSlider label="Flow" value={model?.move?.extruders?.[0]?.factor != null ? Math.round(model.move.extruders[0].factor * 100) : 100} unit="%"
          onChange={v => duetApi.sendGCode(`M221 S${v}`)} min={50} max={200} />
        <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
          <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-1.5">Baby Step Z</h3>
          <div className="flex gap-1">
            <button onClick={() => duetApi.sendGCode('M290 R0 S-0.05')}
              className="flex-1 py-1.5 bg-gray-700/60 hover:bg-gray-600/60 text-gray-300 rounded text-xs font-mono font-bold transition">-0.05</button>
            <button onClick={() => duetApi.sendGCode('M290 R0 S-0.01')}
              className="flex-1 py-1.5 bg-gray-700/60 hover:bg-gray-600/60 text-gray-300 rounded text-xs font-mono font-bold transition">-0.01</button>
            <button onClick={() => duetApi.sendGCode('M290 R0 S0.01')}
              className="flex-1 py-1.5 bg-gray-700/60 hover:bg-gray-600/60 text-gray-300 rounded text-xs font-mono font-bold transition">+0.01</button>
            <button onClick={() => duetApi.sendGCode('M290 R0 S0.05')}
              className="flex-1 py-1.5 bg-gray-700/60 hover:bg-gray-600/60 text-gray-300 rounded text-xs font-mono font-bold transition">+0.05</button>
          </div>
          <div className="text-center text-[10px] text-gray-500 mt-1">
            Z offset: {model?.move?.babystepZ != null
              ? `${model.move.babystepZ.toFixed(3)} mm` : '0.000 mm'}
          </div>
        </div>
      </div>

      {/* ── Three-column layout ── */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">

        {/* ═══ LEFT: Heaters + Quick Actions ═══ */}
        <div className="space-y-4">
          {/* Heaters */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
            <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Heaters</h3>
            <div className="space-y-2">
              {heaterEntries.map(e => <HeaterCard key={e.index} index={e.index} />)}
              {heaterEntries.length === 0 && <p className="text-xs text-gray-600 text-center py-2">No heaters detected</p>}
            </div>
          </div>

          {/* Fans */}
          {fans.length > 0 && (
            <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
              <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Fans</h3>
              <div className="space-y-1.5">
                {fans.map((fan, i) => {
                  const pct = Math.round(fan.requestedValue * 100)
                  return (
                    <div key={i} className="flex items-center gap-2">
                      <span className="text-[10px] text-gray-400 w-14 truncate">{fan.name || `Fan ${i}`}</span>
                      <div className="flex-1 h-1.5 bg-gray-700 rounded-full overflow-hidden">
                        <div className="h-full bg-cyan-400 transition-all rounded-full" style={{ width: `${pct}%` }} />
                      </div>
                      <span className="text-[10px] font-mono text-gray-400 w-8 text-right">{pct}%</span>
                      <div className="flex gap-0.5">
                        <button onClick={() => duetApi.sendGCode(`M106 P${i} S0`)} className="text-[9px] px-1.5 py-0.5 bg-gray-700/60 rounded text-gray-500 hover:text-gray-300 transition">0</button>
                        <button onClick={() => duetApi.sendGCode(`M106 P${i} S0.5`)} className="text-[9px] px-1.5 py-0.5 bg-gray-700/60 rounded text-gray-500 hover:text-gray-300 transition">50</button>
                        <button onClick={() => duetApi.sendGCode(`M106 P${i} S1`)} className="text-[9px] px-1.5 py-0.5 bg-gray-700/60 rounded text-gray-500 hover:text-gray-300 transition">100</button>
                      </div>
                    </div>
                  )
                })}
              </div>
            </div>
          )}

          {/* Print control (pause/resume/cancel) */}
          {model?.job?.file?.fileName && (
            <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
              <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Print Control</h3>
              <div className="grid grid-cols-3 gap-1.5">
                <button onClick={() => duetApi.sendGCode('M25')}
                  className="py-2 bg-yellow-900/40 hover:bg-yellow-800/50 text-yellow-300 rounded-lg text-[11px] font-medium transition border border-yellow-700/30">Pause</button>
                <button onClick={() => duetApi.sendGCode('M24')}
                  className="py-2 bg-green-900/40 hover:bg-green-800/50 text-green-300 rounded-lg text-[11px] font-medium transition border border-green-700/30">Resume</button>
                <button onClick={() => { if (confirm('Cancel the current print?')) duetApi.sendGCode('M0 H1') }}
                  className="py-2 bg-red-900/40 hover:bg-red-800/50 text-red-300 rounded-lg text-[11px] font-medium transition border border-red-700/30">Cancel</button>
              </div>
            </div>
          )}

          {/* Quick actions */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
            <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Quick Actions</h3>
            <div className="grid grid-cols-2 gap-1.5">
              <button onClick={() => duetApi.sendGCode('M84')} className="py-2 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-lg text-[11px] transition">Motors Off</button>
              <button onClick={() => duetApi.sendGCode('M140 S-273.15')} className="py-2 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-lg text-[11px] transition">Heaters Off</button>
              <button onClick={() => duetApi.sendGCode('M106 S0')} className="py-2 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-lg text-[11px] transition">Fans Off</button>
              <button onClick={() => duetApi.sendGCode('M400')} className="py-2 bg-gray-800/60 hover:bg-gray-700/60 text-gray-300 rounded-lg text-[11px] transition">Wait Moves</button>
            </div>
          </div>

          {/* Macros */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
            <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Macros</h3>
            <div className="grid grid-cols-2 gap-1.5">
              <button onClick={() => runMacro('homeall.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">homeall.g</button>
              <button onClick={() => runMacro('homex.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">homex.g</button>
              <button onClick={() => runMacro('homey.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">homey.g</button>
              <button onClick={() => runMacro('homez.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">homez.g</button>
              <button onClick={() => runMacro('bed.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">bed.g</button>
              <button onClick={() => runMacro('config-override.g')} className="py-2 bg-indigo-900/30 hover:bg-indigo-800/40 text-indigo-300 rounded-lg text-[11px] font-medium transition border border-indigo-700/20">config-override.g</button>
            </div>
          </div>
        </div>

        {/* ═══ CENTER: Axis Jog + Extruder + Tools ═══ */}
        <div className="space-y-4">
          {/* Axis controls */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
            <div className="flex items-center justify-between mb-2">
              <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold">Axes</h3>
              <button onClick={() => duetApi.sendGCode('G28')}
                className="px-3 py-1 bg-yellow-900/40 hover:bg-yellow-800/50 text-yellow-300 rounded-lg text-[10px] font-bold transition border border-yellow-700/30">
                Home All
              </button>
            </div>

            {/* Step selector */}
            <div className="flex gap-1 mb-3">
              {MOVE_STEPS.map(s => (
                <button key={s} onClick={() => setStep(s)}
                  className={`flex-1 py-1 rounded text-[10px] font-mono font-bold transition ${
                    step === s ? 'bg-primary/20 text-primary/70 border border-primary/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                  }`}>{s}</button>
              ))}
            </div>

            {/* Per-axis rows */}
            <div className="space-y-1.5">
              {visibleAxes.map(axis => {
                const color = AXIS_COLORS[axis.letter] ?? '#94a3b8'
                return (
                  <div key={axis.letter} className="flex items-center gap-1.5 bg-gray-800/40 rounded-lg p-1.5">
                    <div className="w-8 text-center">
                      <div className="text-xs font-bold" style={{ color }}>{axis.letter}</div>
                      <div className={`text-[7px] ${axis.homed ? 'text-green-400' : 'text-yellow-400'}`}>
                        {axis.homed ? 'OK' : '!'}
                      </div>
                    </div>
                    <button onClick={() => jog(axis.letter, -10)} className="h-7 px-1.5 bg-gray-700/60 hover:bg-gray-600 rounded text-gray-300 text-[9px] font-mono font-bold transition hidden sm:block">-{step * 10}</button>
                    <button onClick={() => jog(axis.letter, -1)} className="h-7 px-2 bg-gray-700 hover:bg-gray-600 rounded text-white text-[10px] font-mono font-bold transition">
                      <span style={{ color }}>-</span>{step}
                    </button>
                    <div className="flex-1 text-center">
                      <div className="text-xs font-mono font-bold text-white tabular-nums">{axis.userPosition.toFixed(2)}</div>
                    </div>
                    <button onClick={() => jog(axis.letter, 1)} className="h-7 px-2 bg-gray-700 hover:bg-gray-600 rounded text-white text-[10px] font-mono font-bold transition">
                      <span style={{ color }}>+</span>{step}
                    </button>
                    <button onClick={() => jog(axis.letter, 10)} className="h-7 px-1.5 bg-gray-700/60 hover:bg-gray-600 rounded text-gray-300 text-[9px] font-mono font-bold transition hidden sm:block">+{step * 10}</button>
                    <button onClick={() => duetApi.sendGCode(`G28 ${axis.letter}`)} className="h-7 px-1.5 bg-yellow-900/40 hover:bg-yellow-800/50 text-yellow-300 rounded text-[9px] font-bold transition">H</button>
                  </div>
                )
              })}
            </div>
          </div>

          {/* Tool selection + Extruder */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3">
            <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Extruder</h3>
            {tools.length > 0 && (
              <div className="flex gap-1 mb-2 flex-wrap">
                {tools.map(t => (
                  <button key={t.number} onClick={() => duetApi.sendGCode(`T${t.number}`)}
                    className={`px-2 py-1 rounded text-[10px] font-semibold transition ${
                      currentTool === t.number ? 'bg-primary/20 text-primary/70 border border-primary/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                    }`}>T{t.number}{t.name ? `: ${t.name}` : ''}</button>
                ))}
                <button onClick={() => duetApi.sendGCode('T-1')}
                  className={`px-2 py-1 rounded text-[10px] font-semibold transition ${
                    currentTool === -1 ? 'bg-gray-600 text-white border border-gray-500' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                  }`}>None</button>
              </div>
            )}
            <div className="flex gap-1 mb-2">
              {[1, 5, 10, 20, 50].map(a => (
                <button key={a} onClick={() => setExtAmount(a)}
                  className={`flex-1 py-1 rounded text-[10px] font-mono font-bold transition ${
                    extAmount === a ? 'bg-primary/20 text-primary/70 border border-primary/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                  }`}>{a}mm</button>
              ))}
            </div>
            <div className="flex gap-1 mb-2">
              {[1, 2, 5, 10].map(f => (
                <button key={f} onClick={() => setExtFeed(f)}
                  className={`flex-1 py-1 rounded text-[10px] font-mono transition ${
                    extFeed === f ? 'bg-primary/20 text-primary/70 border border-primary/40' : 'bg-gray-800 text-gray-400 border border-gray-700 hover:bg-gray-700'
                  }`}>{f}mm/s</button>
              ))}
            </div>
            <div className="flex gap-2">
              <button onClick={() => duetApi.sendGCode(`M120\nG91\nG1 E-${extAmount} F${extFeed * 60}\nG90\nM121`)}
                className="flex-1 py-2 bg-orange-800/40 hover:bg-orange-700/50 text-orange-300 rounded-lg text-xs font-semibold border border-orange-700/30 transition">Retract</button>
              <button onClick={() => duetApi.sendGCode(`M120\nG91\nG1 E${extAmount} F${extFeed * 60}\nG90\nM121`)}
                className="flex-1 py-2 bg-blue-800/40 hover:bg-blue-700/50 text-blue-300 rounded-lg text-xs font-semibold border border-blue-700/30 transition">Extrude</button>
            </div>
          </div>
        </div>

        {/* ═══ RIGHT: Console + Files ═══ */}
        <div className="space-y-4">
          {/* Console */}
          <div className="bg-gray-900/60 border border-gray-800 rounded-xl p-3 flex flex-col" style={{ minHeight: 360 }}>
            <h3 className="text-[10px] text-gray-500 uppercase tracking-wider font-semibold mb-2">Console</h3>
            <div className="flex-1 min-h-0">
              <InlineConsole />
            </div>
          </div>

          {/* Inline file browser */}
          <InlineFileBrowser onRun={runMacro} />
        </div>
      </div>

      {/* ── Temperature chart (full width below) ── */}
      <TempChart />
    </div>
  )
}
