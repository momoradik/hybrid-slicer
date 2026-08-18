import { useEffect, useRef, useState, useMemo } from 'react'
import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import type { BuildVolume } from './StlViewer'

// ── Types ─────────────────────────────────────────────────────────────────────

interface UnmachinableRegion {
  zHeightMm: number
  reason: string
  bounds: { minX: number; minY: number; maxX: number; maxY: number }
}

interface Props {
  toolpathGCode: string
  printGCode: string
  buildVolume: BuildVolume
  toolDiameterMm?: number
  unmachinableRegions?: UnmachinableRegion[]
  className?: string
  bedPositionX?: number
  bedPositionY?: number
  originX?: number
  originY?: number
}

interface SimMove {
  tx0: number; ty0: number; tz0: number
  tx1: number; ty1: number; tz1: number
  isRapid: boolean
  durationSec: number
}

interface PrintSegment {
  x0: number; y0: number; z0: number
  x1: number; y1: number; z1: number
  isSupport: boolean
}

type VisKey = 'rapid' | 'cut' | 'printed' | 'support' | 'unmachinable'

// ── Parse CNC G-code ──────────────────────────────────────────────────────────

function parseCncMoves(gcode: string): { moves: SimMove[]; estimatedMachiningSec: number } {
  const moves: SimMove[] = []
  let gx = 0, gy = 0, gz = 0
  let feed = 800
  let hasPos = false
  let totalTimeSec = 0
  const RAPID_SPEED = 6000

  for (const raw of gcode.split('\n')) {
    const line = raw.split(';')[0].trim()
    if (!line) continue
    const up = line.toUpperCase()

    const fm = up.match(/F([\d.]+)/)
    if (fm) feed = parseFloat(fm[1])

    const isG0 = /^G0($|\s)/.test(up)
    const isG1 = /^G1($|\s)/.test(up)
    if (!isG0 && !isG1) continue

    const xm = up.match(/X([+-]?[\d.]+)/)
    const ym = up.match(/Y([+-]?[\d.]+)/)
    const zm = up.match(/Z([+-]?[\d.]+)/)

    const nx = xm ? parseFloat(xm[1]) : gx
    const ny = ym ? parseFloat(ym[1]) : gy
    const nz = zm ? parseFloat(zm[1]) : gz

    if (hasPos) {
      const dx = nx - gx, dy = ny - gy, dz = nz - gz
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz)
      if (dist > 0.0005) {
        const effFeed = isG0 ? RAPID_SPEED : Math.max(feed, 1)
        const dur = (dist / effFeed) * 60
        if (!isG0) totalTimeSec += dur
        moves.push({
          tx0: gx, ty0: gz, tz0: gy,
          tx1: nx, ty1: nz, tz1: ny,
          isRapid: isG0,
          durationSec: dur,
        })
      }
    }

    gx = nx; gy = ny; gz = nz
    hasPos = true
  }

  return { moves, estimatedMachiningSec: totalTimeSec }
}

// ── Parse print G-code ────────────────────────────────────────────────────────

function parsePrintSegments(gcode: string): PrintSegment[] {
  const segs: PrintSegment[] = []
  let x = 0, y = 0, z = 0, e = 0, hasPos = false
  let isSupport = false

  for (const raw of gcode.split('\n')) {
    const trimmed = raw.trim()
    if (trimmed.startsWith(';TYPE:')) {
      isSupport = trimmed.slice(6).trim().toUpperCase().startsWith('SUPPORT')
      continue
    }
    const line = trimmed.split(';')[0].trim()
    if (!line) continue
    const up = line.toUpperCase()
    if (up.startsWith('G92')) {
      const xr = up.match(/X([+-]?[\d.]+)/); if (xr) x = parseFloat(xr[1])
      const yr = up.match(/Y([+-]?[\d.]+)/); if (yr) y = parseFloat(yr[1])
      const zr = up.match(/Z([+-]?[\d.]+)/); if (zr) z = parseFloat(zr[1])
      const er = up.match(/E([+-]?[\d.]+)/); if (er) e = parseFloat(er[1])
      continue
    }
    if (!up.startsWith('G0') && !up.startsWith('G1')) continue

    const xm = up.match(/X([+-]?[\d.]+)/), ym = up.match(/Y([+-]?[\d.]+)/)
    const zm = up.match(/Z([+-]?[\d.]+)/), em = up.match(/E([+-]?[\d.]+)/)
    const nx = xm ? parseFloat(xm[1]) : x
    const ny = ym ? parseFloat(ym[1]) : y
    const nz = zm ? parseFloat(zm[1]) : z
    const ne = em ? parseFloat(em[1]) : e

    if (hasPos && em && ne > e)
      segs.push({ x0: x, y0: z, z0: y, x1: nx, y1: nz, z1: ny, isSupport })

    x = nx; y = ny; z = nz; e = ne; hasPos = true
  }
  return segs
}

// ── Build InstancedMesh for print segments ────────────────────────────────────

function buildPrintMesh(segs: PrintSegment[], lineWidth: number, scene: THREE.Scene): [THREE.InstancedMesh | null, THREE.InstancedMesh | null] {
  const BEAD_SEGS = 5
  const cylGeo = new THREE.CylinderGeometry(lineWidth / 2, lineWidth / 2, 1, BEAD_SEGS)
  const yAxis = new THREE.Vector3(0, 1, 0)
  const dummy = new THREE.Object3D()
  const dir = new THREE.Vector3()

  function makeMesh(filtered: PrintSegment[], color: number): THREE.InstancedMesh | null {
    if (filtered.length === 0) return null
    const mat = new THREE.MeshPhongMaterial({ color, transparent: true, opacity: 0.55, specular: 0x111111 })
    const mesh = new THREE.InstancedMesh(cylGeo, mat, filtered.length)
    mesh.frustumCulled = false
    for (let i = 0; i < filtered.length; i++) {
      const { x0, y0, z0, x1, y1, z1 } = filtered[i]
      const dx = x1-x0, dy = y1-y0, dz = z1-z0
      const len = Math.sqrt(dx*dx + dy*dy + dz*dz)
      dummy.position.set((x0+x1)/2, (y0+y1)/2, (z0+z1)/2)
      if (len > 0.001) { dir.set(dx, dy, dz).normalize(); dummy.quaternion.setFromUnitVectors(yAxis, dir) } else dummy.quaternion.identity()
      dummy.scale.set(1, Math.max(len, 0.001), 1)
      dummy.updateMatrix()
      mesh.setMatrixAt(i, dummy.matrix)
    }
    mesh.instanceMatrix.needsUpdate = true
    scene.add(mesh)
    return mesh
  }

  return [
    makeMesh(segs.filter(s => !s.isSupport), 0x1e40af),
    makeMesh(segs.filter(s =>  s.isSupport), 0xf97316),
  ]
}

// ── Format seconds ────────────────────────────────────────────────────────────

function fmtTime(sec: number): string {
  if (sec < 60)   return `${Math.round(sec)}s`
  if (sec < 3600) return `${Math.floor(sec/60)}m ${Math.round(sec%60)}s`
  return `${Math.floor(sec/3600)}h ${Math.floor((sec%3600)/60)}m`
}

// ── Main component ────────────────────────────────────────────────────────────

const SPEEDS = [1, 2, 5, 10, 20, 50]

const LEGEND_ITEMS: { key: VisKey; label: string; color: string }[] = [
  { key: 'rapid',        label: 'Rapid (G0)', color: 'bg-yellow-400' },
  { key: 'cut',          label: 'Cut (G1)',   color: 'bg-cyan-400'   },
  { key: 'printed',      label: 'Printed',    color: 'bg-blue-600'   },
  { key: 'support',      label: 'Support',    color: 'bg-orange-500' },
  { key: 'unmachinable', label: 'Unmachinable', color: 'bg-red-600'  },
]

export default function CncSimulation({
  toolpathGCode, printGCode, buildVolume, toolDiameterMm = 3, bedPositionX = 0, bedPositionY = 0, originX = 0, originY = 0,
  unmachinableRegions = [], className,
}: Props) {
  const mountRef = useRef<HTMLDivElement>(null)

  const [playing,   setPlaying]   = useState(false)
  const [speed,     setSpeed]     = useState(4)
  const [progress,  setProgress]  = useState(0)
  const [ended,     setEnded]     = useState(false)
  const [vis, setVis] = useState<Record<VisKey, boolean>>({
    rapid: true, cut: true, printed: true, support: true, unmachinable: true,
  })

  const stateRef = useRef({ playing: false, speed: SPEEDS[4], moveIdx: 0, segProg: 0, lastTs: 0 })
  const rafRef   = useRef(0)

  // Three.js object refs
  const rendererRef    = useRef<THREE.WebGLRenderer | null>(null)
  const sceneRef       = useRef<THREE.Scene | null>(null)
  const cameraRef      = useRef<THREE.PerspectiveCamera | null>(null)
  const controlsRef    = useRef<OrbitControls | null>(null)
  const toolGroupRef   = useRef<THREE.Group | null>(null)
  const rapidLinesRef  = useRef<THREE.LineSegments | null>(null)
  const cutLinesRef    = useRef<THREE.LineSegments | null>(null)
  const cursorGeoRef   = useRef<THREE.BufferGeometry | null>(null)
  const printedMeshRef = useRef<THREE.InstancedMesh | null>(null)
  const supportMeshRef = useRef<THREE.InstancedMesh | null>(null)
  const unmachGroupRef = useRef<THREE.Group | null>(null)
  const movesRef       = useRef<SimMove[]>([])

  // Cumulative rapid/cut counts per move index (for per-type draw range)
  const rapidCumRef = useRef<Int32Array>(new Int32Array(0))
  const cutCumRef   = useRef<Int32Array>(new Int32Array(0))

  const { moves, estimatedMachiningSec } = useMemo(() => parseCncMoves(toolpathGCode), [toolpathGCode])
  const printSegs = useMemo(() => parsePrintSegments(printGCode), [printGCode])
  const setupEstimateSec = useMemo(() => Math.max(1, Math.ceil((moves.length + printSegs.length) / 8000)), [moves.length, printSegs.length])

  // Max Z height across all moves and print segments (for slice slider range)
  const maxSceneZ = useMemo(() => {
    let max = 0
    for (const m of moves)    { max = Math.max(max, m.ty0, m.ty1) }
    for (const s of printSegs){ max = Math.max(max, s.y0,  s.y1)  }
    return max || 1
  }, [moves, printSegs])

  const [sliceZ, setSliceZ] = useState(maxSceneZ)

  // Reset sliceZ when a new toolpath is loaded
  useEffect(() => { setSliceZ(maxSceneZ) }, [maxSceneZ])

  // ── Visibility toggle effects ───────────────────────────────────────────────
  useEffect(() => { if (rapidLinesRef.current)  rapidLinesRef.current.visible  = vis.rapid        }, [vis.rapid])
  useEffect(() => { if (cutLinesRef.current)    cutLinesRef.current.visible    = vis.cut          }, [vis.cut])
  useEffect(() => { if (printedMeshRef.current) printedMeshRef.current.visible = vis.printed      }, [vis.printed])
  useEffect(() => { if (supportMeshRef.current) supportMeshRef.current.visible = vis.support      }, [vis.support])
  useEffect(() => { if (unmachGroupRef.current) unmachGroupRef.current.visible = vis.unmachinable }, [vis.unmachinable])

  // ── Build Three.js scene ────────────────────────────────────────────────────
  useEffect(() => {
    const el = mountRef.current
    if (!el) return

    cancelAnimationFrame(rafRef.current)
    controlsRef.current?.dispose()
    rendererRef.current?.dispose()
    if (rendererRef.current?.domElement && el.contains(rendererRef.current.domElement))
      el.removeChild(rendererRef.current.domElement)

    stateRef.current = { playing: false, speed: SPEEDS[4], moveIdx: 0, segProg: 0, lastTs: 0 }
    setPlaying(false); setProgress(0); setEnded(false)
    movesRef.current = moves

    const w = Math.max(el.clientWidth, 1)
    const h = Math.max(el.clientHeight, 1)

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.setSize(w, h)
    renderer.setClearColor(0x060a10)
    el.appendChild(renderer.domElement)
    rendererRef.current = renderer

    const scene = new THREE.Scene()
    scene.add(new THREE.AmbientLight(0xffffff, 0.6))
    const sun = new THREE.DirectionalLight(0xffffff, 1.0)
    sun.position.set(2, 3, 2)
    scene.add(sun)
    sceneRef.current = scene

    const camera = new THREE.PerspectiveCamera(50, w / h, 0.1, 20000)
    cameraRef.current = camera

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.1
    controlsRef.current = controls

    // ── Bed — positioned in machine space ──────────────────────────────
    const bw = buildVolume.width, bd = buildVolume.depth
    // Bed center in machine coordinates (matches G-code frame after translation)
    const bcx = bedPositionX + bw / 2 - originX
    const bcz = bedPositionY + bd / 2 - originY
    const hw = bw / 2, hd = bd / 2
    const bedOutline = new THREE.LineSegments(
      new THREE.BufferGeometry().setFromPoints([
        new THREE.Vector3(-hw,0,-hd), new THREE.Vector3(hw,0,-hd),
        new THREE.Vector3(hw,0,-hd),  new THREE.Vector3(hw,0,hd),
        new THREE.Vector3(hw,0,hd),   new THREE.Vector3(-hw,0,hd),
        new THREE.Vector3(-hw,0,hd),  new THREE.Vector3(-hw,0,-hd),
      ]),
      new THREE.LineBasicMaterial({ color: 0x1e293b }),
    )
    bedOutline.position.set(bcx, 0, bcz)
    scene.add(bedOutline)
    const bedMesh = new THREE.Mesh(
      new THREE.PlaneGeometry(bw, bd),
      new THREE.MeshStandardMaterial({ color: 0x0f172a, transparent: true, opacity: 0.4, side: THREE.DoubleSide }),
    )
    bedMesh.rotation.x = -Math.PI / 2
    bedMesh.position.set(bcx, 0, bcz)
    scene.add(bedMesh)

    // ── Printed part (static) ─────────────────────────────────────────────
    if (printSegs.length > 0) {
      const [pm, sm] = buildPrintMesh(printSegs, 0.4, scene)
      printedMeshRef.current = pm
      supportMeshRef.current = sm
      if (pm) pm.visible = vis.printed
      if (sm) sm.visible = vis.support
    }

    // ── Unmachinable regions (semi-transparent red boxes) ─────────────────
    const unmachGroup = new THREE.Group()
    unmachGroupRef.current = unmachGroup
    if (unmachinableRegions.length > 0) {
      const unmachMat = new THREE.MeshBasicMaterial({
        color: 0xff2222, transparent: true, opacity: 0.28,
        side: THREE.DoubleSide, depthWrite: false,
      })
      const unmachEdgeMat = new THREE.LineBasicMaterial({ color: 0xff4444 })
      for (const r of unmachinableRegions) {
        const { minX: bx, minY: by, maxX: bX, maxY: bY } = r.bounds
        if (Math.abs(bX - bx) < 0.001 && Math.abs(bY - by) < 0.001) continue
        const rw = Math.abs(bX - bx) || 1
        const rd = Math.abs(bY - by) || 1
        const boxGeo  = new THREE.BoxGeometry(rw, 1, rd)
        const boxMesh = new THREE.Mesh(boxGeo, unmachMat)
        boxMesh.position.set((bx + bX) / 2, r.zHeightMm, (by + bY) / 2)
        unmachGroup.add(boxMesh)
        const edgeLines = new THREE.LineSegments(new THREE.EdgesGeometry(boxGeo), unmachEdgeMat)
        edgeLines.position.copy(boxMesh.position)
        unmachGroup.add(edgeLines)
      }
    }
    unmachGroup.visible = vis.unmachinable
    scene.add(unmachGroup)

    // ── Toolpath traces — separate rapid (yellow) and cut (cyan) ─────────
    const numMoves = moves.length
    if (numMoves > 0) {
      // Precompute cumulative rapid/cut counts per move index
      const rapidCum = new Int32Array(numMoves + 1)
      const cutCum   = new Int32Array(numMoves + 1)
      let rCount = 0, cCount = 0
      for (let i = 0; i < numMoves; i++) {
        rapidCum[i] = rCount; cutCum[i] = cCount
        if (moves[i].isRapid) rCount++; else cCount++
      }
      rapidCum[numMoves] = rCount; cutCum[numMoves] = cCount
      rapidCumRef.current = rapidCum
      cutCumRef.current   = cutCum

      // Rapid lines (yellow)
      const rapidPos = new Float32Array(rCount * 6)
      let ri = 0
      for (const m of moves) {
        if (!m.isRapid) continue
        rapidPos[ri*6]=m.tx0; rapidPos[ri*6+1]=m.ty0; rapidPos[ri*6+2]=m.tz0
        rapidPos[ri*6+3]=m.tx1; rapidPos[ri*6+4]=m.ty1; rapidPos[ri*6+5]=m.tz1
        ri++
      }
      const rapidGeo = new THREE.BufferGeometry()
      rapidGeo.setAttribute('position', new THREE.BufferAttribute(rapidPos, 3))
      rapidGeo.setDrawRange(0, 0)
      const rapidLines = new THREE.LineSegments(rapidGeo, new THREE.LineBasicMaterial({ color: 0xfacc15 }))
      rapidLines.visible = vis.rapid
      scene.add(rapidLines)
      rapidLinesRef.current = rapidLines

      // Cut lines (cyan)
      const cutPos = new Float32Array(cCount * 6)
      let ci = 0
      for (const m of moves) {
        if (m.isRapid) continue
        cutPos[ci*6]=m.tx0; cutPos[ci*6+1]=m.ty0; cutPos[ci*6+2]=m.tz0
        cutPos[ci*6+3]=m.tx1; cutPos[ci*6+4]=m.ty1; cutPos[ci*6+5]=m.tz1
        ci++
      }
      const cutGeo = new THREE.BufferGeometry()
      cutGeo.setAttribute('position', new THREE.BufferAttribute(cutPos, 3))
      cutGeo.setDrawRange(0, 0)
      const cutLines = new THREE.LineSegments(cutGeo, new THREE.LineBasicMaterial({ color: 0x22d3ee }))
      cutLines.visible = vis.cut
      scene.add(cutLines)
      cutLinesRef.current = cutLines

      // Cursor line (white, current in-progress segment)
      const cursorGeo = new THREE.BufferGeometry()
      cursorGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array([0,0,0,0,0,0]), 3))
      cursorGeoRef.current = cursorGeo
      scene.add(new THREE.Line(cursorGeo, new THREE.LineBasicMaterial({ color: 0xffffff })))
    }

    // ── CNC Tool ──────────────────────────────────────────────────────────
    const toolRadius = toolDiameterMm / 2
    const shaftH = Math.max(toolDiameterMm * 7, 20)
    const coneH  = toolRadius * 1.5
    const toolGroup = new THREE.Group()
    const shaftMesh = new THREE.Mesh(
      new THREE.CylinderGeometry(toolRadius, toolRadius, shaftH, 10),
      new THREE.MeshPhongMaterial({ color: 0xd1d5db, specular: 0x888888, shininess: 120 }),
    )
    shaftMesh.position.y = shaftH / 2
    toolGroup.add(shaftMesh)
    const coneMesh = new THREE.Mesh(
      new THREE.ConeGeometry(toolRadius, coneH, 10),
      new THREE.MeshPhongMaterial({ color: 0xfbbf24, specular: 0xffffff, shininess: 200 }),
    )
    coneMesh.position.y = -coneH / 2
    coneMesh.rotation.z = Math.PI
    toolGroup.add(coneMesh)
    if (moves.length > 0) toolGroup.position.set(moves[0].tx0, moves[0].ty0, moves[0].tz0)
    scene.add(toolGroup)
    toolGroupRef.current = toolGroup

    // ── Camera ────────────────────────────────────────────────────────────
    const box = new THREE.Box3()
    for (const m of moves) {
      box.expandByPoint(new THREE.Vector3(m.tx0, m.ty0, m.tz0))
      box.expandByPoint(new THREE.Vector3(m.tx1, m.ty1, m.tz1))
    }
    for (const s of printSegs) {
      box.expandByPoint(new THREE.Vector3(s.x0, s.y0, s.z0))
      box.expandByPoint(new THREE.Vector3(s.x1, s.y1, s.z1))
    }
    const hasData = moves.length > 0 || printSegs.length > 0
    const center = hasData ? box.getCenter(new THREE.Vector3()) : new THREE.Vector3(0, 0, 0)
    const span   = hasData ? box.getSize(new THREE.Vector3()) : new THREE.Vector3(bw, 100, bd)
    const dist   = Math.max(span.x, span.y, span.z, bw * 0.5) * 1.8
    camera.position.set(center.x - dist * 0.3, center.y + dist * 0.7, center.z + dist)
    camera.lookAt(center)
    controls.target.copy(center)
    controls.update()

    // ── Render loop ───────────────────────────────────────────────────────
    let animId: number
    const tick = () => { animId = requestAnimationFrame(tick); controls.update(); renderer.render(scene, camera) }
    tick()

    const ro = new ResizeObserver(() => {
      const nw = el.clientWidth, nh = el.clientHeight
      if (nw > 0 && nh > 0) { camera.aspect = nw/nh; camera.updateProjectionMatrix(); renderer.setSize(nw, nh) }
    })
    ro.observe(el)

    return () => {
      cancelAnimationFrame(animId); cancelAnimationFrame(rafRef.current)
      ro.disconnect(); controls.dispose()
      // Dispose all Three.js geometries and materials to prevent GPU memory leaks
      scene.traverse(obj => {
        if (obj instanceof THREE.Mesh || obj instanceof THREE.LineSegments) {
          obj.geometry?.dispose()
          if (obj.material instanceof THREE.Material) obj.material.dispose()
          else if (Array.isArray(obj.material)) obj.material.forEach(m => m.dispose())
        }
      })
      renderer.dispose()
      if (el.contains(renderer.domElement)) el.removeChild(renderer.domElement)
      sceneRef.current = null; rendererRef.current = null
      toolGroupRef.current = null; rapidLinesRef.current = null
      cutLinesRef.current = null; cursorGeoRef.current = null
      printedMeshRef.current = null; supportMeshRef.current = null
      unmachGroupRef.current = null
    }
  }, [toolpathGCode, printGCode, buildVolume, toolDiameterMm, unmachinableRegions]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => { stateRef.current.speed = SPEEDS[speed] }, [speed])

  // ── Animation tick ──────────────────────────────────────────────────────────
  const tickFnRef = useRef<(ts: number) => void>(() => {})
  tickFnRef.current = (timestamp: number) => {
    const state = stateRef.current
    if (!state.playing) return

    const dt = Math.min((timestamp - state.lastTs) / 1000, 0.05)
    state.lastTs = timestamp

    const allMoves = movesRef.current
    if (state.moveIdx >= allMoves.length) {
      state.playing = false; setPlaying(false); setEnded(true); setProgress(1)
      return
    }

    const move = allMoves[state.moveIdx]
    const moveDur = Math.max(move.durationSec / state.speed, 0.0001)
    state.segProg += dt / moveDur

    while (state.segProg >= 1 && state.moveIdx < allMoves.length - 1) {
      state.segProg -= 1; state.moveIdx++
    }
    if (state.moveIdx >= allMoves.length - 1 && state.segProg >= 1) {
      state.segProg = 1; state.playing = false; setPlaying(false); setEnded(true)
    }

    const m = allMoves[Math.min(state.moveIdx, allMoves.length - 1)]
    const t = Math.min(state.segProg, 1)
    const cx = m.tx0 + (m.tx1 - m.tx0) * t
    const cy = m.ty0 + (m.ty1 - m.ty0) * t
    const cz = m.tz0 + (m.tz1 - m.tz0) * t

    if (toolGroupRef.current) toolGroupRef.current.position.set(cx, cy, cz)

    // Update per-type draw ranges
    const idx = state.moveIdx
    const rCum = rapidCumRef.current; const cCum = cutCumRef.current
    if (rapidLinesRef.current && rCum.length > idx) rapidLinesRef.current.geometry.setDrawRange(0, rCum[idx] * 2)
    if (cutLinesRef.current   && cCum.length > idx) cutLinesRef.current.geometry.setDrawRange(0, cCum[idx] * 2)

    // Cursor
    const cGeo = cursorGeoRef.current
    if (cGeo) {
      const pos = cGeo.attributes.position as THREE.BufferAttribute
      pos.setXYZ(0, m.tx0, m.ty0, m.tz0); pos.setXYZ(1, cx, cy, cz); pos.needsUpdate = true
    }

    setProgress(idx / Math.max(allMoves.length, 1))
    if (state.playing) rafRef.current = requestAnimationFrame(ts => tickFnRef.current(ts))
  }

  useEffect(() => {
    stateRef.current.playing = playing
    if (playing) {
      stateRef.current.lastTs = performance.now()
      rafRef.current = requestAnimationFrame(ts => tickFnRef.current(ts))
    } else {
      cancelAnimationFrame(rafRef.current)
    }
  }, [playing]) // eslint-disable-line react-hooks/exhaustive-deps

  const handleReset = () => {
    cancelAnimationFrame(rafRef.current)
    stateRef.current = { ...stateRef.current, playing: false, moveIdx: 0, segProg: 0 }
    setPlaying(false); setEnded(false); setProgress(0)
    if (rapidLinesRef.current) rapidLinesRef.current.geometry.setDrawRange(0, 0)
    if (cutLinesRef.current)   cutLinesRef.current.geometry.setDrawRange(0, 0)
    if (cursorGeoRef.current) {
      const pos = cursorGeoRef.current.attributes.position as THREE.BufferAttribute
      pos.setXYZ(0, 0,0,0); pos.setXYZ(1, 0,0,0); pos.needsUpdate = true
    }
    if (toolGroupRef.current && moves.length > 0)
      toolGroupRef.current.position.set(moves[0].tx0, moves[0].ty0, moves[0].tz0)
  }

  // Seek to a specific move index (pauses animation and updates all visual state)
  const seekTo = (idx: number) => {
    cancelAnimationFrame(rafRef.current)
    const clamped = Math.max(0, Math.min(idx, moves.length === 0 ? 0 : moves.length - 1))
    stateRef.current = { ...stateRef.current, playing: false, moveIdx: clamped, segProg: 0 }
    setPlaying(false); setEnded(false)

    const rCum = rapidCumRef.current; const cCum = cutCumRef.current
    if (rapidLinesRef.current && rCum.length > clamped) rapidLinesRef.current.geometry.setDrawRange(0, rCum[clamped] * 2)
    if (cutLinesRef.current   && cCum.length > clamped) cutLinesRef.current.geometry.setDrawRange(0, cCum[clamped] * 2)

    if (toolGroupRef.current && moves.length > 0) {
      const m = moves[clamped]
      toolGroupRef.current.position.set(m.tx0, m.ty0, m.tz0)
    }
    setProgress(moves.length === 0 ? 0 : clamped / moves.length)
  }

  // When sliceZ changes, seek to the last move whose destination Z ≤ sliceZ
  useEffect(() => {
    if (moves.length === 0) return
    let lastIdx = 0
    for (let i = 0; i < moves.length; i++) {
      if (moves[i].ty1 <= sliceZ + 0.01) lastIdx = i
    }
    seekTo(lastIdx)
  }, [sliceZ]) // eslint-disable-line react-hooks/exhaustive-deps

  const toggle = (key: VisKey) => setVis(v => ({ ...v, [key]: !v[key] }))
  const simTimeSec = estimatedMachiningSec / SPEEDS[speed]

  const visibleLegend = LEGEND_ITEMS.filter(
    item => item.key !== 'unmachinable' || unmachinableRegions.length > 0
  )

  return (
    <div className={`flex flex-col gap-3 ${className ?? ''}`}>

      {/* Clickable legend */}
      <div className="bg-gray-900 border border-gray-800 rounded-lg px-3 py-2 flex flex-wrap items-center gap-2">
        <span className="text-xs text-gray-400 font-medium mr-1">Show:</span>
        {visibleLegend.map(({ key, label, color }) => (
          <button
            key={key}
            onClick={() => toggle(key)}
            title={vis[key] ? `Hide ${label}` : `Show ${label}`}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded-full border text-xs transition-all select-none ${
              vis[key]
                ? 'border-gray-600 bg-gray-800 text-gray-200 hover:bg-gray-700'
                : 'border-gray-700 bg-gray-900 text-gray-500 hover:bg-gray-800'
            }`}
          >
            <span className={`inline-block w-2.5 h-2.5 rounded-sm flex-shrink-0 ${color} ${vis[key] ? '' : 'opacity-30'}`} />
            <span className={vis[key] ? '' : 'line-through'}>{label}</span>
            {key === 'unmachinable' && (
              <span className="ml-0.5 text-red-400">({unmachinableRegions.length})</span>
            )}
          </button>
        ))}
        <span className="ml-auto text-xs text-gray-500">
          {moves.length} moves · machining: {fmtTime(estimatedMachiningSec)} · setup ~{setupEstimateSec}s
        </span>
      </div>

      {/* 3D viewport + Z-slice slider overlay */}
      <div className="relative bg-gray-950 rounded-xl border border-gray-700 overflow-hidden flex-1" style={{ minHeight: '55vh' }}>
        <div ref={mountRef} style={{ position: 'absolute', inset: 0 }} />

        {/* Z-layer scrub bar (matches GCodePreview3D pattern) */}
        {maxSceneZ > 0 && (
          <div style={{
            position: 'absolute', bottom: 12, left: 12, right: 12,
            display: 'flex', alignItems: 'center', gap: 8,
          }}>
            <span className="text-white text-xs rounded px-1.5 py-0.5" style={{ background: 'rgba(0,0,0,0.6)', whiteSpace: 'nowrap' }}>
              Z {sliceZ.toFixed(1)} mm
            </span>
            <input
              type="range"
              min={0}
              max={maxSceneZ}
              step={0.1}
              value={sliceZ}
              onChange={e => setSliceZ(parseFloat(e.target.value))}
              className="flex-1 accent-cyan-400"
            />
            <span className="text-white text-xs rounded px-1.5 py-0.5" style={{ background: 'rgba(0,0,0,0.6)', whiteSpace: 'nowrap' }}>
              {maxSceneZ.toFixed(1)} mm
            </span>
          </div>
        )}
      </div>

      {/* Playback controls */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 flex flex-col gap-3">
        <div className="relative h-2 bg-gray-800 rounded-full overflow-hidden">
          <div className="absolute left-0 top-0 h-full bg-green-500 transition-none rounded-full" style={{ width: `${progress * 100}%` }} />
        </div>

        <div className="flex items-center gap-3 flex-wrap">
          <button
            onClick={() => { if (ended) handleReset(); setPlaying(p => !p) }}
            className="flex items-center gap-2 px-5 py-2 rounded-lg bg-green-700 hover:bg-green-600 text-white text-sm font-medium transition"
          >
            {ended ? '↺ Restart' : playing ? '⏸ Pause' : '▶ Play'}
          </button>

          <button
            onClick={handleReset}
            disabled={progress === 0 && !playing}
            className="px-4 py-2 rounded-lg bg-gray-700 hover:bg-gray-600 disabled:opacity-40 text-gray-200 text-sm transition"
          >
            ↺ Reset
          </button>

          <div className="flex items-center gap-2 ml-auto">
            <span className="text-xs text-gray-400">Speed</span>
            <div className="flex gap-1">
              {SPEEDS.map((s, i) => (
                <button
                  key={s}
                  onClick={() => setSpeed(i)}
                  className={`px-2.5 py-1 rounded text-xs transition ${speed === i ? 'bg-primary text-white' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'}`}
                >
                  {s}x
                </button>
              ))}
            </div>
          </div>

          <span className="text-xs text-gray-500 ml-2">sim: {fmtTime(simTimeSec)}</span>
        </div>

        {ended && <p className="text-xs text-green-400 text-center">Simulation complete — toolpath fully traced.</p>}
      </div>
    </div>
  )
}
