/**
 * Deviation histogram from core's per-point data.
 * DISPLAY ONLY — bins pre-computed deviation values for visual display.
 *
 * FIREWALL CLASSIFICATION: binning point_deviations into histogram buckets
 * is display-preparation (value->visual), the same category as value->color
 * mapping in the colormap. It does not compute a measurement, deviation,
 * transform, or verdict. The core computed the deviations; the UI arranges
 * them visually in bars.
 */
import { useRef, useEffect } from 'react'
import type { InspectionResult } from '../../api/alignmesh'

function drawHistogram(canvas: HTMLCanvasElement, result: InspectionResult) {
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  const w = canvas.width, h = canvas.height
  ctx.clearRect(0, 0, w, h)

  const devs = result.point_deviations ?? []
  const tolerance = result.tolerance_mm

  if (devs.length === 0) {
    ctx.fillStyle = '#9ca3af'
    ctx.font = '12px sans-serif'
    ctx.textAlign = 'center'
    ctx.fillText('No per-point deviation data', w / 2, h / 2)
    return
  }

  // Range from actual data
  let minDev = devs[0], maxDev = devs[0]
  for (const d of devs) {
    if (d < minDev) minDev = d
    if (d > maxDev) maxDev = d
  }
  const range = Math.max(Math.abs(minDev), Math.abs(maxDev), tolerance * 1.5)

  // Bin the core's pre-computed deviations for display
  const bins = 40
  const binWidth = (2 * range) / bins
  const counts: number[] = new Array(bins).fill(0)

  for (const d of devs) {
    const idx = Math.min(bins - 1, Math.max(0, Math.floor((d + range) / binWidth)))
    counts[idx]++
  }

  const maxCount = Math.max(...counts, 1)

  // Draw bars
  const barW = (w - 60) / bins
  const plotH = h - 40
  for (let i = 0; i < bins; i++) {
    const barH = (counts[i] / maxCount) * plotH * 0.9
    const x = 40 + i * barW
    const binCenter = -range + (i + 0.5) * binWidth
    const t = Math.abs(binCenter) / range
    const r = Math.min(255, Math.floor(t * 510))
    const g = Math.min(255, Math.floor((1 - t) * 255))
    ctx.fillStyle = 'rgb(' + r + ',' + g + ',80)'
    ctx.fillRect(x, plotH - barH + 20, barW - 1, barH)
  }

  // Tolerance band
  const tolLeft = 40 + ((range - tolerance) / (2 * range)) * (w - 60)
  const tolRight = 40 + ((range + tolerance) / (2 * range)) * (w - 60)
  ctx.strokeStyle = '#22c55e'
  ctx.lineWidth = 2
  ctx.setLineDash([4, 4])
  ctx.beginPath(); ctx.moveTo(tolLeft, 20); ctx.lineTo(tolLeft, plotH + 20); ctx.stroke()
  ctx.beginPath(); ctx.moveTo(tolRight, 20); ctx.lineTo(tolRight, plotH + 20); ctx.stroke()
  ctx.setLineDash([])

  // Axis labels
  ctx.fillStyle = '#9ca3af'
  ctx.font = '10px monospace'
  ctx.textAlign = 'center'
  ctx.fillText((-range).toFixed(3), 40, h - 5)
  ctx.fillText('0', w / 2, h - 5)
  ctx.fillText(range.toFixed(3), w - 20, h - 5)
  ctx.fillText('Deviation (mm)', w / 2, h - 18)

  // Legend
  ctx.fillStyle = '#22c55e'
  ctx.fillText('| tol |', (tolLeft + tolRight) / 2, 14)
}

export default function HistogramPanel({ result }: { result: InspectionResult }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    if (canvasRef.current) {
      drawHistogram(canvasRef.current, result)
    }
  }, [result])

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5">
      <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider mb-3">Deviation Distribution</h3>
      <canvas ref={canvasRef} width={600} height={200} className="w-full rounded bg-gray-950" />
      <p className="text-[10px] text-gray-600 mt-2">
        Histogram from {result.point_deviations?.length ?? 0} core-measured points
      </p>
    </div>
  )
}
