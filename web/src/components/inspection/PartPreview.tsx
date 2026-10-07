/**
 * PartPreview — renders imported parts in 3D immediately after selection.
 * When isProcessing=true, meshes dissolve into particles that converge.
 *
 * COMPUTE FIREWALL: display only. The file bytes are parsed by Three.js
 * STLLoader for rendering. The core reads the original file independently
 * for analysis at full double precision.
 */
import { useEffect, useRef, useState } from 'react'
import * as THREE from 'three'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import type { ImportedPart } from './ImportPanel'

interface Props {
  reference: ImportedPart | null
  measured: ImportedPart | null
  isProcessing?: boolean
}

const PALETTE = [
  { hex: '#3b82f6', label: 'Blue' },
  { hex: '#6366f1', label: 'Indigo' },
  { hex: '#8b5cf6', label: 'Violet' },
  { hex: '#ec4899', label: 'Pink' },
  { hex: '#ef4444', label: 'Red' },
  { hex: '#f97316', label: 'Orange' },
  { hex: '#eab308', label: 'Yellow' },
  { hex: '#22c55e', label: 'Green' },
  { hex: '#14b8a6', label: 'Teal' },
  { hex: '#06b6d4', label: 'Cyan' },
  { hex: '#f8fafc', label: 'White' },
  { hex: '#94a3b8', label: 'Silver' },
]

function ColorPicker({ color, onChange, side = 'left' }: {
  color: string
  onChange: (hex: string) => void
  side?: 'left' | 'right'
}) {
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!open) return
    const close = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false)
    }
    document.addEventListener('mousedown', close)
    return () => document.removeEventListener('mousedown', close)
  }, [open])

  return (
    <div className="relative" ref={ref}>
      <button
        onClick={() => setOpen(!open)}
        className="w-5 h-5 rounded-md border-2 border-gray-600 hover:border-gray-400 transition cursor-pointer shadow-sm"
        style={{ background: color }}
        title="Change color"
      />
      {open && (
        <div className={`absolute top-7 ${side === 'right' ? 'right-0' : 'left-0'} z-50 bg-gray-900 border border-gray-700 rounded-xl p-2.5 shadow-2xl shadow-black/50`}
          style={{ animation: 'fadeIn 0.15s ease-out' }}>
          <div className="grid grid-cols-6 gap-1.5">
            {PALETTE.map(p => (
              <button
                key={p.hex}
                onClick={() => { onChange(p.hex); setOpen(false) }}
                className={'w-7 h-7 rounded-lg border-2 transition hover:scale-110 ' +
                  (color === p.hex ? 'border-white scale-110' : 'border-transparent hover:border-gray-500')}
                style={{ background: p.hex }}
                title={p.label}
              />
            ))}
          </div>
        </div>
      )}
    </div>
  )
}

const PARTICLE_COUNT = 800

interface ParticleSystem {
  refPoints: THREE.Points
  measPoints: THREE.Points
  refTrails: THREE.Points
  measTrails: THREE.Points
  refPos: Float32Array
  measPos: Float32Array
  refStart: Float32Array
  measStart: Float32Array
  refTarget: Float32Array
  measTarget: Float32Array
  refSpeeds: Float32Array
  measSpeeds: Float32Array
  count: number
  startTime: number
}

interface SceneState {
  scene: THREE.Scene
  camera: THREE.PerspectiveCamera
  renderer: THREE.WebGLRenderer
  controls: OrbitControls
  meshes: THREE.Object3D[]
  refMaterials: THREE.Material[]
  measMaterials: THREE.Material[]
  animId: number
  ro: ResizeObserver
  particleSystem: ParticleSystem | null
  processingActive: boolean
  sceneCenter: THREE.Vector3
  maxDim: number
}

export default function PartPreview({ reference, measured, isProcessing = false }: Props) {
  const mountRef = useRef<HTMLDivElement>(null)
  const sceneRef = useRef<SceneState | null>(null)
  const [refColor, setRefColor] = useState('#3b82f6')
  const [measColor, setMeasColor] = useState('#22c55e')

  // Main scene setup — runs when parts change
  useEffect(() => {
    const mount = mountRef.current
    if (!mount) return
    if (!reference && !measured) return

    // Clean up previous
    if (sceneRef.current) {
      cancelAnimationFrame(sceneRef.current.animId)
      sceneRef.current.ro.disconnect()
      sceneRef.current.controls.dispose()
      sceneRef.current.renderer.dispose()
      if (sceneRef.current.renderer.domElement.parentNode === mount)
        mount.removeChild(sceneRef.current.renderer.domElement)
      sceneRef.current = null
    }

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x0a0f1a)
    scene.fog = new THREE.FogExp2(0x0a0f1a, 0.0003)

    const w = mount.clientWidth
    const h = mount.clientHeight || 500
    const camera = new THREE.PerspectiveCamera(40, w / h, 0.1, 50000)
    camera.position.set(200, 150, 200)

    const renderer = new THREE.WebGLRenderer({ antialias: true })
    renderer.setSize(w, h)
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2))
    renderer.toneMapping = THREE.ACESFilmicToneMapping
    renderer.toneMappingExposure = 1.2
    renderer.shadowMap.enabled = true
    renderer.shadowMap.type = THREE.PCFSoftShadowMap
    mount.appendChild(renderer.domElement)

    // Lighting
    scene.add(new THREE.HemisphereLight(0x4466aa, 0x222233, 0.6))
    const key = new THREE.DirectionalLight(0xffffff, 1.2)
    key.position.set(3, 5, 4)
    key.castShadow = true
    key.shadow.mapSize.set(1024, 1024)
    scene.add(key)
    scene.add(new THREE.DirectionalLight(0x8899cc, 0.4).translateX(-3).translateY(2).translateZ(-2))
    scene.add(new THREE.DirectionalLight(0x6688ff, 0.3).translateY(-1).translateZ(-4))

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = true
    controls.dampingFactor = 0.06
    controls.rotateSpeed = 0.8
    controls.zoomSpeed = 1.2

    // Ground plane (no grid)
    const groundGeo = new THREE.PlaneGeometry(2000, 2000)
    const ground = new THREE.Mesh(groundGeo, new THREE.MeshStandardMaterial({
      color: 0x0d1117, roughness: 0.95, metalness: 0.05,
    }))
    ground.rotation.x = -Math.PI / 2
    ground.position.y = -0.5
    ground.receiveShadow = true
    scene.add(ground)

    // Axis indicator
    const axisLen = 30
    const makeAxis = (dir: THREE.Vector3, color: number) => {
      const pts = [new THREE.Vector3(), dir.clone().multiplyScalar(axisLen)]
      return new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(pts),
        new THREE.LineBasicMaterial({ color, linewidth: 2 }),
      )
    }
    const axisGroup = new THREE.Group()
    axisGroup.add(makeAxis(new THREE.Vector3(1, 0, 0), 0xff4444))
    axisGroup.add(makeAxis(new THREE.Vector3(0, 1, 0), 0x44ff44))
    axisGroup.add(makeAxis(new THREE.Vector3(0, 0, 1), 0x4488ff))
    scene.add(axisGroup)

    // Load meshes
    const loader = new STLLoader()
    const box = new THREE.Box3()
    const meshes: THREE.Object3D[] = []
    const refMaterials: THREE.Material[] = []
    const measMaterials: THREE.Material[] = []

    // Store sampled vertices for particle animation
    const refVertices: number[] = []
    const measVertices: number[] = []

    const loadPart = (part: ImportedPart, color: string, isRef: boolean, vertOut: number[], matStore: THREE.Material[]) => {
      try {
        const geometry = loader.parse(part.fileData)
        geometry.computeVertexNormals()
        const material = new THREE.MeshPhysicalMaterial({
          color, metalness: 0.15, roughness: 0.45,
          clearcoat: 0.3, clearcoatRoughness: 0.4,
          transparent: true, opacity: isRef ? 0.7 : 0.85,
          side: THREE.DoubleSide,
        })
        const mesh = new THREE.Mesh(geometry, material)
        mesh.castShadow = true
        mesh.receiveShadow = true
        scene.add(mesh)
        meshes.push(mesh)
        matStore.push(material)

        const wireMat = new THREE.MeshBasicMaterial({
          color, wireframe: true, transparent: true, opacity: 0.04,
        })
        const wire = new THREE.Mesh(geometry, wireMat)
        scene.add(wire)
        meshes.push(wire)
        matStore.push(wireMat)

        box.expandByObject(mesh)

        // Sample vertices for particle animation
        const pos = geometry.attributes.position.array as Float32Array
        const vertCount = pos.length / 3
        const step = Math.max(1, Math.floor(vertCount / PARTICLE_COUNT))
        for (let i = 0; i < vertCount && vertOut.length < PARTICLE_COUNT * 3; i += step) {
          vertOut.push(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2])
        }
      } catch {
        const geo = new THREE.IcosahedronGeometry(20, 2)
        const mat = new THREE.MeshPhysicalMaterial({ color, wireframe: true, opacity: 0.5, transparent: true })
        const m = new THREE.Mesh(geo, mat)
        scene.add(m)
        meshes.push(m)
        matStore.push(mat)
      }
    }

    if (reference) loadPart(reference, refColor, true, refVertices, refMaterials)
    if (measured) loadPart(measured, measColor, false, measVertices, measMaterials)

    // Ambient particles
    const ambientCount = 300
    const ambGeo = new THREE.BufferGeometry()
    const ambPos = new Float32Array(ambientCount * 3)
    for (let i = 0; i < ambientCount; i++) {
      ambPos[i * 3] = (Math.random() - 0.5) * 800
      ambPos[i * 3 + 1] = Math.random() * 400
      ambPos[i * 3 + 2] = (Math.random() - 0.5) * 800
    }
    ambGeo.setAttribute('position', new THREE.BufferAttribute(ambPos, 3))
    const ambientParticles = new THREE.Points(ambGeo, new THREE.PointsMaterial({
      color: 0x3366aa, size: 1.5, transparent: true, opacity: 0.3, sizeAttenuation: true,
    }))
    scene.add(ambientParticles)

    // Frame camera
    let sceneCenter = new THREE.Vector3()
    let maxDim = 200
    if (!box.isEmpty()) {
      const size = new THREE.Vector3()
      box.getCenter(sceneCenter)
      box.getSize(size)
      maxDim = Math.max(size.x, size.y, size.z)
      const dist = maxDim * 2.0
      camera.position.set(sceneCenter.x + dist * 0.5, sceneCenter.y + dist * 0.35, sceneCenter.z + dist * 0.6)
      camera.lookAt(sceneCenter)
      controls.target.copy(sceneCenter)
      camera.near = maxDim * 0.001
      camera.far = maxDim * 100
      camera.updateProjectionMatrix()

      ground.position.y = box.min.y - maxDim * 0.01
      axisGroup.position.set(box.min.x - maxDim * 0.15, ground.position.y + 0.2, box.max.z + maxDim * 0.15)

      key.shadow.camera.left = -maxDim
      key.shadow.camera.right = maxDim
      key.shadow.camera.top = maxDim
      key.shadow.camera.bottom = -maxDim
      key.shadow.camera.updateProjectionMatrix()
      key.position.set(sceneCenter.x + maxDim, sceneCenter.y + maxDim * 2, sceneCenter.z + maxDim)
      key.target.position.copy(sceneCenter)
      scene.add(key.target)
      controls.update()
    }

    // Pre-build particle systems (hidden until processing starts)
    let particleSystem: ParticleSystem | null = null

    const count = Math.min(PARTICLE_COUNT, Math.min(refVertices.length / 3, measVertices.length / 3))
    if (count > 10) {
      const separation = maxDim * 0.6

      const buildCloud = (verts: number[], n: number, color: number, offsetDir: number) => {
        const geo = new THREE.BufferGeometry()
        const pos = new Float32Array(n * 3)
        const startPos = new Float32Array(n * 3)
        const targetPos = new Float32Array(n * 3)
        const speeds = new Float32Array(n)

        for (let i = 0; i < n; i++) {
          const x = verts[i * 3] - sceneCenter.x
          const y = verts[i * 3 + 1] - sceneCenter.y
          const z = verts[i * 3 + 2] - sceneCenter.z
          startPos[i * 3] = sceneCenter.x + x + offsetDir * separation
          startPos[i * 3 + 1] = sceneCenter.y + y
          startPos[i * 3 + 2] = sceneCenter.z + z
          targetPos[i * 3] = verts[i * 3]
          targetPos[i * 3 + 1] = verts[i * 3 + 1]
          targetPos[i * 3 + 2] = verts[i * 3 + 2]
          speeds[i] = 0.6 + Math.random() * 0.8
          pos[i * 3] = startPos[i * 3]
          pos[i * 3 + 1] = startPos[i * 3 + 1]
          pos[i * 3 + 2] = startPos[i * 3 + 2]
        }

        geo.setAttribute('position', new THREE.BufferAttribute(pos, 3))
        const mat = new THREE.PointsMaterial({
          color, size: 3, transparent: true, opacity: 0.85,
          sizeAttenuation: true, blending: THREE.AdditiveBlending, depthWrite: false,
        })
        const points = new THREE.Points(geo, mat)
        points.visible = false
        scene.add(points)
        return { points, pos, startPos, targetPos, speeds, geo }
      }

      const ref = buildCloud(refVertices, count, 0x3b9dff, -1)
      const meas = buildCloud(measVertices, count, 0x34d399, 1)

      // Trailing glow
      const buildTrails = (color: number) => {
        const trailN = 200
        const geo = new THREE.BufferGeometry()
        const pos = new Float32Array(trailN * 3)
        for (let i = 0; i < trailN; i++) {
          pos[i * 3] = sceneCenter.x + (Math.random() - 0.5) * separation * 3
          pos[i * 3 + 1] = sceneCenter.y + (Math.random() - 0.5) * separation * 2
          pos[i * 3 + 2] = sceneCenter.z + (Math.random() - 0.5) * separation * 3
        }
        geo.setAttribute('position', new THREE.BufferAttribute(pos, 3))
        const mat = new THREE.PointsMaterial({
          color, size: 1.2, transparent: true, opacity: 0.2,
          sizeAttenuation: true, blending: THREE.AdditiveBlending, depthWrite: false,
        })
        const pts = new THREE.Points(geo, mat)
        pts.visible = false
        scene.add(pts)
        return pts
      }

      particleSystem = {
        refPoints: ref.points,
        measPoints: meas.points,
        refTrails: buildTrails(0x2266cc),
        measTrails: buildTrails(0x1a9960),
        refPos: ref.pos,
        measPos: meas.pos,
        refStart: ref.startPos,
        measStart: meas.startPos,
        refTarget: ref.targetPos,
        measTarget: meas.targetPos,
        refSpeeds: ref.speeds,
        measSpeeds: meas.speeds,
        count,
        startTime: 0,
      }
    }

    // Animation loop
    const clock = new THREE.Clock()
    let animId = 0

    const state: SceneState = {
      scene, camera, renderer, controls, meshes, refMaterials, measMaterials,
      animId, ro: null as unknown as ResizeObserver,
      particleSystem, processingActive: false, sceneCenter, maxDim,
    }
    sceneRef.current = state

    const TOTAL_EST = 55 // seconds estimate for convergence animation

    const animate = () => {
      state.animId = requestAnimationFrame(animate)
      const t = clock.getElapsedTime()

      // Ambient particle drift
      const ap = ambientParticles.geometry.attributes.position.array as Float32Array
      for (let i = 0; i < ambientCount; i++) {
        ap[i * 3 + 1] += Math.sin(t * 0.3 + i) * 0.02
      }
      ambientParticles.geometry.attributes.position.needsUpdate = true
      ambientParticles.rotation.y = t * 0.01

      // Processing particle animation
      if (state.processingActive && state.particleSystem) {
        const ps = state.particleSystem
        const elapsed = (Date.now() - ps.startTime) / 1000
        const rawProgress = Math.min(0.95, elapsed / TOTAL_EST)
        const progress = rawProgress * rawProgress * (3 - 2 * rawProgress)

        // Update both clouds
        const updateCloud = (
          pos: Float32Array, start: Float32Array, target: Float32Array,
          speeds: Float32Array, n: number, points: THREE.Points,
        ) => {
          for (let i = 0; i < n; i++) {
            const sp = speeds[i]
            const lp = Math.min(1, progress * sp * 1.3)
            const ease = lp * lp * (3 - 2 * lp)
            const wobble = Math.sin(t * 2 + i * 0.5) * (1 - ease) * maxDim * 0.015
            pos[i * 3] = start[i * 3] + (target[i * 3] - start[i * 3]) * ease + wobble
            pos[i * 3 + 1] = start[i * 3 + 1] + (target[i * 3 + 1] - start[i * 3 + 1]) * ease + wobble * 0.5
            pos[i * 3 + 2] = start[i * 3 + 2] + (target[i * 3 + 2] - start[i * 3 + 2]) * ease
          }
          points.geometry.attributes.position.needsUpdate = true
          ;(points.material as THREE.PointsMaterial).opacity = 0.5 + Math.sin(t * 3) * 0.2
        }

        updateCloud(ps.refPos, ps.refStart, ps.refTarget, ps.refSpeeds, ps.count, ps.refPoints)
        updateCloud(ps.measPos, ps.measStart, ps.measTarget, ps.measSpeeds, ps.count, ps.measPoints)

        // Trails drift inward
        const updateTrail = (pts: THREE.Points, dir: number) => {
          const arr = (pts.geometry.attributes.position.array as Float32Array)
          for (let i = 0; i < arr.length / 3; i++) {
            arr[i * 3] += dir * -0.3 * (1 - progress)
            arr[i * 3 + 1] += Math.sin(t + i) * 0.1
            const sep = maxDim * 0.6
            if (Math.abs(arr[i * 3] - sceneCenter.x) > sep * 2) {
              arr[i * 3] = sceneCenter.x + (Math.random() - 0.5) * sep
            }
          }
          pts.geometry.attributes.position.needsUpdate = true
          ;(pts.material as THREE.PointsMaterial).opacity = 0.15 * (1 - progress * 0.7)
        }
        updateTrail(ps.refTrails, -1)
        updateTrail(ps.measTrails, 1)

        // Slow auto-orbit during processing
        const orbitRadius = maxDim * (1.5 - progress * 0.3)
        camera.position.x = sceneCenter.x + Math.cos(t * 0.12) * orbitRadius
        camera.position.z = sceneCenter.z + Math.sin(t * 0.12) * orbitRadius
        camera.position.y = sceneCenter.y + maxDim * (0.5 + Math.sin(t * 0.08) * 0.1)
        camera.lookAt(sceneCenter)
        controls.target.copy(sceneCenter)
      }

      controls.update()
      renderer.render(scene, camera)
    }
    animate()

    const ro = new ResizeObserver(() => {
      if (!mount) return
      camera.aspect = mount.clientWidth / mount.clientHeight
      camera.updateProjectionMatrix()
      renderer.setSize(mount.clientWidth, mount.clientHeight)
    })
    ro.observe(mount)
    state.ro = ro

    return () => {
      cancelAnimationFrame(state.animId)
      ro.disconnect()
      controls.dispose()
      renderer.dispose()
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement)
      sceneRef.current = null
    }
  }, [reference, measured])

  // Live color update — no scene rebuild needed
  useEffect(() => {
    const s = sceneRef.current
    if (!s) return
    const updateMats = (mats: THREE.Material[], hex: string) => {
      for (const m of mats) {
        if ('color' in m && m.color instanceof THREE.Color) {
          m.color.set(hex)
        }
      }
    }
    updateMats(s.refMaterials, refColor)
    updateMats(s.measMaterials, measColor)
    // Also update particle colors if they exist
    if (s.particleSystem) {
      ;(s.particleSystem.refPoints.material as THREE.PointsMaterial).color.set(refColor)
      ;(s.particleSystem.measPoints.material as THREE.PointsMaterial).color.set(measColor)
    }
  }, [refColor, measColor])

  // Toggle processing mode — show/hide particles vs meshes
  useEffect(() => {
    const s = sceneRef.current
    if (!s) return

    s.processingActive = isProcessing

    // Toggle mesh visibility
    for (const m of s.meshes) {
      m.visible = !isProcessing
    }

    // Toggle particle visibility
    if (s.particleSystem) {
      const ps = s.particleSystem
      ps.refPoints.visible = isProcessing
      ps.measPoints.visible = isProcessing
      ps.refTrails.visible = isProcessing
      ps.measTrails.visible = isProcessing

      if (isProcessing) {
        ps.startTime = Date.now()
        // Reset positions to start
        ps.refPos.set(ps.refStart)
        ps.measPos.set(ps.measStart)
        ps.refPoints.geometry.attributes.position.needsUpdate = true
        ps.measPoints.geometry.attributes.position.needsUpdate = true
        // Disable user controls during processing for cinematic orbit
        s.controls.enabled = false
      } else {
        s.controls.enabled = true
      }
    }
  }, [isProcessing])

  if (!reference && !measured) return null

  return (
    <div className="bg-gray-950 border border-gray-800 rounded-xl overflow-hidden shadow-2xl shadow-blue-900/10">
      <div className="px-4 py-2.5 border-b border-gray-800/60 flex items-center gap-4 text-xs bg-gray-900/50">
        {reference && (
          <span className="flex items-center gap-2">
            <ColorPicker color={refColor} onChange={setRefColor} side="left" />
            <span className="text-gray-300">Reference: <span style={{ color: refColor }}>{reference.fileName}</span></span>
          </span>
        )}
        {reference && measured && <span className="text-gray-700">|</span>}
        {measured && (
          <span className="flex items-center gap-2">
            <ColorPicker color={measColor} onChange={setMeasColor} side={reference ? 'right' : 'left'} />
            <span className="text-gray-300">Measured: <span style={{ color: measColor }}>{measured.fileName}</span></span>
          </span>
        )}
        {isProcessing && (
          <>
            <span className="text-gray-700">|</span>
            <span className="flex items-center gap-1.5">
              <span className="w-2 h-2 rounded-full bg-purple-500 animate-pulse shadow-sm shadow-purple-500/50" />
              <span className="text-purple-400">Aligning...</span>
            </span>
          </>
        )}
      </div>
      <div ref={mountRef} style={{ height: 500 }} className="w-full" />
    </div>
  )
}
