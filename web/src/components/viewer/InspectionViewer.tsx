/**
 * InspectionViewer — 3D deviation heatmap on real mesh.
 *
 * COMPUTE FIREWALL: deviation values + positions come from the core.
 * Colors are a display-only transform. No measurement math.
 *
 * COLORING: spatial nearest-neighbor from core's point_positions +
 * point_deviations. Each STL vertex (after transform) finds the closest
 * deviation sample by 3D distance and uses its color. Produces
 * spatially-correct heatmaps matching GOM / PolyWorks / Geomagic.
 */
import { useEffect, useRef } from 'react'
import * as THREE from 'three'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import type { InspectResponse } from '../../api/alignmesh-types.generated'
import type { ImportedPart } from '../inspection/ImportPanel'
import { deviationToMetrologyColor, COLOR_NO_DATA } from '../inspection/deviation-colormap'

export type OverlayMode = 'reference' | 'measured' | 'overlay'

interface Props {
  result: InspectResponse
  reference?: ImportedPart | null
  measured?: ImportedPart | null
  overlayMode?: OverlayMode
  rpsPoints?: { x: number; y: number; z: number }[]
  rpsProjectedPoints?: { x: number; y: number; z: number; valid: boolean }[]
  showRpsNominal?: boolean
  showRpsProjected?: boolean
  className?: string
}

/** Simple grid-based spatial index for fast nearest-deviation lookup. */
function buildSpatialIndex(positions: number[], deviations: number[], cellSize: number) {
  const n = Math.min(Math.floor(positions.length / 3), deviations.length)
  const cells = new Map<string, { x: number; y: number; z: number; dev: number }[]>()

  for (let i = 0; i < n; i++) {
    const x = positions[i * 3], y = positions[i * 3 + 1], z = positions[i * 3 + 2]
    const cx = Math.floor(x / cellSize), cy = Math.floor(y / cellSize), cz = Math.floor(z / cellSize)
    const key = `${cx},${cy},${cz}`
    if (!cells.has(key)) cells.set(key, [])
    cells.get(key)!.push({ x, y, z, dev: deviations[i] })
  }

  return (qx: number, qy: number, qz: number): number => {
    const cx = Math.floor(qx / cellSize), cy = Math.floor(qy / cellSize), cz = Math.floor(qz / cellSize)
    let bestDsq = Infinity, bestDev = NaN

    // Search 3x3x3 neighborhood of cells
    for (let dx = -1; dx <= 1; dx++)
      for (let dy = -1; dy <= 1; dy++)
        for (let dz = -1; dz <= 1; dz++) {
          const pts = cells.get(`${cx + dx},${cy + dy},${cz + dz}`)
          if (!pts) continue
          for (const p of pts) {
            const dsq = (qx - p.x) ** 2 + (qy - p.y) ** 2 + (qz - p.z) ** 2
            if (dsq < bestDsq) { bestDsq = dsq; bestDev = p.dev }
          }
        }
    return bestDev
  }
}

export default function InspectionViewer({
  result, reference, measured, overlayMode = 'overlay', rpsPoints, rpsProjectedPoints,
  showRpsNominal = true, showRpsProjected = true, className = '',
}: Props) {
  const mountRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const mount = mountRef.current
    if (!mount || !result.valid) return

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x0a0f1a)

    const w = mount.clientWidth, h = mount.clientHeight || 500
    const camera = new THREE.PerspectiveCamera(40, w / h, 0.1, 50000)

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setSize(w, h)
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.toneMapping = THREE.ACESFilmicToneMapping
    renderer.toneMappingExposure = 1.1
    mount.appendChild(renderer.domElement)

    // Lighting: ambient hemisphere + camera-attached key light so the
    // illuminated side always faces the user when orbiting/zooming.
    scene.add(new THREE.HemisphereLight(0xeeeeff, 0x445566, 0.6))
    scene.add(new THREE.AmbientLight(0x444444, 0.3))
    const camLight = new THREE.DirectionalLight(0xffffff, 0.8)
    camera.add(camLight)       // moves with the camera
    camLight.position.set(0, 0, 1)  // shines forward from camera
    scene.add(camera)          // camera must be in scene for child lights
    // Subtle fill from below for depth
    const fill = new THREE.DirectionalLight(0x8899aa, 0.25)
    fill.position.set(-1, -1, -1)
    scene.add(fill)

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.06

    const loader = new STLLoader()
    const box = new THREE.Box3()
    let refMesh: THREE.Mesh | null = null

    // Build transform matrix
    const tmat = new THREE.Matrix4()
    const m = result.transform_matrix
    if (m?.length === 16) {
      tmat.set(m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7], m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15])
    }

    // Load reference mesh (translucent blue in overlay mode)
    if (reference?.fileData) {
      try {
        const geo = loader.parse(reference.fileData)
        geo.computeVertexNormals()
        refMesh = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({
          color: 0x3b82f6, transparent: true,
          opacity: overlayMode === 'overlay' ? 0.2 : 0.7,
          side: THREE.DoubleSide, roughness: 0.6, metalness: 0.1,
          depthWrite: overlayMode !== 'overlay',
        }))
        scene.add(refMesh)
        box.expandByObject(refMesh)
      } catch { /* skip */ }
    }

    // Load measured mesh with SPATIAL deviation heatmap
    if (measured?.fileData) {
      try {
        const geo = loader.parse(measured.fileData)
        geo.computeVertexNormals()

        const devs = result.point_deviations ?? []
        const positions = (result as any).point_positions as number[] | undefined
        const tolerance = result.tolerance_mm
        const rangeMax = Math.abs(result.heatmap_max)
        const vCount = geo.attributes.position.count
        const hasDevs = devs.length > 0

        if (hasDevs) {
          const colors = new Float32Array(vCount * 3)

          if (positions && positions.length >= devs.length * 3) {
            // SPATIAL nearest-neighbor: build grid index from core positions.
            // Color PER TRIANGLE (centroid lookup) — 3× faster than per-vertex,
            // and each triangle's 3 vertices get the same color (no seam artifacts).
            const bbox2 = new THREE.Box3()
            for (let i = 0; i < devs.length; i++) {
              bbox2.expandByPoint(new THREE.Vector3(positions[i*3], positions[i*3+1], positions[i*3+2]))
            }
            const bsize2 = new THREE.Vector3()
            bbox2.getSize(bsize2)
            const cellSize = Math.max(bsize2.x, bsize2.y, bsize2.z) / 50

            const lookup = buildSpatialIndex(positions, devs, cellSize)
            const posArr = geo.attributes.position.array as Float32Array
            const triCount = Math.floor(vCount / 3)

            for (let t = 0; t < triCount; t++) {
              const base = t * 3
              // Triangle centroid in local space, then transform
              const cx = (posArr[base*3] + posArr[(base+1)*3] + posArr[(base+2)*3]) / 3
              const cy = (posArr[base*3+1] + posArr[(base+1)*3+1] + posArr[(base+2)*3+1]) / 3
              const cz = (posArr[base*3+2] + posArr[(base+1)*3+2] + posArr[(base+2)*3+2]) / 3
              // Apply transform to centroid
              const tx = tmat.elements[0]*cx + tmat.elements[4]*cy + tmat.elements[8]*cz + tmat.elements[12]
              const ty = tmat.elements[1]*cx + tmat.elements[5]*cy + tmat.elements[9]*cz + tmat.elements[13]
              const tz = tmat.elements[2]*cx + tmat.elements[6]*cy + tmat.elements[10]*cz + tmat.elements[14]

              const dev = lookup(tx, ty, tz)
              const c = isNaN(dev) ? COLOR_NO_DATA : deviationToMetrologyColor(dev, tolerance, rangeMax)
              // Same color for all 3 vertices of this triangle
              for (let v = 0; v < 3; v++) {
                colors[(base+v)*3] = c.r; colors[(base+v)*3+1] = c.g; colors[(base+v)*3+2] = c.b
              }
            }
          } else {
            // Fallback without positions: uniform color
            for (let i = 0; i < vCount; i++) {
              colors[i*3] = 0.5; colors[i*3+1] = 0.5; colors[i*3+2] = 0.5
            }
          }
          geo.setAttribute('color', new THREE.BufferAttribute(colors, 3))
        }

        const measMesh = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({
          vertexColors: hasDevs, color: hasDevs ? 0xffffff : 0x22c55e,
          side: THREE.DoubleSide, roughness: 0.55, metalness: 0.02,
        }))
        measMesh.applyMatrix4(tmat)
        scene.add(measMesh)
        box.expandByObject(measMesh)

        // Visibility
        if (refMesh) refMesh.visible = overlayMode === 'reference' || overlayMode === 'overlay'
        measMesh.visible = overlayMode !== 'reference'
      } catch { /* skip */ }
    }

    // Compute part scale for marker sizing
    const partSize = new THREE.Vector3()
    if (!box.isEmpty()) box.getSize(partSize)
    const maxDim = Math.max(partSize.x, partSize.y, partSize.z, 1)
    const markerRadius = maxDim * 0.012  // ~1.2% of part size
    const ringRadius = maxDim * 0.02

    // ── Min/Max deviation markers ──────────────────────────────────
    const positions = (result as any).point_positions as number[] | undefined
    const devs = result.point_deviations ?? []
    if (positions && positions.length >= devs.length * 3 && devs.length > 0) {
      let maxIdx = 0, minIdx = 0, maxVal = -Infinity, minVal = Infinity
      for (let i = 0; i < devs.length; i++) {
        if (devs[i] > maxVal) { maxVal = devs[i]; maxIdx = i }
        if (devs[i] < minVal) { minVal = devs[i]; minIdx = i }
      }

      const addMarker = (idx: number, color: number) => {
        const px = positions![idx*3], py = positions![idx*3+1], pz = positions![idx*3+2]
        const sphere = new THREE.Mesh(
          new THREE.SphereGeometry(markerRadius, 16, 16),
          new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.9 })
        )
        sphere.position.set(px, py, pz)
        scene.add(sphere)
        const ring = new THREE.Mesh(
          new THREE.TorusGeometry(ringRadius, markerRadius * 0.2, 8, 32),
          new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.5 })
        )
        ring.position.set(px, py, pz)
        scene.add(ring)
      }

      addMarker(maxIdx, 0xff3333)
      addMarker(minIdx, 0x3333ff)
    }

    // ── RPS nominal point markers (red) ────────────────────────────
    if (showRpsNominal && rpsPoints && rpsPoints.length > 0) {
      const rpsDot = maxDim * 0.008
      const rpsWire = maxDim * 0.015
      for (let i = 0; i < rpsPoints.length; i++) {
        const p = rpsPoints[i]
        const sphere = new THREE.Mesh(
          new THREE.SphereGeometry(rpsDot, 12, 12),
          new THREE.MeshBasicMaterial({ color: 0xff2222, depthTest: false })
        )
        sphere.renderOrder = 999
        sphere.position.set(p.x, p.y, p.z)
        scene.add(sphere)
        const diamond = new THREE.Mesh(
          new THREE.OctahedronGeometry(rpsWire, 0),
          new THREE.MeshBasicMaterial({ color: 0xff4444, wireframe: true, transparent: true, opacity: 0.7, depthTest: false })
        )
        diamond.renderOrder = 998
        diamond.position.set(p.x, p.y, p.z)
        scene.add(diamond)
      }
    }

    // ── RPS projected point markers (blue) ──────────────────────────
    if (showRpsProjected && rpsProjectedPoints && rpsProjectedPoints.length > 0) {
      const projDot = maxDim * 0.008
      const projWire = maxDim * 0.015
      for (let i = 0; i < rpsProjectedPoints.length; i++) {
        const p = rpsProjectedPoints[i]
        const sphere = new THREE.Mesh(
          new THREE.SphereGeometry(projDot, 12, 12),
          new THREE.MeshBasicMaterial({ color: 0x2288ff, depthTest: false })
        )
        sphere.renderOrder = 999
        sphere.position.set(p.x, p.y, p.z)
        scene.add(sphere)
        const diamond = new THREE.Mesh(
          new THREE.OctahedronGeometry(projWire, 0),
          new THREE.MeshBasicMaterial({ color: 0x4499ff, wireframe: true, transparent: true, opacity: 0.7, depthTest: false })
        )
        diamond.renderOrder = 998
        diamond.position.set(p.x, p.y, p.z)
        scene.add(diamond)
      }
    }

    // Frame camera
    if (!box.isEmpty()) {
      const center = new THREE.Vector3(), size = new THREE.Vector3()
      box.getCenter(center); box.getSize(size)
      const maxDim = Math.max(size.x, size.y, size.z)
      const dist = maxDim * 2.0
      camera.position.set(center.x + dist*0.5, center.y + dist*0.35, center.z + dist*0.6)
      camera.lookAt(center); controls.target.copy(center)
      camera.near = maxDim * 0.001; camera.far = maxDim * 100
      camera.updateProjectionMatrix(); controls.update()
    }

    let animId = 0
    const animate = () => { animId = requestAnimationFrame(animate); controls.update(); renderer.render(scene, camera) }
    animate()

    const ro = new ResizeObserver(() => {
      if (!mount) return
      camera.aspect = mount.clientWidth / mount.clientHeight
      camera.updateProjectionMatrix()
      renderer.setSize(mount.clientWidth, mount.clientHeight)
    })
    ro.observe(mount)

    return () => {
      cancelAnimationFrame(animId); ro.disconnect(); controls.dispose(); renderer.dispose()
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement)
    }
  }, [result, reference, measured, overlayMode, rpsPoints, rpsProjectedPoints, showRpsNominal, showRpsProjected])

  // ── Legend ─────────────────────────────────────────────────────
  // Must exactly match the deviationToMetrologyColor function:
  // Blue (neg out-of-tol) → Cyan → Green (in-tol) → Yellow → Red (pos out-of-tol)
  // Magenta at extremes (out-of-range)
  const rangeMin = result.heatmap_min ?? -result.tolerance_mm
  const rangeMax = result.heatmap_max ?? result.tolerance_mm
  const tol = result.tolerance_mm
  const tolFrac = tol > 0 && rangeMax > 0 ? Math.min(50, (tol / rangeMax) * 50) : 10

  return (
    <div className="relative">
      <div ref={mountRef} className={`w-full overflow-hidden ${className}`} style={{ height: 500 }} />

      {/* Color legend — smooth diverging blue→green→red, matching deviationToMetrologyColor */}
      <div className="absolute bottom-3 right-3 bg-gray-950/90 backdrop-blur-sm border border-gray-700/50 rounded-xl px-4 py-3 text-xs shadow-xl">
        <div className="text-gray-400 text-[10px] uppercase tracking-wider mb-2">Deviation (mm)</div>

        {/* Smooth gradient bar with tolerance markers */}
        <div className="relative w-52 h-5 rounded-md overflow-hidden border border-gray-700/30">
          <div className="absolute inset-0" style={{
            background: `linear-gradient(to right,
              #ff00ff 0%,
              #0044cc 5%,
              #0066ee 15%,
              #0099cc 30%,
              #00cc88 ${50 - tolFrac}%,
              #00dd44 ${50 - tolFrac * 0.5}%,
              #22ee22 50%,
              #44dd00 ${50 + tolFrac * 0.5}%,
              #88cc00 ${50 + tolFrac}%,
              #cc9900 70%,
              #ee6600 85%,
              #cc0000 95%,
              #ff00ff 100%)`
          }} />
          {/* Center line (zero) */}
          <div className="absolute top-0 bottom-0 left-1/2 w-px bg-white/70" />
          {/* Tolerance boundary markers */}
          <div className="absolute top-0 bottom-0 bg-white/30" style={{ left: `${50 - tolFrac}%`, width: '1px' }} />
          <div className="absolute top-0 bottom-0 bg-white/30" style={{ left: `${50 + tolFrac}%`, width: '1px' }} />
        </div>

        {/* Scale labels */}
        <div className="flex justify-between text-[10px] text-gray-400 mt-1 w-52 font-mono">
          <span>{rangeMin.toFixed(3)}</span>
          <span className="text-gray-300 font-medium">0</span>
          <span>+{rangeMax.toFixed(3)}</span>
        </div>

        {/* Tolerance band label */}
        <div className="text-center text-[9px] text-green-500/70 mt-0.5 font-mono">
          tol: &plusmn;{tol.toFixed(3)} mm
        </div>

        {/* Stats */}
        <div className="mt-2 grid grid-cols-3 gap-2 text-[9px] text-gray-500">
          <div><span className="text-gray-600">RMS</span> <span className="text-gray-300 font-mono">{result.stats.rms.toFixed(4)}</span></div>
          <div><span className="text-gray-600">Max</span> <span className="text-gray-300 font-mono">{result.stats.max.toFixed(4)}</span></div>
          <div><span className="text-gray-600">In-tol</span> <span className="text-gray-300 font-mono">{result.stats.percent_within_tolerance.toFixed(0)}%</span></div>
        </div>

        {/* Marker legend */}
        <div className="mt-1.5 flex gap-3 text-[9px]">
          <span className="flex items-center gap-1"><span className="inline-block w-2 h-2 rounded-full bg-red-500" /> MAX</span>
          <span className="flex items-center gap-1"><span className="inline-block w-2 h-2 rounded-full bg-blue-500" /> MIN</span>
          {rpsPoints && rpsPoints.length > 0 && (
            <span className="flex items-center gap-1"><span className="inline-block w-2 h-2 rounded-full bg-red-500" /> RPS nom</span>
          )}
          {rpsProjectedPoints && rpsProjectedPoints.length > 0 && (
            <span className="flex items-center gap-1"><span className="inline-block w-2 h-2 rounded-full bg-blue-400" /> RPS proj</span>
          )}
        </div>
      </div>

      <div className="absolute top-2 left-2 bg-yellow-950/80 border border-yellow-700/50 rounded-md px-2 py-1 text-[9px] text-yellow-400/80 backdrop-blur-sm">
        CORROBORATING — not authoritative
      </div>
      <div className="absolute top-2 right-2 bg-gray-950/80 border border-gray-700/50 rounded-md px-2 py-1 text-[9px] text-gray-500 backdrop-blur-sm font-mono">
        {result.n_display_points.toLocaleString()} pts
      </div>
    </div>
  )
}
