import { useState, useEffect, useRef } from 'react'
import * as duetApi from '../../services/duetApi'

interface HeightMapData {
  xMin: number
  xMax: number
  yMin: number
  yMax: number
  radius: number
  xSpacing: number
  ySpacing: number
  numX: number
  numY: number
  points: (number | null)[][]
  stats: { min: number; max: number; mean: number; rms: number }
}

function parseHeightMap(csv: string): HeightMapData | null {
  const lines = csv.split('\n').filter(l => l.trim())
  if (lines.length < 3) return null

  // First line: RepRapFirmware height map file v2 ...
  // Second line: xmin,xmax,ymin,ymax,radius,xspacing,yspacing,xnum,ynum
  const header = lines[1].split(',').map(Number)
  const [xMin, xMax, yMin, yMax, radius, xSpacing, ySpacing, numX, numY] = header

  const points: (number | null)[][] = []
  let min = Infinity, max = -Infinity, sum = 0, sumSq = 0, count = 0

  for (let i = 2; i < lines.length; i++) {
    const row = lines[i].split(',').map(v => {
      const n = parseFloat(v.trim())
      return isNaN(n) ? null : n
    })
    points.push(row)
    for (const v of row) {
      if (v !== null) {
        if (v < min) min = v
        if (v > max) max = v
        sum += v
        sumSq += v * v
        count++
      }
    }
  }

  const mean = count > 0 ? sum / count : 0
  const rms = count > 0 ? Math.sqrt(sumSq / count) : 0

  return {
    xMin, xMax, yMin, yMax, radius,
    xSpacing, ySpacing, numX, numY,
    points,
    stats: { min, max, mean, rms },
  }
}

function heightToColor(value: number | null, minVal: number, maxVal: number): string {
  if (value === null) return '#1f2937' // gray-800
  const range = Math.max(maxVal - minVal, 0.001)
  const normalized = (value - minVal) / range
  // Blue (low) -> Green (mid) -> Red (high)
  if (normalized < 0.5) {
    const t = normalized * 2
    const r = Math.round(0)
    const g = Math.round(t * 200)
    const b = Math.round((1 - t) * 255)
    return `rgb(${r},${g},${b})`
  } else {
    const t = (normalized - 0.5) * 2
    const r = Math.round(t * 255)
    const g = Math.round((1 - t) * 200)
    const b = Math.round(0)
    return `rgb(${r},${g},${b})`
  }
}

function HeightMapCanvas({ data }: { data: HeightMapData }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [hoveredCell, setHoveredCell] = useState<{ x: number; y: number; val: number | null } | null>(null)

  const cellSize = Math.min(500 / data.numX, 500 / data.numY, 40)
  const width = data.numX * cellSize
  const height = data.numY * cellSize
  const padding = 40

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    canvas.width = width + padding * 2
    canvas.height = height + padding * 2

    ctx.fillStyle = '#111827'
    ctx.fillRect(0, 0, canvas.width, canvas.height)

    // Draw cells
    for (let y = 0; y < data.points.length; y++) {
      for (let x = 0; x < (data.points[y]?.length ?? 0); x++) {
        const val = data.points[y][x]
        ctx.fillStyle = heightToColor(val, data.stats.min, data.stats.max)
        const px = padding + x * cellSize
        const py = padding + (data.points.length - 1 - y) * cellSize
        ctx.fillRect(px, py, cellSize - 1, cellSize - 1)

        // Draw value text if cells are large enough
        if (cellSize > 25 && val !== null) {
          ctx.fillStyle = '#d1d5db'
          ctx.font = `${Math.max(8, cellSize / 4)}px monospace`
          ctx.textAlign = 'center'
          ctx.textBaseline = 'middle'
          ctx.fillText(val.toFixed(3), px + cellSize / 2, py + cellSize / 2)
        }
      }
    }

    // Draw axis labels
    ctx.fillStyle = '#9ca3af'
    ctx.font = '11px sans-serif'
    ctx.textAlign = 'center'
    ctx.fillText(`X: ${data.xMin} to ${data.xMax} mm`, width / 2 + padding, height + padding + 25)
    ctx.save()
    ctx.translate(15, height / 2 + padding)
    ctx.rotate(-Math.PI / 2)
    ctx.fillText(`Y: ${data.yMin} to ${data.yMax} mm`, 0, 0)
    ctx.restore()
  }, [data, width, height, cellSize])

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current
    if (!canvas) return
    const rect = canvas.getBoundingClientRect()
    const mx = e.clientX - rect.left
    const my = e.clientY - rect.top
    const x = Math.floor((mx - padding) / cellSize)
    const y = data.points.length - 1 - Math.floor((my - padding) / cellSize)
    if (x >= 0 && x < data.numX && y >= 0 && y < data.points.length) {
      setHoveredCell({ x, y, val: data.points[y]?.[x] ?? null })
    } else {
      setHoveredCell(null)
    }
  }

  return (
    <div className="relative">
      <canvas
        ref={canvasRef}
        className="rounded-lg"
        onMouseMove={handleMouseMove}
        onMouseLeave={() => setHoveredCell(null)}
      />
      {hoveredCell && (
        <div className="absolute top-2 right-2 bg-gray-900 border border-gray-700 rounded-lg p-2 text-xs">
          <div className="text-gray-400">
            Point ({hoveredCell.x}, {hoveredCell.y})
          </div>
          <div className="text-white font-mono">
            {hoveredCell.val !== null ? `${hoveredCell.val.toFixed(4)} mm` : 'N/A'}
          </div>
        </div>
      )}
    </div>
  )
}

function ColorScale({ min, max }: { min: number; max: number }) {
  return (
    <div className="flex items-center gap-2 text-xs text-gray-400">
      <span className="font-mono">{min.toFixed(3)}</span>
      <div className="flex-1 h-3 rounded" style={{
        background: 'linear-gradient(to right, rgb(0,0,255), rgb(0,200,0), rgb(255,0,0))',
      }} />
      <span className="font-mono">{max.toFixed(3)}</span>
      <span className="ml-1">mm</span>
    </div>
  )
}

export default function PrinterHeightMap() {
  const [data, setData] = useState<HeightMapData | null>(null)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  const loadMap = async () => {
    setLoading(true)
    setError('')
    try {
      const csv = await duetApi.getHeightMap()
      const parsed = parseHeightMap(csv)
      if (!parsed) throw new Error('Failed to parse height map data')
      setData(parsed)
    } catch (err: unknown) {
      const msg = err instanceof Error ? err.message : String(err)
      setError(msg)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => { loadMap() }, [])

  const runProbing = async () => {
    try {
      await duetApi.sendGCode('G29 S0')
      // Reload after probing
      setTimeout(loadMap, 5000)
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : String(err))
    }
  }

  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h2 className="text-xl font-semibold text-white">Height Map</h2>
        <div className="flex gap-2">
          <button
            onClick={loadMap}
            disabled={loading}
            className="px-4 py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-sm disabled:opacity-50"
          >
            {loading ? 'Loading...' : 'Reload'}
          </button>
          <button
            onClick={runProbing}
            className="px-4 py-1.5 bg-primary/80 hover:bg-primary text-white rounded-lg text-sm"
          >
            Run Bed Probing (G29)
          </button>
        </div>
      </div>

      {error && (
        <div className="bg-red-950/40 border border-red-800 rounded-lg px-4 py-2 text-sm text-red-400">
          {error}
        </div>
      )}

      {data ? (
        <div className="grid grid-cols-1 lg:grid-cols-3 gap-4">
          {/* Map visualization */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 lg:col-span-2">
            <h3 className="text-sm font-medium text-white mb-3">Bed Mesh Visualization</h3>
            <HeightMapCanvas data={data} />
            <div className="mt-3">
              <ColorScale min={data.stats.min} max={data.stats.max} />
            </div>
          </div>

          {/* Statistics */}
          <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-4">
            <h3 className="text-sm font-medium text-white">Statistics</h3>
            <div className="space-y-2">
              {[
                { label: 'Min deviation', value: `${data.stats.min.toFixed(4)} mm`, color: 'text-blue-400' },
                { label: 'Max deviation', value: `${data.stats.max.toFixed(4)} mm`, color: 'text-red-400' },
                { label: 'Mean deviation', value: `${data.stats.mean.toFixed(4)} mm`, color: 'text-gray-300' },
                { label: 'RMS', value: `${data.stats.rms.toFixed(4)} mm`, color: 'text-gray-300' },
                { label: 'Range', value: `${(data.stats.max - data.stats.min).toFixed(4)} mm`, color: 'text-yellow-400' },
              ].map(s => (
                <div key={s.label} className="flex justify-between">
                  <span className="text-sm text-gray-400">{s.label}</span>
                  <span className={`text-sm font-mono ${s.color}`}>{s.value}</span>
                </div>
              ))}
            </div>

            <h3 className="text-sm font-medium text-white pt-2">Grid Info</h3>
            <div className="space-y-2">
              {[
                { label: 'Grid size', value: `${data.numX} x ${data.numY}` },
                { label: 'Spacing', value: `${data.xSpacing} x ${data.ySpacing} mm` },
                { label: 'Area', value: `${data.xMin}-${data.xMax} x ${data.yMin}-${data.yMax} mm` },
                { label: 'Probe radius', value: data.radius > 0 ? `${data.radius} mm` : 'N/A' },
              ].map(s => (
                <div key={s.label} className="flex justify-between">
                  <span className="text-sm text-gray-400">{s.label}</span>
                  <span className="text-sm font-mono text-gray-300">{s.value}</span>
                </div>
              ))}
            </div>

            <h3 className="text-sm font-medium text-white pt-2">Actions</h3>
            <div className="space-y-2">
              <button onClick={() => duetApi.sendGCode('G29 S1')} className="w-full py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Load Height Map (G29 S1)</button>
              <button onClick={() => duetApi.sendGCode('G29 S2')} className="w-full py-1.5 bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg text-xs">Disable Compensation (G29 S2)</button>
            </div>
          </div>
        </div>
      ) : !loading && !error ? (
        <div className="bg-gray-900 border border-gray-800 rounded-xl p-8 text-center text-gray-600">
          <p className="text-lg mb-2">No height map data</p>
          <p className="text-sm">Run bed probing (G29 S0) to generate a height map.</p>
        </div>
      ) : null}
    </div>
  )
}
