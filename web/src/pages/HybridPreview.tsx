import { useState, useEffect, useRef, useMemo, useCallback } from 'react'
import { Link } from 'react-router-dom'
import { useQuery } from '@tanstack/react-query'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { jobsApi, printProfilesApi, machineProfilesApi, customGCodeApi } from '../api/client'
import DisabledHint from '../components/DisabledHint'
import type { PrintJob } from '../types'

// ─────────────────────────────────────────────────────────────────────────────
// G-code parsers
// ─────────────────────────────────────────────────────────────────────────────

interface PrintSeg {
  x0: number; z0: number; y0: number   // THREE.js: X=gcodeX, Y=gcodeZ(height), Z=gcodeY
  x1: number; z1: number; y1: number
  layerIdx: number
  isSupport: boolean                    // true for ;TYPE:SUPPORT / SUPPORT_INTERFACE
}

interface CncMoveSim {
  x: number; z: number; y: number
  rapid: boolean
  blockIdx: number
}

interface ParsedPrint {
  segs: PrintSeg[]
  layerBoundaries: number[]  // segs index where each Cura layer starts
  maxZ: number
  // Prefix-sum counts for part vs support segments
  // partPrefix[i] = # of part segs in segs[0..i-1], suppPrefix[i] = support segs
  partPrefix: Int32Array
  suppPrefix: Int32Array
}

/** Build a per-axis regex from a 3-letter remapping string (e.g. "XVW").
 *  axes[0]=letter for machine X, axes[1]=Y, axes[2]=Z. Falls back to XYZ. */
function axisRegexes(axes: string): { x: RegExp; y: RegExp; z: RegExp } {
  const a = (axes && axes.length >= 3 ? axes : 'XYZ').toUpperCase()
  const lx = a[0] ?? 'X', ly = a[1] ?? 'Y', lz = a[2] ?? 'Z'
  const esc = (c: string) => c.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  // \b avoids matching the F (feed) letter when axis letter is e.g. F.
  // We anchor to a token start (start of line OR whitespace).
  return {
    x: new RegExp(`(?:^|\\s)${esc(lx)}([+-]?[\\d.]+)`),
    y: new RegExp(`(?:^|\\s)${esc(ly)}([+-]?[\\d.]+)`),
    z: new RegExp(`(?:^|\\s)${esc(lz)}([+-]?[\\d.]+)`),
  }
}

/** Parse the full print G-code — walls, infill, supports, everything.
 *  Cura TYPE comments are used to colour-code support vs part material.
 *  extruderAxes is the 3-letter machine remapping for the FDM head
 *  (default "XYZ"). Required so V/W-style remapped G-code parses correctly. */
function parsePrintGCode(gcode: string, extruderAxes: string = 'XYZ'): ParsedPrint {
  const ax = axisRegexes(extruderAxes)
  const segs: PrintSeg[] = []
  const layerBoundaries: number[] = [0]
  let cx = 0, cy = 0, cz = 0, ce = 0, hasPos = false, layerIdx = 0, maxZ = 0
  let isSupport = false

  for (const raw of gcode.split('\n')) {
    const t = raw.trim()

    // Cura type markers — detect support regions
    if (t.startsWith(';TYPE:')) {
      const typ = t.slice(6).toUpperCase()
      isSupport = typ.startsWith('SUPPORT')
      continue
    }

    // Layer markers
    if (t.startsWith(';LAYER:')) {
      if (segs.length > layerBoundaries[layerBoundaries.length - 1]) {
        layerBoundaries.push(segs.length)
        layerIdx++
      }
      continue
    }

    const line = t.split(';')[0].trim()
    if (!line) continue
    const up = line.toUpperCase()
    if (!up.startsWith('G0') && !up.startsWith('G1')) continue

    const xm = ax.x.exec(' ' + up), ym = ax.y.exec(' ' + up)
    const zm = ax.z.exec(' ' + up), em = up.match(/(?:^|\s)E([+-]?[\d.]+)/)
    const nx = xm ? parseFloat(xm[1]) : cx
    const ny = ym ? parseFloat(ym[1]) : cy
    const nz = zm ? parseFloat(zm[1]) : cz
    const ne = em ? parseFloat(em[1]) : ce

    if (hasPos && em && ne > ce) {
      segs.push({ x0: cx, z0: cz, y0: cy, x1: nx, z1: nz, y1: ny, layerIdx, isSupport })
      if (nz > maxZ) maxZ = nz
    }
    cx = nx; cy = ny; cz = nz; ce = ne; hasPos = true
  }
  layerBoundaries.push(segs.length)

  // Build prefix sums so animation can look up part/support counts in O(1)
  const partPrefix = new Int32Array(segs.length + 1)
  const suppPrefix = new Int32Array(segs.length + 1)
  for (let i = 0; i < segs.length; i++) {
    partPrefix[i + 1] = partPrefix[i] + (segs[i].isSupport ? 0 : 1)
    suppPrefix[i + 1] = suppPrefix[i] + (segs[i].isSupport ? 1 : 0)
  }

  return { segs, layerBoundaries, maxZ, partPrefix, suppPrefix }
}

/** Parse the real CNC toolpath G-code.
 *  cncAxes is the 3-letter machine remapping for the CNC head (default "XYZ").
 *  Required: the toolpath.gcode file written to disk has Y/Z replaced with the
 *  machine's actual axis letters (e.g. "V"/"W"). Without this mapping the
 *  parser only finds X values and the whole toolpath collapses onto Y=Z=0. */
function parseCncGCode(gcode: string, cncAxes: string = 'XYZ'): { moves: CncMoveSim[]; blockBoundaries: number[] } {
  const ax = axisRegexes(cncAxes)
  const moves: CncMoveSim[] = []
  const blockBoundaries: number[] = [0]
  let cx = 0, cy = 0, cz = 0, hasPos = false, blockIdx = 0

  for (const raw of gcode.split('\n')) {
    const t = raw.trim()
    if (/^;.*Layer\s+\d+/i.test(t) || t.startsWith('; === Postamble') || t.startsWith('; === Preamble')) {
      if (moves.length > blockBoundaries[blockBoundaries.length - 1]) {
        blockBoundaries.push(moves.length)
        blockIdx++
      }
      continue
    }
    const line = t.split(';')[0].trim()
    if (!line) continue
    const up = line.toUpperCase()
    const isG0 = /^G0($|\s)/.test(up), isG1 = /^G1($|\s)/.test(up)
    if (!isG0 && !isG1) continue
    const padded = ' ' + up
    const xm = ax.x.exec(padded), ym = ax.y.exec(padded), zm = ax.z.exec(padded)
    const nx = xm ? parseFloat(xm[1]) : cx
    const ny = ym ? parseFloat(ym[1]) : cy
    const nz = zm ? parseFloat(zm[1]) : cz
    if (hasPos) {
      const dx = nx - cx, dy = ny - cy, dz = nz - cz
      if (dx * dx + dy * dy + dz * dz > 0.0001)
        moves.push({ x: nx, z: nz, y: ny, rapid: isG0, blockIdx })
    }
    cx = nx; cy = ny; cz = nz; hasPos = true
  }
  blockBoundaries.push(moves.length)
  return { moves, blockBoundaries }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stage builder
// ─────────────────────────────────────────────────────────────────────────────

interface PrintStage   { type: 'print';   printSegStart: number; printSegEnd: number; label: string }
interface MachineStage { type: 'machine'; cncMoveStart: number;  cncMoveEnd: number;  label: string }
interface CustomStage  { type: 'custom';  label: string }
type SimStage = PrintStage | MachineStage | CustomStage

function buildStages(
  printLayerBoundaries: number[],
  cncBlockBoundaries: number[],
  machinedLayers: number[],
  totalPrintLayers: number,
): SimStage[] {
  if (machinedLayers.length === 0)
    return [{ type: 'print', printSegStart: 0, printSegEnd: printLayerBoundaries[printLayerBoundaries.length - 1], label: `Print all ${totalPrintLayers} layers` }]

  const stages: SimStage[] = []
  let lastPrintLayer = 0, cncBlockIdx = 0

  for (const ml of machinedLayers) {
    const pStart = printLayerBoundaries[Math.min(lastPrintLayer, printLayerBoundaries.length - 1)]
    const pEnd   = printLayerBoundaries[Math.min(ml, printLayerBoundaries.length - 1)]
    if (pEnd > pStart)
      stages.push({ type: 'print', printSegStart: pStart, printSegEnd: pEnd, label: `Print L${lastPrintLayer + 1}–L${ml}` })

    const cStart = cncBlockBoundaries[Math.min(cncBlockIdx, cncBlockBoundaries.length - 1)]
    const cEnd   = cncBlockBoundaries[Math.min(cncBlockIdx + 1, cncBlockBoundaries.length - 1)]
    if (cEnd > cStart)
      stages.push({ type: 'machine', cncMoveStart: cStart, cncMoveEnd: cEnd, label: `CNC @ L${ml}` })

    lastPrintLayer = ml
    cncBlockIdx++
  }

  const pStart = printLayerBoundaries[Math.min(lastPrintLayer, printLayerBoundaries.length - 1)]
  const pEnd   = printLayerBoundaries[printLayerBoundaries.length - 1]
  if (pEnd > pStart)
    stages.push({ type: 'print', printSegStart: pStart, printSegEnd: pEnd, label: `Print L${lastPrintLayer + 1}–L${totalPrintLayers}` })

  return stages
}

// ─────────────────────────────────────────────────────────────────────────────
// Hybrid G-code parser — reads the combined hybrid.gcode directly to produce
// accurate stages matching the real sequence including per-bed phases, custom
// G-code blocks, and bed switch transitions.
// ─────────────────────────────────────────────────────────────────────────────

interface HybridParsed {
  print: ParsedPrint
  cnc: { moves: CncMoveSim[]; blockBoundaries: number[] }
  stages: SimStage[]
}

function parseHybridGCode(
  gcode: string,
  extruderAxes: string = 'XYZ',
  cncAxes: string = 'XYZ',
): HybridParsed {
  const extAx = axisRegexes(extruderAxes)
  const cncAx = axisRegexes(cncAxes)
  const printSegs: PrintSeg[] = []
  const cncMoves: CncMoveSim[] = []
  const stages: SimStage[] = []

  let cx = 0, cy = 0, cz = 0, ce = 0, hasPos = false
  let isSupport = false, layerIdx = 0, maxZ = 0
  let mode: 'print' | 'cnc' | 'custom' | 'idle' = 'idle'
  let currentLabel = ''
  let stageStartPrint = 0, stageStartCnc = 0

  const flushStage = () => {
    if (mode === 'print' && printSegs.length > stageStartPrint) {
      stages.push({ type: 'print', printSegStart: stageStartPrint, printSegEnd: printSegs.length, label: currentLabel })
    } else if (mode === 'cnc' && cncMoves.length > stageStartCnc) {
      stages.push({ type: 'machine', cncMoveStart: stageStartCnc, cncMoveEnd: cncMoves.length, label: currentLabel })
    } else if (mode === 'custom') {
      stages.push({ type: 'custom', label: currentLabel })
    }
  }

  for (const raw of gcode.split('\n')) {
    const t = raw.trim()

    // Detect section markers from the hybrid G-code.
    // Two emitters produce hybrid.gcode with slightly different section labels:
    //   • MultiBedMerger: "; --- PRINT Bed 1, layers …", "; --- CNC Bed 1, layers …"
    //   • HybridOrchestrator (single-bed plan-hybrid): "; --- Print layers …",
    //     "; --- CNC Machining @ Layer …", "; --- CNC Preamble/Postamble …"
    // Match on the first word case-insensitively (PRINT/Print, CNC) so both
    // emitters drive the timeline. The "End CNC @ Layer" marker is ignored —
    // the next PRINT/CNC marker flushes the previous stage at the right point.
    const upT = t.toUpperCase()
    if (upT.startsWith('; --- PRINT ')) {
      flushStage()
      mode = 'print'
      currentLabel = t.replace(/^;\s*---\s*/, '').replace(/\s*---\s*$/, '')
      stageStartPrint = printSegs.length
      continue
    }
    if (upT.startsWith('; --- CNC ') && !upT.startsWith('; --- END CNC')) {
      flushStage()
      mode = 'cnc'
      currentLabel = t.replace(/^;\s*---\s*/, '').replace(/\s*---\s*$/, '')
      stageStartCnc = cncMoves.length
      continue
    }
    if (t.startsWith('; === Custom G-code:') || t.startsWith('; === Job Start') || t.startsWith('; === Job End')) {
      flushStage()
      mode = 'custom'
      currentLabel = t.replace(/^;\s*===\s*/, '').replace(/\s*===\s*$/, '')
      continue
    }
    if (t.startsWith('; === End Custom') || t.startsWith('; === End Job')) {
      flushStage()
      mode = 'idle'
      continue
    }
    if (t.startsWith('; >>> BED SWITCH:')) {
      flushStage()
      stages.push({ type: 'custom', label: t.replace(/^;\s*>>>\s*/, '').replace(/\s*<<<\s*$/, '') })
      mode = 'idle'
      continue
    }
    if (t.startsWith('; INTERVAL')) {
      // Don't create a stage, just note the interval
      continue
    }

    // Support detection
    if (t.startsWith(';TYPE:')) {
      isSupport = t.slice(6).toUpperCase().startsWith('SUPPORT')
      continue
    }
    if (t.startsWith(';LAYER:')) { layerIdx++; continue }

    // G92 E reset
    const codePart = t.split(';')[0].trim()
    if (!codePart) continue
    const up = codePart.toUpperCase()

    if (up.startsWith('G92')) {
      const em = up.match(/E([+-]?[\d.]+)/)
      if (em) ce = parseFloat(em[1])
      continue
    }

    if (!up.startsWith('G0') && !up.startsWith('G1')) continue

    // Pick axis mapping by current mode: CNC sections in hybrid.gcode use the
    // machine's CNC axis letters; PRINT sections use the extruder's letters.
    const ax = mode === 'cnc' ? cncAx : extAx
    const padded = ' ' + up
    const xm = ax.x.exec(padded), ym = ax.y.exec(padded)
    const zm = ax.z.exec(padded), em = up.match(/(?:^|\s)E([+-]?[\d.]+)/)
    const nx = xm ? parseFloat(xm[1]) : cx
    const ny = ym ? parseFloat(ym[1]) : cy
    const nz = zm ? parseFloat(zm[1]) : cz
    const ne = em ? parseFloat(em[1]) : ce

    if (mode === 'print' && hasPos && em && ne > ce) {
      printSegs.push({ x0: cx, z0: cz, y0: cy, x1: nx, z1: nz, y1: ny, layerIdx, isSupport })
      if (nz > maxZ) maxZ = nz
    } else if (mode === 'cnc' && hasPos) {
      const isG0 = /^G0($|\s)/i.test(up)
      const ddx = nx - cx, ddy = ny - cy, ddz = nz - cz
      if (ddx * ddx + ddy * ddy + ddz * ddz > 0.0001) {
        cncMoves.push({ x: nx, z: nz, y: ny, rapid: isG0, blockIdx: stages.length })
      }
    }

    cx = nx; cy = ny; cz = nz; ce = ne; hasPos = true
  }

  flushStage()

  // Build prefix sums for print segments
  const partPrefix = new Int32Array(printSegs.length + 1)
  const suppPrefix = new Int32Array(printSegs.length + 1)
  for (let i = 0; i < printSegs.length; i++) {
    partPrefix[i + 1] = partPrefix[i] + (printSegs[i].isSupport ? 0 : 1)
    suppPrefix[i + 1] = suppPrefix[i] + (printSegs[i].isSupport ? 1 : 0)
  }

  return {
    print: { segs: printSegs, layerBoundaries: [0, printSegs.length], maxZ, partPrefix, suppPrefix },
    cnc: { moves: cncMoves, blockBoundaries: [0, cncMoves.length] },
    stages,
  }
}

// Speed mapping: slider 1-100 → segs/sec on a log scale.
// Range expanded from [1, 1000] to [0.1, 1000] (4 orders of magnitude) so the
// user can crawl through the simulation segment-by-segment for inspection.
//   v=1   → 0.1  seg/s   (one segment every 10 seconds)
//   v=50  → ~10  seg/s
//   v=100 → 1000 seg/s
function sliderToSegsPerSec(v: number): number {
  const segs = Math.pow(10, ((v - 1) / 99) * 4 - 1)
  // Below 1 seg/s, return the float so very-slow stepping actually works.
  // Above, round to integer for the on-screen indicator.
  return segs < 1 ? Math.round(segs * 100) / 100 : Math.round(segs)
}

// ─────────────────────────────────────────────────────────────────────────────
// Visibility state (what layers / objects are shown in the 3D scene)
// ─────────────────────────────────────────────────────────────────────────────

interface Visibility {
  part: boolean
  support: boolean
  cncRapid: boolean
  cncCut: boolean
  nozzle: boolean
  tool: boolean
}

// ─────────────────────────────────────────────────────────────────────────────
// Three.js Hybrid Simulation Viewer
// ─────────────────────────────────────────────────────────────────────────────

interface BedInfo {
  index: number
  widthMm: number
  depthMm: number
  positionXMm: number
  positionYMm: number
}

interface ViewerProps {
  printGCode: string
  cncGCode: string
  machinedLayers: number[]
  totalPrintLayers: number
  layerHeightMm: number
  nozzleDiameterMm: number
  toolDiameterMm: number
  bedWidth: number
  bedDepth: number
  travelX?: number
  travelY?: number
  travelZ?: number
  originX?: number
  originY?: number
  beds?: BedInfo[]
  // Spindle position relative to nozzle on the same head (machine coords).
  // Used to park the CNC tool over the active bed when CNC doesn't move on
  // an axis, and to keep the nozzle / tool offset visually correct.
  hybridGCode?: string  // combined hybrid.gcode — when provided, used for accurate staging
  cncOffsetX?: number
  cncOffsetY?: number
  cncOffsetZ?: number
  // Per-tool axis-letter remappings written by IMachineCoordinateTranslator.
  // For a machine with cncAxes="XVW", the toolpath.gcode on disk uses V for Y
  // and W for Z; the parser must use the same mapping to decode positions.
  extruderAxes?: string  // 3 chars, e.g. "XYZ" or "XVW"
  cncAxes?: string       // 3 chars
  motionAssignment?: {
    enabled: boolean
    extruder: string  // axes extruder moves on, e.g. "YZ"
    cnc: string       // axes CNC moves on, e.g. "YZ"
    beds: string[]    // per-bed axes, e.g. ["X", "X"]
  }
}

export function HybridSimViewer({
  printGCode, cncGCode, machinedLayers, totalPrintLayers,
  layerHeightMm, nozzleDiameterMm, toolDiameterMm, bedWidth, bedDepth,
  travelX, travelY, travelZ, originX, originY, beds,
  hybridGCode,
  cncOffsetX = 0, cncOffsetY = 0, cncOffsetZ = 0,
  extruderAxes = 'XYZ', cncAxes = 'XYZ',
  motionAssignment,
}: ViewerProps) {
  const containerRef = useRef<HTMLDivElement>(null)
  const rendererRef  = useRef<THREE.WebGLRenderer | null>(null)
  const cameraRef    = useRef<THREE.PerspectiveCamera | null>(null)
  const controlsRef  = useRef<OrbitControls | null>(null)
  const animFrameRef = useRef<number>(0)
  const tickFrameRef = useRef<number>(0)

  const isPlayingRef = useRef(false)
  const stageIdxRef  = useRef(0)
  const progressRef  = useRef(0)
  const speedRef     = useRef(1)   // slider value 1-100

  const parsed = useMemo(() => {
    // Use hybrid G-code parser when available for accurate per-bed staging
    if (hybridGCode) return parseHybridGCode(hybridGCode, extruderAxes, cncAxes)
    // Fallback: separate print + CNC files
    const print  = parsePrintGCode(printGCode, extruderAxes)
    const cnc    = parseCncGCode(cncGCode, cncAxes)
    const stages = buildStages(print.layerBoundaries, cnc.blockBoundaries, machinedLayers, totalPrintLayers)
    return { print, cnc, stages }
  }, [printGCode, cncGCode, hybridGCode, machinedLayers, totalPrintLayers, extruderAxes, cncAxes])

  const [stageIdx,      setStageIdx]      = useState(0)
  const [isPlaying,     setIsPlaying]     = useState(false)
  const [speed,         setSpeed]         = useState(15)       // default low-range slider value
  const [stageProgress, setStageProgress] = useState(0)

  // Reset simulation state when data changes (new job loaded)
  useEffect(() => {
    stageIdxRef.current = 0
    progressRef.current = 0
    setStageIdx(0)
    setStageProgress(0)
    setIsPlaying(false)
  }, [parsed])
  const [vis, setVis] = useState<Visibility>({
    part: true, support: true, cncRapid: true, cncCut: true, nozzle: true, tool: true,
  })

  useEffect(() => { speedRef.current = speed }, [speed])
  const ma = motionAssignment?.enabled ? motionAssignment : null

  // ── Axis-letter → physical-direction mapping ──────────────────────────────
  // When CNC uses remapped axes (e.g. UVW instead of XYZ), the motion
  // assignment strings use the remapped letters. We need to know which
  // physical direction each letter corresponds to so the 3D preview moves
  // beds/tools along the correct scene axis.
  //   extruderAxes[0] → physical X,  [1] → physical Y,  [2] → physical Z
  //   cncAxes[0]      → physical X,  [1] → physical Y,  [2] → physical Z
  const axisToPhysical = useMemo(() => {
    const map: Record<string, string> = {}
    const eAx = (extruderAxes || 'XYZ').toUpperCase()
    const cAx = (cncAxes || 'XYZ').toUpperCase()
    if (eAx[0]) map[eAx[0]] = 'X'; if (eAx[1]) map[eAx[1]] = 'Y'; if (eAx[2]) map[eAx[2]] = 'Z'
    if (cAx[0]) map[cAx[0]] = 'X'; if (cAx[1]) map[cAx[1]] = 'Y'; if (cAx[2]) map[cAx[2]] = 'Z'
    return map
  }, [extruderAxes, cncAxes])

  /** Does any letter in `axes` map to the given physical direction? */
  const hasPhys = useCallback((axes: string, dir: string) => {
    for (const ch of axes.toUpperCase()) if (axisToPhysical[ch] === dir) return true
    return false
  }, [axisToPhysical])

  // ── Active bed and its scene-space reference centre ───────────────────────
  // The active bed is the one the print is on (detected from G-code centroid).
  // Its scene-space centre is the reference around which the bed slides during
  // motion-assigned animation: when G-code = bed centre, the bed stays at its
  // default position. That keeps every scene element (bed plate, deposited
  // material, parked nozzle / tool) within the travel envelope and over the
  // active bed instead of marching off to the machine corner.
  const ox = originX ?? 0, oy = originY ?? 0
  const bedListResolved: BedInfo[] = beds && beds.length > 0
    ? beds
    : [{ index: 0, widthMm: bedWidth, depthMm: bedDepth, positionXMm: 0, positionYMm: 0 }]
  const activeBed = useMemo(() => {
    const segs = parsed.print.segs
    if (segs.length === 0) return bedListResolved[0]
    let sumX = 0, sumY = 0
    for (const s of segs) { sumX += (s.x0 + s.x1) / 2; sumY += (s.y0 + s.y1) / 2 }
    const cx = sumX / segs.length, cy = sumY / segs.length
    return bedListResolved.find(b =>
      cx >= b.positionXMm && cx <= b.positionXMm + b.widthMm &&
      cy >= b.positionYMm && cy <= b.positionYMm + b.depthMm,
    ) ?? bedListResolved[0]
  }, [parsed, bedListResolved])
  const refX = activeBed.positionXMm + activeBed.widthMm / 2 - ox  // active bed centre in scene X
  const refZ = activeBed.positionYMm + activeBed.depthMm / 2 - oy  // active bed centre in scene Z
  const activeBedSceneIdx = bedListResolved.findIndex(b => b.index === activeBed.index)

  // Refs for scene objects that need to be accessed in animation / jump
  const partMeshRef    = useRef<THREE.InstancedMesh | null>(null)
  const supportMeshRef = useRef<THREE.InstancedMesh | null>(null)
  const cncRapidRef    = useRef<THREE.LineSegments | null>(null)
  const cncCutRef      = useRef<THREE.LineSegments | null>(null)
  // Groups for motion assignment: beds + parts move together
  const bedGroupsRef   = useRef<THREE.Group[]>([])
  const nozzleRef      = useRef<THREE.Group | null>(null)
  const toolRef        = useRef<THREE.Group | null>(null)

  // Apply visibility changes immediately to scene objects
  useEffect(() => {
    if (partMeshRef.current)    partMeshRef.current.visible    = vis.part
    if (supportMeshRef.current) supportMeshRef.current.visible = vis.support
    if (cncRapidRef.current)    cncRapidRef.current.visible    = vis.cncRapid
    if (cncCutRef.current)      cncCutRef.current.visible      = vis.cncCut
    // nozzle / tool visibility is managed per-frame during animation;
    // when not playing we also honour the toggle
    if (!isPlaying) {
      if (nozzleRef.current) nozzleRef.current.visible = vis.nozzle && (nozzleRef.current.visible)
      if (toolRef.current)   toolRef.current.visible   = vis.tool   && (toolRef.current.visible)
    }
  }, [vis, isPlaying])

  // ── Setup Three.js ────────────────────────────────────────────────────────
  useEffect(() => {
    const el = containerRef.current
    if (!el) return
    const w = el.clientWidth, h = el.clientHeight || 500

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x0d0d14)

    // Machine centre in scene coordinates (ox/oy declared at component scope)
    const txVal = travelX ?? bedWidth, tyVal = travelY ?? bedDepth
    const machineCentreX = -ox + txVal / 2
    const machineCentreZ = -oy + tyVal / 2
    const viewDist = Math.max(txVal, tyVal, 200) * 1.4

    const camera = new THREE.PerspectiveCamera(45, w / h, 0.1, 5000)
    // Camera sits at low three.js Z (in front of -Y side of machine) so that
    // +Y (G-code) → +Z (three.js) → further away → higher on screen.
    // This matches the 2D machine-config preview convention (+Y up on screen).
    camera.position.set(machineCentreX, viewDist * 0.5, machineCentreZ - viewDist * 0.8)
    cameraRef.current = camera

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setSize(w, h)
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    el.appendChild(renderer.domElement)
    rendererRef.current = renderer

    const controls = new OrbitControls(camera, renderer.domElement)
    // Set the orbit target at the part's vertical mid-height (THREE Y).
    // The dolly direction in OrbitControls is the line camera→target. With
    // target at bed-level (Y=0) and a tall part above the bed, zooming in
    // drives the camera down past the part — once the camera's Y drops below
    // the part, the part falls behind the camera and is culled, so it looks
    // like it "disappears". Re-targeting at the part's centre keeps zoom
    // tracking the part naturally for any part height.
    const partMidY = parsed.print.maxZ > 0
      ? parsed.print.maxZ / 2 + layerHeightMm  // include layer-height offset (matches deposit Y)
      : 0
    controls.target.set(machineCentreX, partMidY, machineCentreZ)
    controls.enableDamping = true
    controls.dampingFactor = 0.08
    controls.enablePan = true
    controls.screenSpacePanning = true
    // Dolly toward the cursor (CAD-style zoom-to-feature). Without this, the
    // camera always dollies toward the orbit target regardless of where the
    // user is looking.
    controls.zoomToCursor = true
    // Floor / ceiling on the dolly distance. The floor stops the camera from
    // ever flying through the scene; the ceiling matches the camera's far
    // plane so the user can't lose the part by zooming out forever.
    controls.minDistance = 5
    controls.maxDistance = 5000
    controls.mouseButtons = {
      LEFT: THREE.MOUSE.ROTATE,
      MIDDLE: THREE.MOUSE.PAN,
      RIGHT: THREE.MOUSE.PAN,
    }
    controls.update()
    controlsRef.current = controls

    scene.add(new THREE.AmbientLight(0xffffff, 0.6))
    const dir = new THREE.DirectionalLight(0xffffff, 1.4)
    dir.position.set(150, 200, 100)
    scene.add(dir)
    const fill = new THREE.DirectionalLight(0x8888ff, 0.4)
    fill.position.set(-100, -50, -100)
    scene.add(fill)

    // Machine dimensions (ox, oy, txVal, tyVal already computed above for camera)
    const tx = txVal, ty = tyVal, tz = travelZ ?? 350

    // ── Travel envelope (wireframe box) ─────────────────────────────────
    const envGeo = new THREE.BoxGeometry(tx, tz, ty)
    const envWire = new THREE.LineSegments(
      new THREE.EdgesGeometry(envGeo),
      new THREE.LineBasicMaterial({ color: 0x333355, transparent: true, opacity: 0.4 }),
    )
    envWire.position.set(-ox + tx / 2, tz / 2, -oy + ty / 2)
    scene.add(envWire)

    // ── Floor grid (at Y=0, spanning travel area) ───────────────────────
    const gridSize = Math.max(tx, ty)
    const grid = new THREE.GridHelper(gridSize, Math.round(gridSize / 20), 0x222233, 0x1a1a2e)
    grid.position.set(-ox + tx / 2, 0, -oy + ty / 2)
    scene.add(grid)
    scene.add(new THREE.AxesHelper(30))

    // ── Per-bed plates ──────────────────────────────────────────────────
    const bedList = beds && beds.length > 0
      ? beds
      : [{ index: 0, widthMm: bedWidth, depthMm: bedDepth, positionXMm: 0, positionYMm: 0 }]

    const bedColors = [0x3366cc, 0x33aa55, 0xcc5533, 0xaaaa33]
    const edgeColors = [0x5588ee, 0x55cc77, 0xee7755, 0xcccc55]

    // Create bed groups before the loop so bedParent can reference them
    const bedGroups: THREE.Group[] = []
    for (let i = 0; i < bedList.length; i++) {
      const bg = new THREE.Group()
      bg.frustumCulled = false  // beds move dynamically in motion-assignment mode
      scene.add(bg)
      bedGroups.push(bg)
    }
    bedGroupsRef.current = bedGroups

    for (const bed of bedList) {
      const bc = bedColors[bed.index % bedColors.length]
      const ec = edgeColors[bed.index % edgeColors.length]
      const bedParent = ma && bed.index < bedGroups.length ? bedGroups[bed.index] : scene

      // Bed centre in scene coords
      const bcx = bed.positionXMm + bed.widthMm / 2 - ox
      const bcy = bed.positionYMm + bed.depthMm / 2 - oy

      // Bed surface
      const bGeo = new THREE.PlaneGeometry(bed.widthMm, bed.depthMm)
      bGeo.rotateX(-Math.PI / 2)
      const bMesh = new THREE.Mesh(bGeo, new THREE.MeshPhongMaterial({
        color: bc, side: THREE.DoubleSide, transparent: true, opacity: 0.15,
      }))
      bMesh.position.set(bcx, 0.05, bcy)
      bMesh.frustumCulled = false
      bedParent.add(bMesh)

      // Bed outline (colored border)
      const outlineGeo = new THREE.EdgesGeometry(new THREE.PlaneGeometry(bed.widthMm, bed.depthMm))
      const outline = new THREE.LineSegments(outlineGeo, new THREE.LineBasicMaterial({ color: ec, linewidth: 2 }))
      outline.rotateX(-Math.PI / 2)
      outline.position.set(bcx, 0.1, bcy)
      outline.frustumCulled = false
      bedParent.add(outline)

      // Corner marker — small cylinder at front-left corner
      const cornerX = bed.positionXMm - ox + 2
      const cornerZ = bed.positionYMm - oy + 2
      const markerH = 8
      const marker = new THREE.Mesh(
        new THREE.CylinderGeometry(1.5, 1.5, markerH, 12),
        new THREE.MeshPhongMaterial({ color: ec, shininess: 80 }),
      )
      marker.position.set(cornerX, markerH / 2, cornerZ)
      bedParent.add(marker)

      // Number disc on top of corner marker
      const discCanvas = document.createElement('canvas')
      discCanvas.width = 64; discCanvas.height = 64
      const dCtx = discCanvas.getContext('2d')!
      dCtx.beginPath()
      dCtx.arc(32, 32, 30, 0, Math.PI * 2)
      dCtx.fillStyle = `#${ec.toString(16).padStart(6, '0')}`
      dCtx.fill()
      dCtx.fillStyle = '#ffffff'
      dCtx.font = 'bold 38px sans-serif'
      dCtx.textAlign = 'center'
      dCtx.textBaseline = 'middle'
      dCtx.fillText(`${bed.index + 1}`, 32, 33)
      const discTex = new THREE.CanvasTexture(discCanvas)
      const disc = new THREE.Sprite(new THREE.SpriteMaterial({ map: discTex, transparent: true }))
      disc.scale.set(5, 5, 1)
      disc.position.set(cornerX, markerH + 3, cornerZ)
      bedParent.add(disc)
    }

    // ── Two InstancedMesh objects: part (blue) and support (orange) ──────────
    const { segs, partPrefix, suppPrefix } = parsed.print
    const filW = Math.max(nozzleDiameterMm, 0.3) * 2.0
    const filH = Math.max(layerHeightMm,    0.1) * 1.5
    const cylGeo = new THREE.CylinderGeometry(0.5, 0.5, 1, 6)

    const nPart = partPrefix[segs.length]
    const nSupp = suppPrefix[segs.length]

    const partMesh = new THREE.InstancedMesh(cylGeo, new THREE.MeshPhongMaterial({ color: 0x2277dd }), Math.max(nPart, 1))
    partMesh.count = 0
    partMesh.frustumCulled = false   // count changes dynamically + position shifts in motion-assignment mode
    scene.add(partMesh)
    partMeshRef.current = partMesh

    const suppMesh = new THREE.InstancedMesh(cylGeo, new THREE.MeshPhongMaterial({ color: 0xff8800 }), Math.max(nSupp, 1))
    suppMesh.count = 0
    suppMesh.frustumCulled = false
    scene.add(suppMesh)
    supportMeshRef.current = suppMesh

    const dummy = new THREE.Object3D()
    const up    = new THREE.Vector3(0, 1, 0)
    let   pi = 0, si = 0

    for (let i = 0; i < segs.length; i++) {
      const s     = segs[i]
      const start = new THREE.Vector3(s.x0, s.z0, s.y0)
      const end   = new THREE.Vector3(s.x1, s.z1, s.y1)
      const dir3  = end.clone().sub(start)
      const len   = dir3.length()
      const target = s.isSupport ? suppMesh : partMesh
      const idx    = s.isSupport ? si++     : pi++
      if (len < 0.001) {
        dummy.scale.set(0, 0, 0); dummy.position.set(0, 0, 0); dummy.updateMatrix()
      } else {
        dummy.position.copy(start.clone().add(end).multiplyScalar(0.5))
        dummy.quaternion.setFromUnitVectors(up, dir3.clone().normalize())
        dummy.scale.set(filW, len, filH)
        dummy.updateMatrix()
      }
      target.setMatrixAt(idx, dummy.matrix)
    }
    partMesh.instanceMatrix.needsUpdate = true
    suppMesh.instanceMatrix.needsUpdate = true

    // ── CNC paths ─────────────────────────────────────────────────────────────
    // Build visualization geometry directly from parsed.cnc.moves so that the
    // vertex indices are perfectly aligned with the animation draw-range indices.
    // Previously this re-parsed cncGCode separately, which produced a different
    // move count (no small-move filter, different file when hybrid G-code is
    // used) and caused the contour's closing segments to be clipped off.
    {
      const { moves } = parsed.cnc
      const rapidPos: number[] = [], cutPos: number[] = []
      // CNC G-code has the spindle offset baked into every coordinate so the
      // gantry physically places the tool at the wall. Subtract it here so the
      // visualization aligns with the printed material (which is drawn in the
      // nozzle's coordinate frame, without the spindle offset).
      const cox = cncOffsetX, coy = cncOffsetY, coz = cncOffsetZ
      for (let i = 0; i < moves.length; i++) {
        const m = moves[i]
        const prev = i > 0 ? moves[i - 1] : m
        const arr = m.rapid ? rapidPos : cutPos
        // Three.js coords: X = gcode X, Y = gcode Z (height), Z = gcode Y (depth)
        // Subtract CNC offset so toolpath lines sit around the printed part
        arr.push(prev.x - cox, prev.z - coz, prev.y - coy,
                 m.x - cox,    m.z - coz,    m.y - coy)
      }
      const rapidGeo = new THREE.BufferGeometry()
      rapidGeo.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(rapidPos), 3))
      rapidGeo.setDrawRange(0, 0)
      const rapidLines = new THREE.LineSegments(rapidGeo, new THREE.LineBasicMaterial({ color: 0xffcc00 }))
      rapidLines.frustumCulled = false  // position shifts in motion-assignment mode
      scene.add(rapidLines)
      cncRapidRef.current = rapidLines

      const cutGeo = new THREE.BufferGeometry()
      cutGeo.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(cutPos), 3))
      cutGeo.setDrawRange(0, 0)
      const cutLines = new THREE.LineSegments(cutGeo, new THREE.LineBasicMaterial({ color: 0x2288ff }))
      cutLines.frustumCulled = false
      scene.add(cutLines)
      cncCutRef.current = cutLines
    }

    // ── FDM Nozzle (hot-end shape, orange tip) ────────────────────────────────
    const nozzleGroup = new THREE.Group()
    nozzleGroup.frustumCulled = false
    nozzleGroup.visible = false
    const blockMesh2 = new THREE.Mesh(new THREE.BoxGeometry(8, 8, 8), new THREE.MeshPhongMaterial({ color: 0xc8960c, shininess: 60 }))
    blockMesh2.position.y = 12; nozzleGroup.add(blockMesh2)
    const hbMesh2 = new THREE.Mesh(new THREE.CylinderGeometry(1.5, 1.5, 10, 8), new THREE.MeshPhongMaterial({ color: 0x444444 }))
    hbMesh2.position.y = 21; nozzleGroup.add(hbMesh2)
    const tipR  = Math.max(nozzleDiameterMm / 2, 0.6)
    const tipGeo2 = new THREE.ConeGeometry(tipR * 3, 6, 8); tipGeo2.rotateX(Math.PI)
    const tipMesh2 = new THREE.Mesh(tipGeo2, new THREE.MeshPhongMaterial({ color: 0xff8c00, shininess: 80 }))
    tipMesh2.position.y = 5; nozzleGroup.add(tipMesh2)
    scene.add(nozzleGroup)
    nozzleRef.current = nozzleGroup

    // ── CNC End Mill (tapered flute + collet) ─────────────────────────────────
    const toolGroup = new THREE.Group()
    toolGroup.frustumCulled = false
    toolGroup.visible = false
    const tr = Math.max(toolDiameterMm / 2, 1.0)
    const fluteMesh2 = new THREE.Mesh(new THREE.CylinderGeometry(tr * 0.9, tr, 18, 12), new THREE.MeshPhongMaterial({ color: 0xd8d8d8, shininess: 120 }))
    fluteMesh2.position.y = 9; toolGroup.add(fluteMesh2)
    const botMesh2 = new THREE.Mesh(new THREE.CircleGeometry(tr, 12), new THREE.MeshPhongMaterial({ color: 0xaaaaaa, side: THREE.DoubleSide }))
    botMesh2.rotation.x = Math.PI / 2; botMesh2.position.y = 0; toolGroup.add(botMesh2)
    const shankMesh2 = new THREE.Mesh(new THREE.CylinderGeometry(tr * 1.3, tr * 1.3, 20, 10), new THREE.MeshPhongMaterial({ color: 0x888888, shininess: 60 }))
    shankMesh2.position.y = 28; toolGroup.add(shankMesh2)
    const colletMesh2 = new THREE.Mesh(new THREE.TorusGeometry(tr * 1.5, tr * 0.3, 6, 12), new THREE.MeshPhongMaterial({ color: 0x555555 }))
    colletMesh2.position.y = 38; colletMesh2.rotation.x = Math.PI / 2; toolGroup.add(colletMesh2)
    scene.add(toolGroup)
    toolRef.current = toolGroup

    const onResize = () => {
      const w2 = el.clientWidth, h2 = el.clientHeight
      camera.aspect = w2 / h2; camera.updateProjectionMatrix()
      renderer.setSize(w2, h2)
    }
    window.addEventListener('resize', onResize)

    const animate = () => {
      animFrameRef.current = requestAnimationFrame(animate)
      controls.update()
      renderer.render(scene, camera)
    }
    animate()

    return () => {
      cancelAnimationFrame(animFrameRef.current)
      window.removeEventListener('resize', onResize)
      controls.dispose(); renderer.dispose()
      el.removeChild(renderer.domElement)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [parsed, nozzleDiameterMm, layerHeightMm, toolDiameterMm, bedWidth, bedDepth, cncAxes])

  // ── Animation tick ────────────────────────────────────────────────────────
  useEffect(() => {
    if (!isPlaying) { isPlayingRef.current = false; return }
    isPlayingRef.current = true
    const stages = parsed.stages
    let lastTime = performance.now()

    const tick = () => {
      if (!isPlayingRef.current) return
      const now = performance.now()
      const dt  = Math.min((now - lastTime) / 1000, 0.1)   // cap dt to avoid big jumps
      lastTime  = now

      const si = stageIdxRef.current
      if (si >= stages.length) { isPlayingRef.current = false; setIsPlaying(false); return }

      const stage = stages[si]
      const SEGS_PER_SEC = sliderToSegsPerSec(speedRef.current)

      // Custom stages (bed switches, custom G-code blocks) — auto-advance
      if (stage.type === 'custom') {
        stageIdxRef.current = si + 1; progressRef.current = 0
        setStageIdx(si + 1); setStageProgress(0)
        tickFrameRef.current = requestAnimationFrame(tick)
        return
      }

      if (stage.type === 'print') {
        const total = stage.printSegEnd - stage.printSegStart
        // Float arithmetic — Math.ceil would round up to 1 every frame at very
        // slow speeds, defeating the slow end of the slider.
        const added = SEGS_PER_SEC * dt
        const np    = Math.min(progressRef.current + added / Math.max(total, 1), 1)
        progressRef.current = np

        const visCount = stage.printSegStart + Math.floor(np * total)
        const { partPrefix, suppPrefix } = parsed.print
        if (partMeshRef.current)    partMeshRef.current.count    = partPrefix[visCount]
        if (supportMeshRef.current) supportMeshRef.current.count = suppPrefix[visCount]

        if (nozzleRef.current) {
          const { segs } = parsed.print
          const segIdx = Math.min(visCount, segs.length - 1)
          if (segIdx >= 0) {
            const s = segs[segIdx]
            const gx = s.x1, gy = s.z1 + layerHeightMm, gz = s.y1
            if (ma) {
              const eAxes = ma.extruder.toUpperCase()
              // Nozzle: tracks G-code on its assigned axes; on axes it can't
              // move on, parks at the active bed's centre so it visibly
              // hovers over the bed instead of at the machine origin.
              nozzleRef.current.position.set(
                hasPhys(eAxes, 'X') ? gx : refX,
                hasPhys(eAxes, 'Z') ? gy : 0,
                hasPhys(eAxes, 'Y') ? gz : refZ,
              )
              // Bed: slides relative to the active bed's centre so the deposit
              // point lands at the head's parked position. With this offset,
              // the bed swings ±half-bed-width around its default position
              // (well within the travel envelope) instead of marching from the
              // origin out to the bed-centre G-code coordinate.
              for (let bi = 0; bi < bedGroupsRef.current.length; bi++) {
                const bAxes = (ma.beds[bi] ?? '').toUpperCase()
                bedGroupsRef.current[bi].position.set(
                  hasPhys(bAxes, 'X') ? (refX - gx) : 0,
                  hasPhys(bAxes, 'Z') ? (-gy) : 0,
                  hasPhys(bAxes, 'Y') ? (refZ - gz) : 0,
                )
              }
              // Printed material + CNC toolpaths ride with the active bed.
              const activeBg = bedGroupsRef.current[activeBedSceneIdx] ?? bedGroupsRef.current[0]
              if (activeBg) {
                if (partMeshRef.current)    partMeshRef.current.position.copy(activeBg.position)
                if (supportMeshRef.current) supportMeshRef.current.position.copy(activeBg.position)
                if (cncRapidRef.current)    cncRapidRef.current.position.copy(activeBg.position)
                if (cncCutRef.current)      cncCutRef.current.position.copy(activeBg.position)
              }
            } else {
              nozzleRef.current.position.set(gx, gy, gz)
            }
            nozzleRef.current.visible = vis.nozzle
          }
        }
        if (toolRef.current) toolRef.current.visible = false

        setStageProgress(np)
        if (np >= 1) { stageIdxRef.current = si + 1; progressRef.current = 0; setStageIdx(si + 1); setStageProgress(0) }

      } else {
        const total = stage.cncMoveEnd - stage.cncMoveStart
        // Float arithmetic — Math.ceil would round up to 1 every frame at very
        // slow speeds, defeating the slow end of the slider.
        const added = SEGS_PER_SEC * dt
        const np    = Math.min(progressRef.current + added / Math.max(total, 1), 1)
        progressRef.current = np

        const visEnd = stage.cncMoveStart + Math.floor(np * total)
        const { moves } = parsed.cnc
        let rapidIdx = 0, cutIdx = 0
        for (let i = 0; i < visEnd && i < moves.length; i++) {
          if (moves[i].rapid) rapidIdx++; else cutIdx++
        }
        if (cncRapidRef.current) cncRapidRef.current.geometry.setDrawRange(0, rapidIdx * 2)
        if (cncCutRef.current)   cncCutRef.current.geometry.setDrawRange(0, cutIdx * 2)

        if (toolRef.current && visEnd > 0 && visEnd <= moves.length) {
          const m = moves[Math.max(0, visEnd - 1)]
          // Subtract CNC offset so the tool visually tracks the printed part
          const gx = m.x - cncOffsetX, gy = m.z - cncOffsetZ, gz = m.y - cncOffsetY
          if (ma) {
            const cAxes = ma.cnc.toUpperCase()
            toolRef.current.position.set(
              hasPhys(cAxes, 'X') ? gx : refX,
              hasPhys(cAxes, 'Z') ? gy : 0,
              hasPhys(cAxes, 'Y') ? gz : refZ,
            )
            for (let bi = 0; bi < bedGroupsRef.current.length; bi++) {
              const bAxes = (ma.beds[bi] ?? '').toUpperCase()
              bedGroupsRef.current[bi].position.set(
                hasPhys(bAxes, 'X') ? (refX - gx) : 0,
                hasPhys(bAxes, 'Z') ? (-gy) : 0,
                hasPhys(bAxes, 'Y') ? (refZ - gz) : 0,
              )
            }
            const activeBg = bedGroupsRef.current[activeBedSceneIdx] ?? bedGroupsRef.current[0]
            if (activeBg) {
              if (partMeshRef.current)    partMeshRef.current.position.copy(activeBg.position)
              if (supportMeshRef.current) supportMeshRef.current.position.copy(activeBg.position)
              if (cncRapidRef.current)    cncRapidRef.current.position.copy(activeBg.position)
              if (cncCutRef.current)      cncCutRef.current.position.copy(activeBg.position)
            }
          } else {
            toolRef.current.position.set(gx, gy, gz)
          }
          toolRef.current.visible = vis.tool
        }
        if (nozzleRef.current) nozzleRef.current.visible = false

        setStageProgress(np)
        if (np >= 1) { stageIdxRef.current = si + 1; progressRef.current = 0; setStageIdx(si + 1); setStageProgress(0) }
      }

      tickFrameRef.current = requestAnimationFrame(tick)
    }

    tickFrameRef.current = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(tickFrameRef.current)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isPlaying, parsed, layerHeightMm, vis])

  // ── Jump to stage ─────────────────────────────────────────────────────────
  const jumpToStage = useCallback((idx: number) => {
    const stages = parsed.stages
    if (idx < 0 || idx >= stages.length) return
    stageIdxRef.current = idx; progressRef.current = 0
    setStageIdx(idx); setStageProgress(0)

    let maxSeg = 0
    for (let i = 0; i < idx; i++) {
      const s = stages[i]
      if (s.type === 'print') maxSeg = s.printSegEnd
    }
    const { partPrefix, suppPrefix } = parsed.print
    if (partMeshRef.current)    partMeshRef.current.count    = partPrefix[maxSeg]
    if (supportMeshRef.current) supportMeshRef.current.count = suppPrefix[maxSeg]

    let maxCncMove = 0
    for (let i = 0; i < idx; i++) {
      const s = stages[i]
      if (s.type === 'machine') maxCncMove = s.cncMoveEnd
    }
    // Count rapids and cuts separately — the two buffers have independent indexing
    const { moves } = parsed.cnc
    let convergRapid = 0, convergCut = 0
    for (let i = 0; i < maxCncMove && i < moves.length; i++) {
      if (moves[i].rapid) convergRapid++; else convergCut++
    }
    if (cncRapidRef.current) cncRapidRef.current.geometry.setDrawRange(0, convergRapid * 2)
    if (cncCutRef.current)   cncCutRef.current.geometry.setDrawRange(0, convergCut * 2)

    if (nozzleRef.current) nozzleRef.current.visible = false
    if (toolRef.current)   toolRef.current.visible   = false
  }, [parsed])

  const stages = parsed.stages
  const currentStage = stages[stageIdx]
  const segsPerSec   = sliderToSegsPerSec(speed)

  // ── Legend toggle helper ─────────────────────────────────────────────────
  const Toggle = ({ label, color, field }: { label: string; color: string; field: keyof Visibility }) => (
    <button
      onClick={() => setVis(v => ({ ...v, [field]: !v[field] }))}
      className={`flex items-center gap-1.5 px-2.5 py-1 rounded-lg text-[11px] font-medium border transition select-none ${
        vis[field]
          ? 'bg-gray-800 border-gray-600 text-gray-200'
          : 'bg-gray-950 border-gray-800 text-gray-600 line-through opacity-50'
      }`}
    >
      <span className="inline-block w-3 h-3 rounded-sm shrink-0" style={{ background: color }} />
      {label}
    </button>
  )

  return (
    <div className="flex flex-col h-full">
      {/* Controls bar */}
      <div className="flex items-center gap-3 px-4 py-2 bg-gray-900 border-b border-gray-800 flex-wrap">
        <button
          onClick={() => setIsPlaying(p => !p)}
          className={`px-4 py-1.5 rounded-lg text-sm font-medium transition ${
            isPlaying ? 'bg-red-800 hover:bg-red-700 text-red-200' : 'bg-green-800 hover:bg-green-700 text-green-200'
          }`}
        >
          {isPlaying ? '⏸ Pause' : '▶ Play'}
        </button>

        <button onClick={() => jumpToStage(0)} className="px-3 py-1.5 text-xs bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg">
          ⏮ Reset
        </button>

        {/* Speed — logarithmic slider 1-100 */}
        <div className="flex items-center gap-2">
          <span className="text-xs text-gray-500">Slow</span>
          <input
            type="range" min={1} max={100} value={speed}
            onChange={e => setSpeed(+e.target.value)}
            className="w-28 accent-primary"
          />
          <span className="text-xs text-gray-500">Fast</span>
          <span className="text-[10px] text-gray-600 ml-1 tabular-nums w-16">
            {segsPerSec >= 1000 ? `${(segsPerSec / 1000).toFixed(1)}k` : segsPerSec} seg/s
          </span>
        </div>

        <div className="flex items-center gap-2 ml-2">
          <span className="text-xs text-gray-500">Stage</span>
          <select value={stageIdx} onChange={e => jumpToStage(+e.target.value)} className="input text-xs py-1 px-2 bg-gray-800">
            {stages.map((s, i) => (
              <option key={i} value={i}>{i + 1}. {s.label}</option>
            ))}
          </select>
        </div>

        {currentStage && (
          <div className="flex items-center gap-2 ml-auto">
            <span className={`text-xs px-2 py-0.5 rounded font-medium ${
              currentStage.type === 'print' ? 'bg-blue-900/60 text-blue-300' : 'bg-yellow-900/60 text-yellow-300'
            }`}>
              {currentStage.type === 'print' ? '🖨️ Printing' : '⚙️ Machining'}
            </span>
            <div className="w-32 h-1.5 bg-gray-700 rounded-full overflow-hidden">
              <div className="h-full bg-primary rounded-full transition-all" style={{ width: `${Math.round(stageProgress * 100)}%` }} />
            </div>
            <span className="text-xs text-gray-500">{Math.round(stageProgress * 100)}%</span>
          </div>
        )}
      </div>

      {/* Stage timeline */}
      <div className="flex gap-1 px-4 py-1.5 bg-gray-950 border-b border-gray-800 overflow-x-auto">
        {stages.map((s, i) => (
          <button key={i} onClick={() => jumpToStage(i)}
            className={`shrink-0 px-2 py-0.5 rounded text-[10px] transition ${
              i === stageIdx
                ? s.type === 'print' ? 'bg-blue-800 text-blue-200' : 'bg-yellow-800 text-yellow-200'
                : i < stageIdx ? 'bg-gray-800 text-gray-400' : 'bg-gray-900 text-gray-600 border border-gray-800'
            }`}
          >
            {s.type === 'print' ? '▣' : '⚙'} {s.label}
          </button>
        ))}
      </div>

      {/* 3D canvas */}
      <div ref={containerRef} className="flex-1 min-h-0" />

      {/* Legend + visibility toggles */}
      <div className="flex items-center gap-2 px-4 py-2 bg-gray-900 border-t border-gray-800 flex-wrap">
        <span className="text-[10px] text-gray-600 mr-1 shrink-0">Show/Hide:</span>
        <Toggle label="Part"        color="#2277dd" field="part"     />
        <Toggle label="Support"     color="#ff8800" field="support"  />
        <Toggle label="CNC Rapids"  color="#ffcc00" field="cncRapid" />
        <Toggle label="CNC Cuts"    color="#2288ff" field="cncCut"   />
        <Toggle label="Nozzle"      color="#ff8c00" field="nozzle"   />
        <Toggle label="CNC Tool"    color="#d8d8d8" field="tool"     />
        <span className="ml-auto text-[10px] text-gray-700 tabular-nums">
          LH {layerHeightMm}mm · Ø{nozzleDiameterMm}mm nozzle · Ø{toolDiameterMm}mm tool
        </span>
      </div>
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// Module-level persistence (survives React navigation)
// ─────────────────────────────────────────────────────────────────────────────

let _saved = {
  jobId: '',
  simReady: false,
  printGCode: null as string | null,
  cncGCode: null as string | null,
  hybridGCode: null as string | null,
  machinedLayers: [] as number[],
}

// ─────────────────────────────────────────────────────────────────────────────
// Main HybridPreview page
// ─────────────────────────────────────────────────────────────────────────────

export default function HybridPreview() {
  const { data: jobs     = [] } = useQuery({ queryKey: ['jobs'],          queryFn: jobsApi.getAll })
  const { data: profiles = [] } = useQuery({ queryKey: ['printProfiles'], queryFn: printProfilesApi.getAll })
  const { data: machines = [] } = useQuery({ queryKey: ['machines'],      queryFn: machineProfilesApi.getAll })
  // Wrapped: getAll now takes an optional machine filter, so it must not receive
  // react-query's QueryFunctionContext as its first argument.
  const { data: gCodeBlocks = [] } = useQuery({ queryKey: ['gcode-blocks'], queryFn: () => customGCodeApi.getAll() })

  const [jobId,          setJobId]          = useState(() => _saved.jobId)
  const [simReady,       setSimReady]       = useState(() => _saved.simReady)
  const [printGCode,     setPrintGCode]     = useState<string | null>(() => _saved.printGCode)
  const [cncGCode,       setCncGCode]       = useState<string | null>(() => _saved.cncGCode)
  const [hybridGCode,    setHybridGCode]    = useState<string | null>(() => _saved.hybridGCode)
  const [machinedLayers, setMachinedLayers] = useState<number[]>(() => _saved.machinedLayers)
  const [loading,        setLoading]        = useState(false)
  const [error,          setError]          = useState<string | null>(null)

  // Persist state to module level
  useEffect(() => { _saved.jobId = jobId }, [jobId])
  useEffect(() => { _saved.simReady = simReady }, [simReady])
  useEffect(() => { _saved.printGCode = printGCode }, [printGCode])
  useEffect(() => { _saved.cncGCode = cncGCode }, [cncGCode])
  useEffect(() => { _saved.hybridGCode = hybridGCode }, [hybridGCode])
  useEffect(() => { _saved.machinedLayers = machinedLayers }, [machinedLayers])

  const readyJobs    = jobs.filter(j => j.status === 'SlicingComplete' || j.status === 'ToolpathsComplete' || j.status === 'Ready')
  const selectedJob  = jobs.find(j => j.id === jobId) as PrintJob | undefined
  const printProfile = profiles.find(p => p.id === selectedJob?.printProfileId)
  const machine      = machines.find(m => m.id === selectedJob?.machineProfileId)

  const layerHeightMm    = printProfile?.layerHeightMm ?? 0.2
  const nozzleDiameterMm = (printProfile?.nozzleDiameterMm && printProfile.nozzleDiameterMm > 0)
    ? printProfile.nozzleDiameterMm
    : (printProfile?.lineWidthMm ?? 0.4)
  const bedWidth  = machine?.bedWidthMm  ?? 300
  const bedDepth  = machine?.bedDepthMm  ?? 300

  const loadAndRun = async () => {
    if (!jobId) return
    setLoading(true); setError(null)
    try {
      const pg = await jobsApi.getPrintGCode(jobId)
      // CNC toolpath may not exist yet (SlicingComplete jobs) — treat as empty
      let cg = ''
      try { cg = await jobsApi.getToolpathGCode(jobId) } catch { /* no toolpath yet */ }
      // Hybrid G-code (combined file) — for accurate simulation
      let hg: string | null = null
      try { hg = await jobsApi.getHybridGCode(jobId) } catch { /* no hybrid yet */ }
      setPrintGCode(pg); setCncGCode(cg); setHybridGCode(hg)
      const layers: number[] = []
      for (const line of cg.split('\n')) {
        const m = line.match(/^;.*Layer\s+(\d+)/i)
        if (m) {
          const n = parseInt(m[1])
          if (!isNaN(n) && !layers.includes(n)) layers.push(n)
        }
      }
      layers.sort((a, b) => a - b)
      setMachinedLayers(layers)
      setSimReady(true)
    } catch (e: unknown) {
      const err = e as { response?: { data?: { detail?: string } }; message?: string }
      setError(err?.response?.data?.detail ?? err?.message ?? 'Failed to load G-code')
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="h-full flex flex-col space-y-0">
      {!simReady && (
        <div className="p-6 space-y-6">
          <h2 className="text-2xl font-semibold text-white">Hybrid Preview</h2>
          <p className="text-sm text-gray-400">
            Simulates the full hybrid process using the real print and CNC G-code files.
            Part and support material are coloured differently. Use the legend to show or hide layers.
          </p>
          <div className="max-w-lg space-y-4">
            <div className="space-y-1">
              <label className="text-sm text-gray-400">Select Job</label>
              <select className="input w-full" value={jobId} onChange={e => setJobId(e.target.value)}>
                <option value="">Choose a job with toolpaths…</option>
                {readyJobs.map(j => (
                  <option key={j.id} value={j.id}>
                    {j.name} ({j.status}) — {j.totalPrintLayers ?? '?'} layers
                  </option>
                ))}
              </select>
              {jobs.length > 0 && readyJobs.length === 0 && (
                <p className="text-xs text-yellow-500 mt-1">No sliced jobs found. Slice a job in Import STL first.</p>
              )}
            </div>

            {selectedJob && (
              <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-2 text-sm">
                <div className="font-medium text-white">{selectedJob.name}</div>
                <div className="grid grid-cols-2 gap-x-6 gap-y-1 text-xs text-gray-400">
                  <span>Status: <span className="text-gray-300">{selectedJob.status}</span></span>
                  <span>Layers: <span className="text-gray-300">{selectedJob.totalPrintLayers ?? '?'}</span></span>
                  <span>Layer height: <span className="text-gray-300">{layerHeightMm} mm</span></span>
                  <span>Nozzle: <span className="text-gray-300">Ø{nozzleDiameterMm} mm</span></span>
                  <span>Bed: <span className="text-gray-300">{bedWidth}×{bedDepth} mm</span></span>
                </div>
              </div>
            )}

            {selectedJob && (() => {
              const enabledCount = gCodeBlocks.filter(b => b.isEnabled).length
              return enabledCount > 0 ? (
                <div className="bg-blue-950/40 border border-blue-800 rounded-lg px-3 py-2 text-xs text-blue-300 flex items-center justify-between">
                  <span>{enabledCount} G-code customisation block{enabledCount !== 1 ? 's' : ''} enabled — will be included in hybrid output</span>
                  <Link to="/custom-gcode" className="text-blue-400 hover:text-blue-200 underline ml-2">Edit</Link>
                </div>
              ) : (
                <div className="flex items-center gap-2 text-xs text-gray-600">
                  <span>No G-code customisation blocks enabled.</span>
                  <Link to="/custom-gcode" className="text-gray-500 hover:text-gray-300 underline">Add G-code customisation</Link>
                </div>
              )
            })()}

            {error && (
              <div className="bg-red-950/40 border border-red-800 rounded-lg px-3 py-2 text-xs text-red-400">{error}</div>
            )}

            <div className="flex gap-3">
              <DisabledHint when={!jobId} reason="Select a job with toolpaths above to run the simulation.">
                <button
                  onClick={loadAndRun}
                  disabled={!jobId || loading}
                  className="px-6 py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 text-white rounded-lg text-sm transition"
                >
                  {loading ? 'Loading G-code…' : '▶ Run Hybrid Simulation'}
                </button>
              </DisabledHint>
              {printGCode && cncGCode !== null && (
                <button
                  onClick={() => setSimReady(true)}
                  className="px-6 py-2.5 bg-cyan-800/80 hover:bg-cyan-700 text-cyan-200 rounded-lg text-sm transition"
                >
                  Resume Previous
                </button>
              )}
            </div>
          </div>
        </div>
      )}

      {simReady && printGCode && cncGCode !== null && (
        <div className="flex flex-col h-screen">
          <div className="flex items-center gap-4 px-6 py-3 bg-gray-900 border-b border-gray-800">
            <h2 className="text-lg font-semibold text-white">Hybrid Preview</h2>
            <span className="text-sm text-gray-400">{selectedJob?.name}</span>
            <span className="text-xs text-gray-600">
              {machinedLayers.length} machining stages · {selectedJob?.totalPrintLayers ?? '?'} print layers
            </span>
            <Link
              to="/custom-gcode"
              className="ml-auto px-3 py-1 text-xs bg-blue-900/60 hover:bg-blue-800 text-blue-300 rounded-lg border border-blue-700 transition"
            >
              G-code Customisation
            </Link>
            <button
              onClick={() => setSimReady(false)}
              className="px-3 py-1 text-xs bg-gray-800 hover:bg-gray-700 text-gray-300 rounded-lg"
            >
              ← Back
            </button>
          </div>
          <div className="flex-1 min-h-0">
            <HybridSimViewer
              printGCode={printGCode}
              cncGCode={cncGCode}
              hybridGCode={hybridGCode ?? undefined}
              machinedLayers={machinedLayers}
              totalPrintLayers={selectedJob?.totalPrintLayers ?? 100}
              layerHeightMm={layerHeightMm}
              nozzleDiameterMm={nozzleDiameterMm}
              toolDiameterMm={3}
              bedWidth={bedWidth}
              bedDepth={bedDepth}
              travelX={machine?.travelXMm}
              travelY={machine?.travelYMm}
              travelZ={machine?.bedHeightMm}
              originX={machine?.originXMm}
              originY={machine?.originYMm}
              cncOffsetX={machine?.cncOffset?.x ?? 0}
              cncOffsetY={machine?.cncOffset?.y ?? 0}
              cncOffsetZ={machine?.cncOffset?.z ?? 0}
              extruderAxes={machine?.extruderAxes ?? 'XYZ'}
              cncAxes={machine?.cncAxes ?? 'XYZ'}
              beds={machine ? (() => {
                try {
                  const raw = JSON.parse(machine.bedsJson || '[]') as any[]
                  if (raw.length > 0) return raw.map((b: any, i: number) => ({
                    index: b.index ?? b.Index ?? i,
                    widthMm: b.widthMm ?? b.WidthMm ?? machine.bedWidthMm,
                    depthMm: b.depthMm ?? b.DepthMm ?? machine.bedDepthMm,
                    positionXMm: b.positionXMm ?? b.PositionXMm ?? 0,
                    positionYMm: b.positionYMm ?? b.PositionYMm ?? 0,
                  }))
                } catch { /* fall through */ }
                return [{ index: 0, widthMm: machine.bedWidthMm, depthMm: machine.bedDepthMm, positionXMm: 0, positionYMm: 0 }]
              })() : undefined}
              motionAssignment={machine?.motionAssignmentEnabled ? (() => {
                try {
                  const ma = JSON.parse(machine.motionAssignmentJson || '{}')
                  return { enabled: true, extruder: ma.extruder ?? 'XYZ', cnc: ma.cnc ?? 'XYZ', beds: ma.beds ?? [] }
                } catch { return undefined }
              })() : undefined}
            />
          </div>
        </div>
      )}
    </div>
  )
}
