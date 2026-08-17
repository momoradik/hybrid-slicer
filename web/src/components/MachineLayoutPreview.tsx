import { useRef, useCallback, useState } from 'react'
import type { ExtruderAssignment, OriginMode, BedDef } from '../types'

const NOZZLE_COLORS = ['#3b82f6', '#f97316', '#a78bfa', '#10b981', '#f43f5e', '#eab308', '#06b6d4', '#ec4899']
const DUTY_SHORT: Record<string, string> = { Walls: 'W', Infill: 'I', Support: 'S', All: '*' }

interface Props {
  travelX: number
  travelY: number
  originMode: OriginMode
  bedWidth: number
  bedDepth: number
  bedPositionX: number
  bedPositionY: number
  beds?: BedDef[]
  originX: number
  originY: number
  extruderCount: number
  nozzleXOffsets: number[]
  nozzleYOffsets: number[]
  leftEdge: number
  rightEdge: number
  frontEdge: number
  backEdge: number
  extruderAssignments: ExtruderAssignment[]
  isHybrid?: boolean
  cncOffsetX?: number
  cncOffsetY?: number
  highlight?: string | null
  // Callbacks for interactive editing
  onBedPositionChange?: (x: number, y: number) => void
  onBedSizeChange?: (w: number, d: number) => void
  onNozzleOffsetChange?: (index: number, dx: number, dy: number) => void
  onExtruder1PositionChange?: (frontEdge: number, leftEdge: number) => void
  onOriginChange?: (x: number, y: number) => void
  onCncOffsetChange?: (x: number, y: number) => void
  onBedChange?: (bedIndex: number, x: number, y: number, w: number, d: number) => void
  onSwapXY?: () => void
}

export default function MachineLayoutPreview({
  travelX, travelY, originMode: _om, bedWidth, bedDepth, bedPositionX, bedPositionY,
  beds: bedsProp,
  originX, originY,
  extruderCount, nozzleXOffsets, nozzleYOffsets,
  leftEdge, rightEdge, frontEdge, backEdge: _be,
  extruderAssignments, isHybrid, cncOffsetX, cncOffsetY,
  highlight,
  onBedPositionChange, onBedSizeChange, onNozzleOffsetChange,
  onExtruder1PositionChange, onOriginChange, onCncOffsetChange, onBedChange,
}: Props) {
  const svgRef = useRef<SVGSVGElement>(null)
  // Pure view-only toggle — flips ONLY the +X / +Y arrow labels at the origin.
  // Does NOT mutate any travel/bed/origin values, so the geometry never rotates.
  const [labelSwapped, setLabelSwapped] = useState(false)
  const dragRef = useRef<{
    type: string // 'bed' | 'origin' | 'bed-N' | 'bedResizeR-N' | 'bedResizeB-N' | 'bedResizeC-N' | 'nozzle-N'
    startMmX: number; startMmY: number
    startSvgX: number; startSvgY: number
  } | null>(null)

  // ── Layout math ─────────────────────────────────────────────────────────
  const W = 520, H = 360
  const pad = { top: 30, bottom: 44, left: 44, right: 44 }
  const mL = pad.left, mR = W - pad.right, mT = pad.top, mB = H - pad.bottom
  const mW = mR - mL, mH = mB - mT

  const scaleX = travelX > 0 ? mW / travelX : 1
  const scaleY = travelY > 0 ? mH / travelY : 1
  const scale = Math.min(scaleX, scaleY)
  const usedW = travelX * scale, usedH = travelY * scale
  const offX = mL + (mW - usedW) / 2
  const offY = mT + (mH - usedH) / 2

  // All beds (use prop array or fall back to single bed from legacy fields)
  const allBeds: BedDef[] = bedsProp && bedsProp.length > 0
    ? bedsProp
    : [{ index: 0, widthMm: bedWidth, depthMm: bedDepth, heightMm: 0, positionXMm: bedPositionX, positionYMm: bedPositionY }]

  // Helper: flip Y so +Y = up in SVG (SVG Y increases downward)
  const svgY = (machineY: number) => offY + usedH - machineY * scale

  // Primary bed (bed 1) for extruder positioning and backward compat
  const bedSvgX = offX + allBeds[0].positionXMm * scale
  const bedSvgY = svgY(allBeds[0].positionYMm + allBeds[0].depthMm)
  const bedSvgW = allBeds[0].widthMm * scale
  const bedSvgH = allBeds[0].depthMm * scale

  // Machine origin (explicit position in travel frame)
  const originSvgX = offX + originX * scale
  const originSvgY = svgY(originY)

  // Bed center (print reference — bed 1)
  const bedCenterSvgX = bedSvgX + bedSvgW / 2
  const bedCenterSvgY = bedSvgY + bedSvgH / 2

  // Extruder positions
  const extPosXmm: number[] = [frontEdge]
  const extPosYmm: number[] = [leftEdge]
  for (let i = 0; i < extruderCount - 1; i++) {
    extPosXmm.push(extPosXmm[extPosXmm.length - 1] + (nozzleXOffsets[i] ?? 0))
    extPosYmm.push(extPosYmm[extPosYmm.length - 1] + (nozzleYOffsets[i] ?? 0))
  }
  const extSvg = extPosXmm.map((xmm, i) => ({
    x: bedSvgX + extPosYmm[i] * (bedSvgW / (bedWidth || 1)),
    y: bedSvgY + bedSvgH - xmm * (bedSvgH / (bedDepth || 1)),
  }))

  const getDuty = (i: number) => {
    const a = extruderAssignments.find(a => a.extruderIndex === i)
    return a ? (DUTY_SHORT[a.duty] ?? a.duty[0]) : '?'
  }

  // ── SVG coordinate conversion ───────────────────────────────────────────
  const svgPoint = useCallback((clientX: number, clientY: number) => {
    const svg = svgRef.current
    if (!svg) return { x: 0, y: 0 }
    const pt = svg.createSVGPoint()
    pt.x = clientX; pt.y = clientY
    const svgP = pt.matrixTransform(svg.getScreenCTM()!.inverse())
    return { x: svgP.x, y: svgP.y }
  }, [])

  // svgToMm helpers kept for potential future use
  void _om; void _be; // suppress unused warnings for forwarded-but-unused props

  // ── Drag handlers ───────────────────────────────────────────────────────
  const onPointerDown = useCallback((
    e: React.PointerEvent,
    type: string,
    startMmX: number, startMmY: number,
  ) => {
    e.preventDefault()
    e.stopPropagation()
    const svg = svgRef.current
    if (!svg) return
    ;(e.target as Element).setPointerCapture(e.pointerId)
    const p = svgPoint(e.clientX, e.clientY)
    dragRef.current = { type, startMmX, startMmY, startSvgX: p.x, startSvgY: p.y }
  }, [svgPoint])

  const onPointerMove = useCallback((e: React.PointerEvent) => {
    const d = dragRef.current
    if (!d) return
    const p = svgPoint(e.clientX, e.clientY)
    const dxSvg = p.x - d.startSvgX, dySvg = p.y - d.startSvgY
    const dxMm = dxSvg / scale, dyMm = -dySvg / scale

    if (d.type.startsWith('bedResizeR-') && onBedChange) {
      const bi = parseInt(d.type.split('-')[1])
      const bed = allBeds[bi]
      if (bed) {
        const nw = Math.max(10, Math.min(travelX - bed.positionXMm, d.startMmX + dxMm))
        onBedChange(bi, bed.positionXMm, bed.positionYMm, Math.round(nw * 10) / 10, bed.depthMm)
      }
    } else if (d.type.startsWith('bedResizeB-') && onBedChange) {
      const bi = parseInt(d.type.split('-')[1])
      const bed = allBeds[bi]
      if (bed) {
        const nd = Math.max(10, Math.min(travelY - bed.positionYMm, d.startMmY - dyMm))
        onBedChange(bi, bed.positionXMm, bed.positionYMm, bed.widthMm, Math.round(nd * 10) / 10)
      }
    } else if (d.type.startsWith('bedResizeC-') && onBedChange) {
      const bi = parseInt(d.type.split('-')[1])
      const bed = allBeds[bi]
      if (bed) {
        const nw = Math.max(10, Math.min(travelX - bed.positionXMm, d.startMmX + dxMm))
        const nd = Math.max(10, Math.min(travelY - bed.positionYMm, d.startMmY - dyMm))
        onBedChange(bi, bed.positionXMm, bed.positionYMm, Math.round(nw * 10) / 10, Math.round(nd * 10) / 10)
      }
    } else if (d.type.startsWith('bed-') && onBedChange) {
      const bi = parseInt(d.type.split('-')[1])
      const bed = allBeds[bi]
      if (bed) {
        const nx = Math.max(0, Math.min(travelX - bed.widthMm, d.startMmX + dxMm))
        const ny = Math.max(0, Math.min(travelY - bed.depthMm, d.startMmY + dyMm))
        onBedChange(bi, Math.round(nx * 10) / 10, Math.round(ny * 10) / 10, bed.widthMm, bed.depthMm)
      }
    } else if (d.type === 'bed' && onBedPositionChange) {
      const nx = Math.max(0, Math.min(travelX - bedWidth, d.startMmX + dxMm))
      const ny = Math.max(0, Math.min(travelY - bedDepth, d.startMmY + dyMm))
      onBedPositionChange(Math.round(nx * 10) / 10, Math.round(ny * 10) / 10)
    } else if (d.type === 'bedRight' && onBedSizeChange) {
      const nw = Math.max(10, Math.min(travelX - bedPositionX, d.startMmX + dxMm))
      onBedSizeChange(Math.round(nw * 10) / 10, bedDepth)
    } else if (d.type === 'bedBottom' && onBedSizeChange) {
      const nd = Math.max(10, Math.min(travelY - bedPositionY, d.startMmY - dyMm))
      onBedSizeChange(bedWidth, Math.round(nd * 10) / 10)
    } else if (d.type === 'bedCorner' && onBedSizeChange) {
      const nw = Math.max(10, Math.min(travelX - bedPositionX, d.startMmX + dxMm))
      const nd = Math.max(10, Math.min(travelY - bedPositionY, d.startMmY - dyMm))
      onBedSizeChange(Math.round(nw * 10) / 10, Math.round(nd * 10) / 10)
    } else if (d.type === 'cnc' && onCncOffsetChange) {
      const newY = d.startMmX + dxMm
      const newX = d.startMmY + dyMm
      onCncOffsetChange(Math.round(newX * 10) / 10, Math.round(newY * 10) / 10)
    } else if (d.type === 'origin' && onOriginChange) {
      const nx = Math.max(0, Math.min(travelX, d.startMmX + dxMm))
      const ny = Math.max(0, Math.min(travelY, d.startMmY + dyMm))
      onOriginChange(Math.round(nx * 10) / 10, Math.round(ny * 10) / 10)
    } else if (d.type === 'nozzle-0' && onExtruder1PositionChange) {
      // E1 drag changes bed-edge offsets (front/left)
      const newLeft = Math.max(0, d.startMmX + dxMm)
      const newFront = Math.max(0, d.startMmY + dyMm)
      onExtruder1PositionChange(Math.round(newFront * 10) / 10, Math.round(newLeft * 10) / 10)
    } else if (d.type.startsWith('nozzle-') && onNozzleOffsetChange) {
      const idx = parseInt(d.type.split('-')[1])
      if (idx > 0) {
        const newY = d.startMmX + dxMm
        const newX = d.startMmY + dyMm
        onNozzleOffsetChange(idx - 1, Math.round(newX * 10) / 10, Math.round(newY * 10) / 10)
      }
    }
  }, [svgPoint, scale, travelX, travelY, bedWidth, bedDepth, bedPositionX, bedPositionY,
      onBedPositionChange, onBedSizeChange, onNozzleOffsetChange])

  const onPointerUp = useCallback(() => { dragRef.current = null }, [])

  // ── Highlight ───────────────────────────────────────────────────────────
  const hl = highlight ?? null
  const isDragging = dragRef.current !== null
  const opacity = (keys: string | string[]) => {
    if (!hl) return 1
    const arr = Array.isArray(keys) ? keys : [keys]
    return arr.includes(hl) ? 1 : 0.2
  }
  const nozzleOpacity = (i: number) => {
    if (!hl) return 1
    if (hl === `nozzle-${i}` || hl === `nozzleX-${i}` || hl === `nozzleX-${i-1}`
      || hl === `nozzleY-${i}` || hl === `nozzleY-${i-1}`) return 1
    if (hl === 'bed' || hl === 'travel') return 0.6
    return 0.25
  }
  const dimOpacity = (key: string) => opacity(key)

  const HANDLE = 6 // resize handle radius

  return (
    <div className="bg-gray-950/60 rounded-xl border border-gray-800 p-3 space-y-1">
      <div className="flex items-center justify-between">
        <p className="text-[10px] text-gray-500">
          {isDragging
            ? <span className="text-green-400">Dragging — release to set position</span>
            : hl
              ? <span className="text-gray-300">Highlighted: <span className="text-white font-medium">{formatHighlight(hl)}</span></span>
              : 'Drag the bed or nozzles to reposition. Drag bed edges to resize.'}
        </p>
        <button
          type="button"
          onClick={() => setLabelSwapped(v => !v)}
          title="Flip the +X / +Y axis labels in this preview only — no machine values are changed."
          className={`text-[10px] px-2 py-0.5 rounded border transition ${
            labelSwapped
              ? 'bg-blue-900/40 hover:bg-blue-900/60 text-blue-300 border-blue-700'
              : 'bg-gray-700 hover:bg-gray-600 text-gray-400 hover:text-gray-200 border-gray-600'
          }`}
        >Swap X/Y label</button>
      </div>
      <svg ref={svgRef} viewBox={`0 0 ${W} ${H}`} className="w-full select-none"
        aria-label="Interactive machine layout"
        onPointerMove={onPointerMove} onPointerUp={onPointerUp}>

        {/* ── Machine travel boundary ── */}
        <g opacity={opacity(['travel', 'bed'])}>
          <rect x={offX} y={offY} width={usedW} height={usedH}
            fill="none" stroke={hl === 'travel' ? '#9ca3af' : '#374151'}
            strokeWidth={hl === 'travel' ? 2 : 1} strokeDasharray="6,4" rx="2" />
          <text x={offX + usedW / 2} y={offY - 6} textAnchor="middle"
            fill={hl === 'travel' ? '#e5e7eb' : '#4b5563'} fontSize="8.5"
            fontFamily="ui-sans-serif,sans-serif">Machine: {travelX} × {travelY} mm</text>
        </g>

        {/* ── Beds / build areas ── */}
        {allBeds.map((bed, bi) => {
          const bx = offX + bed.positionXMm * scale
          const by = svgY(bed.positionYMm + bed.depthMm)
          const bw = bed.widthMm * scale
          const bh = bed.depthMm * scale
          const isPrimary = bi === 0
          const bedColor = isPrimary ? '#6b7280' : '#4b5563'
          return (
            <g key={bi} opacity={opacity(['bed', 'travel'])}>
              <rect x={bx} y={by} width={bw} height={bh}
                fill="#111827" stroke={hl === 'bed' ? '#d1d5db' : bedColor}
                strokeWidth={hl === 'bed' ? 2.5 : 1.5} rx="3"
                style={{ cursor: onBedChange ? 'move' : 'default' }}
                onPointerDown={e => onPointerDown(e, `bed-${bi}`, bed.positionXMm, bed.positionYMm)} />
              {/* Per-bed resize handles */}
              {onBedChange && <>
                <rect x={bx + bw - HANDLE/2} y={by + bh/2 - HANDLE} width={HANDLE} height={HANDLE*2} rx="1"
                  fill="#6b7280" fillOpacity="0.6" stroke="#9ca3af" strokeWidth="0.5"
                  style={{ cursor: 'ew-resize' }}
                  onPointerDown={e => onPointerDown(e, `bedResizeR-${bi}` as any, bed.widthMm, 0)} />
                <rect x={bx + bw/2 - HANDLE} y={by + bh - HANDLE/2} width={HANDLE*2} height={HANDLE} rx="1"
                  fill="#6b7280" fillOpacity="0.6" stroke="#9ca3af" strokeWidth="0.5"
                  style={{ cursor: 'ns-resize' }}
                  onPointerDown={e => onPointerDown(e, `bedResizeB-${bi}` as any, 0, bed.depthMm)} />
                <rect x={bx + bw - HANDLE} y={by + bh - HANDLE} width={HANDLE} height={HANDLE} rx="1"
                  fill="#9ca3af" fillOpacity="0.6" stroke="#d1d5db" strokeWidth="0.5"
                  style={{ cursor: 'nwse-resize' }}
                  onPointerDown={e => onPointerDown(e, `bedResizeC-${bi}` as any, bed.widthMm, bed.depthMm)} />
              </>}
              <text x={bx + bw / 2} y={by + bh + 12} textAnchor="middle"
                fill={hl === 'bed' ? '#e5e7eb' : '#9ca3af'} fontSize="8"
                fontFamily="ui-sans-serif,sans-serif">
                Bed {bi + 1}: {bed.widthMm}×{bed.depthMm}
              </text>
            </g>
          )
        })}

        {/* ── Machine origin (draggable) ── */}
        <g opacity={opacity('origin')} style={{ cursor: onOriginChange ? 'move' : 'default' }}
          onPointerDown={e => onPointerDown(e, 'origin', originX, originY)}>
          <circle cx={originSvgX} cy={originSvgY} r="6" fill="#ef4444" fillOpacity="0.15"
            stroke="#ef4444" strokeWidth="1.5" />
          <line x1={originSvgX - 8} y1={originSvgY} x2={originSvgX + 8} y2={originSvgY} stroke="#ef4444" strokeWidth="1" />
          <line x1={originSvgX} y1={originSvgY - 8} x2={originSvgX} y2={originSvgY + 8} stroke="#ef4444" strokeWidth="1" />
          <text x={originSvgX + 11} y={originSvgY - 6} fill="#ef4444" fontSize="8"
            fontFamily="ui-sans-serif,sans-serif">(0,0) origin</text>
        </g>

        {/* ── Axis arrows ── */}
        {/* labelSwapped flips ONLY the +X / +Y label text. The arrows themselves
            stay where they are so the diagram never rotates. */}
        <g opacity={opacity('origin')}>
          <line x1={originSvgX} y1={originSvgY} x2={originSvgX + 30} y2={originSvgY}
            stroke="#3b82f6" strokeWidth="1.2" markerEnd="url(#arrowB)" />
          <text x={originSvgX + 34} y={originSvgY + 3} fill="#3b82f6" fontSize="7.5"
            fontFamily="ui-sans-serif,sans-serif">{labelSwapped ? '+Y' : '+X'}</text>
          <line x1={originSvgX} y1={originSvgY} x2={originSvgX} y2={originSvgY - 30}
            stroke="#22c55e" strokeWidth="1.2" markerEnd="url(#arrowG)" />
          <text x={originSvgX + 4} y={originSvgY - 32} fill="#22c55e" fontSize="7.5"
            fontFamily="ui-sans-serif,sans-serif">{labelSwapped ? '+X' : '+Y'}</text>
        </g>
        <defs>
          <marker id="arrowB" markerWidth="6" markerHeight="6" refX="5" refY="3" orient="auto">
            <path d="M0,0 L6,3 L0,6 Z" fill="#3b82f6" />
          </marker>
          <marker id="arrowG" markerWidth="6" markerHeight="6" refX="3" refY="5" orient="auto">
            <path d="M0,6 L3,0 L6,6 Z" fill="#22c55e" />
          </marker>
        </defs>

        {/* ── Bed center (print reference) ── */}
        <circle cx={bedCenterSvgX} cy={bedCenterSvgY} r="3" fill="none" stroke="#6b7280" strokeWidth="1" strokeDasharray="2,2" />
        <text x={bedCenterSvgX + 6} y={bedCenterSvgY - 4} fill="#6b7280" fontSize="7"
          fontFamily="ui-sans-serif,sans-serif">print ref</text>

        {/* ── Bed edge dimensions ── */}
        {leftEdge > 0 && extSvg.length > 0 && (
          <g opacity={dimOpacity('leftEdge')}>
            <line x1={bedSvgX} y1={extSvg[0].y} x2={extSvg[0].x} y2={extSvg[0].y}
              stroke="#f59e0b" strokeWidth="1" strokeDasharray="3,2" />
            <text x={(bedSvgX + extSvg[0].x) / 2} y={extSvg[0].y - 4} textAnchor="middle"
              fill="#f59e0b" fontSize="8" fontFamily="ui-sans-serif,sans-serif">{leftEdge}</text>
          </g>
        )}
        {rightEdge > 0 && extSvg.length > 0 && (
          <g opacity={dimOpacity('rightEdge')}>
            <line x1={extSvg[extSvg.length-1].x} y1={extSvg[extSvg.length-1].y}
              x2={bedSvgX + bedSvgW} y2={extSvg[extSvg.length-1].y}
              stroke="#f59e0b" strokeWidth="1" strokeDasharray="3,2" />
            <text x={(extSvg[extSvg.length-1].x + bedSvgX + bedSvgW) / 2} y={extSvg[extSvg.length-1].y - 4}
              textAnchor="middle" fill="#f59e0b" fontSize="8" fontFamily="ui-sans-serif,sans-serif">{rightEdge}</text>
          </g>
        )}

        {/* ── Nozzle spacing ── */}
        {extruderCount > 1 && nozzleYOffsets.map((v, i) => {
          if (i >= extruderCount - 1) return null
          const y = Math.min(extSvg[i].y, extSvg[i+1].y) + 14 + i * 10
          return (
            <g key={`ny${i}`} opacity={dimOpacity(`nozzleY-${i}`)}>
              <line x1={extSvg[i].x} y1={y} x2={extSvg[i+1].x} y2={y}
                stroke="#8b5cf6" strokeWidth="1" strokeDasharray="3,2" />
              <text x={(extSvg[i].x + extSvg[i+1].x) / 2} y={y - 3} textAnchor="middle"
                fill="#8b5cf6" fontSize="7.5" fontFamily="ui-sans-serif,sans-serif">
                {v > 0 ? `Y${v}` : '—'}
              </text>
            </g>
          )
        })}
        {extruderCount > 1 && nozzleXOffsets.map((v, i) => {
          if (i >= extruderCount - 1 || v === 0) return null
          const x = Math.max(extSvg[i].x, extSvg[i+1].x) + 12 + i * 10
          return (
            <g key={`nx${i}`} opacity={dimOpacity(`nozzleX-${i}`)}>
              <line x1={x} y1={extSvg[i].y} x2={x} y2={extSvg[i+1].y}
                stroke="#8b5cf6" strokeWidth="1" strokeDasharray="3,2" />
              <text x={x + 5} y={(extSvg[i].y + extSvg[i+1].y) / 2 + 3}
                fill="#8b5cf6" fontSize="7.5" fontFamily="ui-sans-serif,sans-serif">X{v}</text>
            </g>
          )
        })}

        {/* ── Extruder markers (draggable for E1+) ── */}
        {extSvg.map((pos, i) => {
          const canDrag = (i === 0 && onExtruder1PositionChange) || (i > 0 && onNozzleOffsetChange)
          return (
          <g key={i} opacity={nozzleOpacity(i)}
            style={{ cursor: canDrag ? 'grab' : 'default' }}
            onPointerDown={canDrag ? e => {
              if (i === 0) onPointerDown(e, 'nozzle-0', leftEdge, frontEdge)
              else onPointerDown(e, `nozzle-${i}`, nozzleYOffsets[i-1] ?? 0, nozzleXOffsets[i-1] ?? 0)
            } : undefined}>
            {/* Invisible larger hit area for easier grabbing */}
            {canDrag && <circle cx={pos.x} cy={pos.y} r="14" fill="transparent" />}
            <circle cx={pos.x} cy={pos.y} r={canDrag ? 10 : 7}
              fill={NOZZLE_COLORS[i % NOZZLE_COLORS.length]} fillOpacity="0.15"
              stroke={NOZZLE_COLORS[i % NOZZLE_COLORS.length]} strokeWidth="1.5" />
            <circle cx={pos.x} cy={pos.y} r="2.5"
              fill={NOZZLE_COLORS[i % NOZZLE_COLORS.length]} />
            <text x={pos.x} y={pos.y - 14} textAnchor="middle"
              fill={NOZZLE_COLORS[i % NOZZLE_COLORS.length]} fontSize="8.5" fontWeight="600"
              fontFamily="ui-sans-serif,sans-serif">E{i + 1}</text>
            <text x={pos.x} y={pos.y + 18} textAnchor="middle"
              fill={NOZZLE_COLORS[i % NOZZLE_COLORS.length]} fontSize="7" fontWeight="500"
              fontFamily="ui-sans-serif,sans-serif">{getDuty(i)}</text>
          </g>
          )
        })}

        {/* ── CNC spindle (hybrid only, relative to E1) ── */}
        {isHybrid && extSvg.length > 0 && (() => {
          const e1 = extSvg[0]
          const cncX = cncOffsetY ?? 0 // CNC Y offset → SVG horizontal (same mapping as nozzles)
          const cncXmm = cncOffsetX ?? 0 // CNC X offset → SVG vertical (inverted)
          const cncSvgX = e1.x + cncX * (bedSvgW / (bedWidth || 1))
          const cncSvgY = e1.y - cncXmm * (bedSvgH / (bedDepth || 1))
          return (
            <g style={{ cursor: onCncOffsetChange ? 'grab' : 'default' }}
              onPointerDown={onCncOffsetChange
                ? e => onPointerDown(e, 'cnc', cncOffsetY ?? 0, cncOffsetX ?? 0)
                : undefined}>
              {/* Dashed line from E1 to CNC */}
              <line x1={e1.x} y1={e1.y} x2={cncSvgX} y2={cncSvgY}
                stroke="#d946ef" strokeWidth="1" strokeDasharray="4,3" opacity="0.5" />
              {/* CNC spindle marker — diamond shape (larger hit area for dragging) */}
              <circle cx={cncSvgX} cy={cncSvgY} r="12" fill="transparent" />
              <rect x={cncSvgX - 7} y={cncSvgY - 7} width={14} height={14} rx="2"
                fill="#d946ef" fillOpacity="0.15" stroke="#d946ef" strokeWidth="1.5"
                transform={`rotate(45,${cncSvgX},${cncSvgY})`} />
              <circle cx={cncSvgX} cy={cncSvgY} r="2" fill="#d946ef" />
              <text x={cncSvgX} y={cncSvgY - 14} textAnchor="middle"
                fill="#d946ef" fontSize="8" fontWeight="600"
                fontFamily="ui-sans-serif,sans-serif">CNC</text>
              {/* Offset label */}
              <text x={(e1.x + cncSvgX) / 2} y={Math.min(e1.y, cncSvgY) - 4} textAnchor="middle"
                fill="#d946ef" fontSize="7" opacity="0.7"
                fontFamily="ui-sans-serif,sans-serif">
                X{cncOffsetX ?? 0} Y{cncOffsetY ?? 0}
              </text>
            </g>
          )
        })()}
      </svg>

      {/* ── Legend ── */}
      <div className="flex flex-wrap gap-x-4 gap-y-1 text-[10px] justify-center pt-1">
        <span className="flex items-center gap-1">
          <span className="inline-block w-3 h-3 rounded border border-gray-600" style={{ borderStyle: 'dashed' }} />
          <span className="text-gray-500">Machine travel</span>
        </span>
        <span className="flex items-center gap-1">
          <span className="inline-block w-3 h-3 rounded bg-gray-900 border border-gray-500" />
          <span className="text-gray-400">Bed (drag to move)</span>
        </span>
        <span className="flex items-center gap-1">
          <span className="inline-block w-2.5 h-2.5 rounded-full border-2 border-red-500" />
          <span className="text-red-400">(0,0)</span>
        </span>
        {isHybrid && (
          <span className="flex items-center gap-1">
            <span className="inline-block w-2.5 h-2.5 rotate-45 border-2 border-fuchsia-500" style={{ borderRadius: 2 }} />
            <span className="text-fuchsia-400">CNC spindle</span>
          </span>
        )}
        {extruderCount > 0 && Array.from({ length: extruderCount }, (_, i) => (
          <span key={i} className="flex items-center gap-1">
            <span className="inline-block w-2.5 h-2.5 rounded-full" style={{ background: NOZZLE_COLORS[i % NOZZLE_COLORS.length] }} />
            <span style={{ color: NOZZLE_COLORS[i % NOZZLE_COLORS.length] }}>
              E{i + 1}{i > 0 ? ' (drag)' : ''}={extruderAssignments.find(a => a.extruderIndex === i)?.duty ?? 'All'}
            </span>
          </span>
        ))}
      </div>
    </div>
  )
}

function formatHighlight(key: string): string {
  if (key === 'travel')    return 'Machine travel'
  if (key === 'origin')    return 'Machine zero (0,0)'
  if (key === 'bed')       return 'Bed / build area'
  if (key === 'leftEdge')  return 'Left bed edge (Y)'
  if (key === 'rightEdge') return 'Right bed edge (Y)'
  if (key === 'frontEdge') return 'Front bed edge (X)'
  if (key === 'backEdge')  return 'Back bed edge (X)'
  if (key.startsWith('nozzleY-')) { const i = +key.split('-')[1]; return `Nozzle Y: E${i+1}→E${i+2}` }
  if (key.startsWith('nozzleX-')) { const i = +key.split('-')[1]; return `Nozzle X: E${i+1}→E${i+2}` }
  if (key.startsWith('nozzle-'))  { const i = +key.split('-')[1]; return `Extruder ${i+1}` }
  return key
}
