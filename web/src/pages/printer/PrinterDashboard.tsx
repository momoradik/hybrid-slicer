import { useDuetStore, useHeaters, useAxes, useTools, useJob, useMachineStatus } from '../../store/duetStore'
import { statusChar } from '../../services/duetApi'
import * as duetApi from '../../services/duetApi'
import { useState } from 'react'
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, Legend } from 'recharts'

const HEATER_COLORS = ['#ef4444', '#f59e0b', '#10b981', '#3b82f6', '#8b5cf6', '#ec4899', '#06b6d4', '#84cc16']

function TempChart() {
  const tempHistory = useDuetStore(s => s.tempHistory)
  const heaters = useHeaters()

  if (!tempHistory.length) {
    return (
      <div className="h-64 flex items-center justify-center text-gray-600 text-sm">
        Waiting for temperature data...
      </div>
    )
  }

  const chartData = tempHistory.map(sample => {
    const point: Record<string, number | string> = { label: sample.label }
    sample.heaters.forEach((temp, i) => {
      point[`heater${i}`] = Math.round(temp * 10) / 10
    })
    return point
  })

  return (
    <ResponsiveContainer width="100%" height={280}>
      <LineChart data={chartData} margin={{ top: 5, right: 10, left: 0, bottom: 5 }}>
        <CartesianGrid strokeDasharray="3 3" stroke="#374151" />
        <XAxis dataKey="label" stroke="#6b7280" tick={{ fontSize: 11 }} interval="preserveStartEnd" />
        <YAxis stroke="#6b7280" tick={{ fontSize: 11 }} domain={[0, 'auto']} unit="°C" />
        <Tooltip
          contentStyle={{ backgroundColor: '#1f2937', border: '1px solid #374151', borderRadius: '8px' }}
          labelStyle={{ color: '#9ca3af' }}
        />
        <Legend />
        {heaters.map((_h, i) => (
          <Line
            key={i}
            type="monotone"
            dataKey={`heater${i}`}
            name={i === 0 ? 'Bed' : `Heater ${i}`}
            stroke={HEATER_COLORS[i % HEATER_COLORS.length]}
            dot={false}
            strokeWidth={2}
            isAnimationActive={false}
          />
        ))}
      </LineChart>
    </ResponsiveContainer>
  )
}

function StatusBadge({ status }: { status: string }) {
  const label = statusChar(status)
  const colors: Record<string, string> = {
    I: 'bg-green-900 text-green-300',
    P: 'bg-blue-900 text-blue-300 animate-pulse',
    S: 'bg-red-900 text-red-300',
    H: 'bg-red-900 text-red-300',
    B: 'bg-yellow-900 text-yellow-300 animate-pulse',
    M: 'bg-purple-900 text-purple-300',
    C: 'bg-yellow-900 text-yellow-300',
  }
  return (
    <span className={`px-3 py-1 rounded-full text-xs font-medium ${colors[status] ?? 'bg-gray-700 text-gray-300'}`}>
      {label}
    </span>
  )
}

function HeaterTable() {
  const heaters = useHeaters()
  const tools = useTools()
  const [editIdx, setEditIdx] = useState<number | null>(null)
  const [editTemp, setEditTemp] = useState('')

  const setHeaterTemp = async (heaterIdx: number, temp: number) => {
    if (heaterIdx === 0) {
      await duetApi.sendGCode(`M140 S${temp}`)
    } else {
      // Find which tool uses this heater
      const tool = tools.find(t => t.heaters.includes(heaterIdx))
      if (tool) {
        const hi = tool.heaters.indexOf(heaterIdx)
        const temps = [...tool.active]
        temps[hi] = temp
        await duetApi.sendGCode(`M568 P${tool.number} S${temps.join(':')}`)
      } else {
        await duetApi.sendGCode(`M568 P0 S${temp}`)
      }
    }
    setEditIdx(null)
  }

  return (
    <table className="w-full text-sm">
      <thead>
        <tr className="text-gray-500 text-xs">
          <th className="text-left py-1 font-medium">Heater</th>
          <th className="text-right py-1 font-medium">Current</th>
          <th className="text-right py-1 font-medium">Active</th>
          <th className="text-right py-1 font-medium">State</th>
          <th className="py-1"></th>
        </tr>
      </thead>
      <tbody>
        {heaters.map((h, i) => (
          <tr key={i} className="border-t border-gray-800">
            <td className="py-2 text-gray-300 font-medium">
              <span className="inline-block w-2 h-2 rounded-full mr-2" style={{ background: HEATER_COLORS[i % HEATER_COLORS.length] }} />
              {i === 0 ? 'Bed' : `Heater ${i}`}
            </td>
            <td className="py-2 text-right font-mono text-white">{h.current.toFixed(1)}°C</td>
            <td className="py-2 text-right">
              {editIdx === i ? (
                <input
                  className="input w-16 text-right text-xs py-0.5"
                  value={editTemp}
                  onChange={e => setEditTemp(e.target.value)}
                  onKeyDown={e => {
                    if (e.key === 'Enter') setHeaterTemp(i, parseFloat(editTemp) || 0)
                    if (e.key === 'Escape') setEditIdx(null)
                  }}
                  onBlur={() => setEditIdx(null)}
                  autoFocus
                />
              ) : (
                <button
                  onClick={() => { setEditIdx(i); setEditTemp(String(h.active)) }}
                  className="text-gray-400 hover:text-white font-mono"
                >
                  {h.active.toFixed(0)}°C
                </button>
              )}
            </td>
            <td className="py-2 text-right">
              <span className={`text-xs px-1.5 py-0.5 rounded ${
                h.state === 'active' ? 'bg-red-900/50 text-red-300' :
                h.state === 'standby' ? 'bg-yellow-900/50 text-yellow-300' :
                'bg-gray-800 text-gray-500'
              }`}>
                {h.state}
              </span>
            </td>
            <td className="py-2 text-right">
              {h.state === 'off' ? (
                <button onClick={() => setHeaterTemp(i, h.active)} className="text-xs text-green-400 hover:text-green-300">On</button>
              ) : (
                <button onClick={() => duetApi.sendGCode(i === 0 ? 'M140 S-273.15' : `M568 P0 R-273.15 S-273.15`)} className="text-xs text-red-400 hover:text-red-300">Off</button>
              )}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}

function PositionDisplay() {
  const axes = useAxes()
  return (
    <div className="grid grid-cols-3 gap-3">
      {axes.filter(a => a.visible !== false).map(axis => (
        <div key={axis.letter} className="bg-gray-800 rounded-lg p-3 text-center">
          <div className="text-xs text-gray-500 mb-1">{axis.letter}</div>
          <div className="text-lg font-mono text-white">{axis.userPosition.toFixed(2)}</div>
          <div className={`text-xs mt-1 ${axis.homed ? 'text-green-400' : 'text-yellow-400'}`}>
            {axis.homed ? 'Homed' : 'Not homed'}
          </div>
        </div>
      ))}
    </div>
  )
}

function SpeedFactors() {
  const model = useDuetStore(s => s.model)
  const speedFactor = model?.move?.speedFactor ?? 100
  const extruders = model?.move?.extruders ?? []
  const babystep = model?.move?.babystepZ ?? 0

  return (
    <div className="space-y-3">
      <div className="flex items-center justify-between">
        <span className="text-sm text-gray-400">Speed Factor</span>
        <div className="flex items-center gap-2">
          <button onClick={() => duetApi.sendGCode(`M220 S${Math.max(10, speedFactor - 10)}`)} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">-10</button>
          <span className="font-mono text-white w-12 text-center">{Math.round(speedFactor)}%</span>
          <button onClick={() => duetApi.sendGCode(`M220 S${speedFactor + 10}`)} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">+10</button>
        </div>
      </div>
      {extruders.map((ext, i) => (
        <div key={i} className="flex items-center justify-between">
          <span className="text-sm text-gray-400">Extruder {i} Factor</span>
          <div className="flex items-center gap-2">
            <button onClick={() => duetApi.sendGCode(`M221 D${i} S${Math.max(50, (ext.factor * 100) - 5)}`)} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">-5</button>
            <span className="font-mono text-white w-12 text-center">{Math.round(ext.factor * 100)}%</span>
            <button onClick={() => duetApi.sendGCode(`M221 D${i} S${(ext.factor * 100) + 5}`)} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">+5</button>
          </div>
        </div>
      ))}
      <div className="flex items-center justify-between">
        <span className="text-sm text-gray-400">Baby Stepping Z</span>
        <div className="flex items-center gap-2">
          <button onClick={() => duetApi.sendGCode('M290 R1 S-0.05')} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">-0.05</button>
          <span className="font-mono text-white w-16 text-center">{babystep.toFixed(3)}mm</span>
          <button onClick={() => duetApi.sendGCode('M290 R1 S0.05')} className="px-2 py-0.5 bg-gray-700 rounded text-xs hover:bg-gray-600">+0.05</button>
        </div>
      </div>
    </div>
  )
}

function JobProgress() {
  const job = useJob()
  const status = useMachineStatus()

  if (!job?.file?.fileName && status !== 'P') {
    return (
      <div className="text-center text-gray-600 py-4 text-sm">
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
        <span className="text-white font-mono ml-2">{progress.toFixed(1)}%</span>
      </div>
      <div className="w-full bg-gray-800 rounded-full h-2">
        <div className="bg-primary-400 h-2 rounded-full transition-all" style={{ width: `${progress}%` }} />
      </div>
      <div className="grid grid-cols-3 gap-2 text-xs text-gray-400">
        <div>
          <div className="text-gray-500">Layer</div>
          <div className="text-white font-mono">{job?.layer ?? '-'}</div>
        </div>
        <div>
          <div className="text-gray-500">Elapsed</div>
          <div className="text-white font-mono">{formatTime(job?.duration ?? null)}</div>
        </div>
        <div>
          <div className="text-gray-500">Remaining</div>
          <div className="text-white font-mono">{formatTime(job?.timesLeft?.file ?? null)}</div>
        </div>
      </div>
      {status === 'P' && (
        <div className="flex gap-2">
          <button onClick={() => duetApi.sendGCode('M25')} className="flex-1 px-3 py-1.5 bg-yellow-700 hover:bg-yellow-600 text-white rounded-lg text-xs">Pause</button>
          <button onClick={() => { if (confirm('Cancel the current print?')) duetApi.sendGCode('M0') }} className="flex-1 px-3 py-1.5 bg-red-700 hover:bg-red-600 text-white rounded-lg text-xs">Cancel</button>
        </div>
      )}
      {status === 'S' && (
        <button onClick={() => duetApi.sendGCode('M24')} className="w-full px-3 py-1.5 bg-green-700 hover:bg-green-600 text-white rounded-lg text-xs">Resume</button>
      )}
    </div>
  )
}

export default function PrinterDashboard() {
  const status = useMachineStatus()
  const model = useDuetStore(s => s.model)

  return (
    <div className="space-y-4">
      {/* Status bar */}
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-white">Printer Dashboard</h2>
        <div className="flex items-center gap-3">
          {model?.network?.name && <span className="text-sm text-gray-400">{model.network.name}</span>}
          <StatusBadge status={status} />
        </div>
      </div>

      {/* Main grid */}
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-4">
        {/* Temperature Chart */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 lg:col-span-2">
          <h3 className="text-sm font-medium text-white mb-2">Temperature</h3>
          <TempChart />
        </div>

        {/* Heater Controls */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Heaters</h3>
          <HeaterTable />
        </div>

        {/* Position */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Position</h3>
          <PositionDisplay />
        </div>

        {/* Job Progress */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Current Job</h3>
          <JobProgress />
        </div>

        {/* Speed / Extrusion / Babystep */}
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-4">
          <h3 className="text-sm font-medium text-white mb-3">Speed & Factors</h3>
          <SpeedFactors />
        </div>
      </div>
    </div>
  )
}
