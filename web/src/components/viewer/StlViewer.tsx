import { useEffect, useRef, useCallback, forwardRef, useImperativeHandle, useState } from 'react'
import * as THREE from 'three'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { GizmoManager } from './GizmoManager'

// ── Public types ──────────────────────────────────────────────────────────────

export interface BuildVolume { width: number; depth: number; height: number }

/**
 * All values in print-space:
 *   x/y = left-right / front-back on the bed (mm from center)
 *   z   = height above bed (mm)
 *   rotX/Y = tilt; rotZ = spin on bed (degrees)
 *   scaleX/Y/Z = multipliers (1.0 = unscaled)
 *
 * Three.js mapping (Y-up):  threeX=printX  threeY=printZ  threeZ=printY
 */
export interface ModelTransform {
  x: number; y: number; z: number
  rotX: number; rotY: number; rotZ: number
  scaleX: number; scaleY: number; scaleZ: number
}

export const DEFAULT_TRANSFORM: ModelTransform = {
  x: 0, y: 0, z: 0,
  rotX: 0, rotY: 0, rotZ: 0,
  scaleX: 1, scaleY: 1, scaleZ: 1,
}

export interface ModelEntry {
  id: string
  name: string
  url: string
  transform: ModelTransform
}

export interface StlViewerHandle {
  placeFaceOnBed(): void
  resetTransform(): void
  centerOnBed(): void
  placeOnBed(): void
  autoArrange(): void
}

interface Props {
  models: ModelEntry[]
  selectedId: string | null
  className?: string
  buildVolume?: BuildVolume
  onModelLoaded?: (id: string, size: { x: number; y: number; z: number }) => void
  onTransformChange?: (id: string, t: ModelTransform) => void
  onModelSelect?: (id: string | null) => void
  onBoundsChange?: (id: string, out: boolean) => void
  onFaceSelected?: (selected: boolean) => void
  onSizeChange?: (id: string, size: { x: number; y: number; z: number }) => void
}

// ── Coordinate-space helpers ──────────────────────────────────────────────────
//  print-space → Three.js:  rotZ (spin on bed) → euler.y   rotY (depth tilt) → euler.z
//  print scale: scaleY(depth)→scale.z  scaleZ(height)→scale.y

function applyTransform(group: THREE.Group, t: ModelTransform) {
  group.position.set(t.x, t.z, t.y)
  group.rotation.set(
    THREE.MathUtils.degToRad(t.rotX),
    THREE.MathUtils.degToRad(t.rotZ),
    THREE.MathUtils.degToRad(t.rotY),
    'XYZ',
  )
  group.scale.set(t.scaleX, t.scaleZ, t.scaleY)
}

function extractTransform(group: THREE.Group, _prev: ModelTransform): ModelTransform {
  return {
    x: group.position.x,
    y: group.position.z,
    z: group.position.y,
    rotX: THREE.MathUtils.radToDeg(group.rotation.x),
    rotY: THREE.MathUtils.radToDeg(group.rotation.z),
    rotZ: THREE.MathUtils.radToDeg(group.rotation.y),
    scaleX: group.scale.x,
    scaleY: group.scale.z,
    scaleZ: group.scale.y,
  }
}

/** Fast world-AABB from cached geometry BB + matrix — O(8) not O(2.1M vertices) */
function fastWorldBB(group: THREE.Group): THREE.Box3 {
  const mesh = group.children[0] as THREE.Mesh
  if (!mesh?.geometry?.boundingBox) return new THREE.Box3()
  group.updateMatrixWorld()
  return mesh.geometry.boundingBox.clone().applyMatrix4(mesh.matrixWorld)
}

function modelIsOOB(group: THREE.Group, bv: BuildVolume): boolean {
  const wb = fastWorldBB(group)
  if (wb.isEmpty()) return false
  return (
    wb.min.x < -bv.width / 2 || wb.max.x > bv.width / 2 ||
    wb.min.z < -bv.depth / 2 || wb.max.z > bv.depth / 2 ||
    wb.min.y < -0.01 || wb.max.y > bv.height
  )
}

// ── Per-model internal state ──────────────────────────────────────────────────

interface MeshData {
  group: THREE.Group
  mesh: THREE.Mesh
  proxyMesh: THREE.Mesh        // cheap invisible box for raycasting (12 tris vs 700K)
  url: string
  naturalSize: THREE.Vector3   // un-scaled, in Three.js units
  currentTransform: ModelTransform
  faceNormal: THREE.Vector3 | null     // local geometry space
  faceHitPoint: THREE.Vector3 | null   // world space
  arrowHelper: THREE.ArrowHelper | null
}

// ── Constants ─────────────────────────────────────────────────────────────────

const DEFAULT_BV: BuildVolume = { width: 220, depth: 220, height: 250 }

// Model colors
const C_NORMAL   = 0x1e40af  // unselected blue
const C_SELECTED = 0x3b82f6  // selected bright blue
const C_FACE     = 0xf97316  // face selected orange
const C_OOB      = 0xef4444  // out-of-bounds red
const C_OOB_SEL  = 0xdc2626  // out-of-bounds selected

// ── Component ─────────────────────────────────────────────────────────────────

const StlViewer = forwardRef<StlViewerHandle, Props>(function StlViewer(
  {
    models,
    selectedId,
    className = '',
    buildVolume = DEFAULT_BV,
    onModelLoaded,
    onTransformChange,
    onModelSelect,
    onBoundsChange,
    onFaceSelected,
    onSizeChange,
  },
  ref,
) {
  const mountRef   = useRef<HTMLDivElement>(null)
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null)
  const sceneRef   = useRef<THREE.Scene | null>(null)
  const cameraRef  = useRef<THREE.PerspectiveCamera | null>(null)
  const controlsRef = useRef<OrbitControls | null>(null)
  const animIdRef  = useRef(0)

  const buildBoxRef = useRef<THREE.LineSegments | null>(null)
  const bedMeshRef  = useRef<THREE.Mesh | null>(null)
  const boxHelperRef = useRef<{ helper: THREE.Box3Helper; box: THREE.Box3 } | null>(null)

  const meshMapRef    = useRef<Map<string, MeshData>>(new Map())
  const loadingIdsRef = useRef<Set<string>>(new Set())
  const gizmoRef      = useRef<GizmoManager | null>(null)

  // Drag state (model bed-plane drag)
  const draggingIdRef    = useRef<string | null>(null)
  const hasDraggedRef    = useRef(false)
  const mouseDownPxRef   = useRef({ x: 0, y: 0 })
  const dragOffsetRef    = useRef({ x: 0, z: 0 })
  // Empty-area click tracking (for deselect-on-click-empty)
  const emptyClickPxRef  = useRef<{ x: number; y: number } | null>(null)

  // Use state so effects that depend on scene being ready re-run correctly
  const [sceneReady, setSceneReady] = useState(false)

  // Stable prop refs (so closures always see the latest without re-binding)
  const modelsRef           = useRef(models);            modelsRef.current = models
  const selectedIdRef       = useRef(selectedId);        selectedIdRef.current = selectedId
  const buildVolumeRef      = useRef(buildVolume);       buildVolumeRef.current = buildVolume
  const onTransformChangeRef = useRef(onTransformChange); onTransformChangeRef.current = onTransformChange
  const onModelSelectRef    = useRef(onModelSelect);     onModelSelectRef.current = onModelSelect
  const onBoundsChangeRef   = useRef(onBoundsChange);    onBoundsChangeRef.current = onBoundsChange
  const onFaceSelectedRef   = useRef(onFaceSelected);    onFaceSelectedRef.current = onFaceSelected
  const onModelLoadedRef    = useRef(onModelLoaded);     onModelLoadedRef.current = onModelLoaded
  const onSizeChangeRef     = useRef(onSizeChange);      onSizeChangeRef.current  = onSizeChange

  // ── Color / bounds helpers ────────────────────────────────────────────────

  const getModelColor = useCallback((id: string): number => {
    const data = meshMapRef.current.get(id)
    if (!data) return C_NORMAL
    const sel = id === selectedIdRef.current
    const out = modelIsOOB(data.group, buildVolumeRef.current)
    if (out) return sel ? C_OOB_SEL : C_OOB
    if (sel && data.faceNormal) return C_FACE
    return sel ? C_SELECTED : C_NORMAL
  }, [])

  const paintMesh = useCallback((id: string) => {
    const data = meshMapRef.current.get(id)
    if (!data) return
    ;(data.mesh.material as THREE.MeshPhongMaterial).color.setHex(getModelColor(id))
  }, [getModelColor])

  const paintAll = useCallback(() => {
    for (const id of meshMapRef.current.keys()) paintMesh(id)
  }, [paintMesh])

  const checkAllBounds = useCallback(() => {
    for (const [id] of meshMapRef.current) {
      const d = meshMapRef.current.get(id)
      if (d) onBoundsChangeRef.current?.(id, modelIsOOB(d.group, buildVolumeRef.current))
    }
    paintAll()
  }, [paintAll])

  // ── Face selection ────────────────────────────────────────────────────────

  const clearFace = useCallback((id?: string) => {
    const target = id ?? selectedIdRef.current
    if (!target) return
    const data = meshMapRef.current.get(target)
    if (!data) return
    data.faceNormal = null
    data.faceHitPoint = null
    if (data.arrowHelper && sceneRef.current) {
      sceneRef.current.remove(data.arrowHelper)
      data.arrowHelper = null
    }
    paintMesh(target)
    onFaceSelectedRef.current?.(false)
  }, [paintMesh])

  // ── Imperative handle ─────────────────────────────────────────────────────

  useImperativeHandle(ref, () => ({

    placeFaceOnBed() {
      const id = selectedIdRef.current
      if (!id) return
      const data = meshMapRef.current.get(id)
      if (!data || !data.faceNormal) return

      // Ensure matrices are current before transforming the face normal
      data.group.updateMatrixWorld(true)

      // Transform local-space face normal → world space
      const worldNormal = data.faceNormal.clone()
        .transformDirection(data.mesh.matrixWorld)
        .normalize()

      const down = new THREE.Vector3(0, -1, 0)
      if (worldNormal.dot(down) < 0.9999) {
        const q = new THREE.Quaternion().setFromUnitVectors(worldNormal, down)
        data.group.quaternion.premultiply(q)
      }

      // Snap lowest point to bed (Y = 0) — use precise=true to get exact vertex-level AABB
      data.group.updateMatrixWorld(true)
      const wb = fastWorldBB(data.group)
      data.group.position.y -= wb.min.y

      data.currentTransform = extractTransform(data.group, data.currentTransform)
      clearFace(id)
      checkAllBounds()
      onTransformChangeRef.current?.(id, { ...data.currentTransform })
    },

    resetTransform() {
      const id = selectedIdRef.current
      if (!id) return
      const data = meshMapRef.current.get(id)
      if (!data) return
      applyTransform(data.group, DEFAULT_TRANSFORM)
      data.currentTransform = { ...DEFAULT_TRANSFORM }
      clearFace(id)
      checkAllBounds()
      onTransformChangeRef.current?.(id, { ...DEFAULT_TRANSFORM })
    },

    centerOnBed() {
      const id = selectedIdRef.current
      if (!id) return
      const data = meshMapRef.current.get(id)
      if (!data) return
      data.group.position.x = 0
      data.group.position.z = 0
      data.currentTransform.x = 0
      data.currentTransform.y = 0
      checkAllBounds()
      onTransformChangeRef.current?.(id, { ...data.currentTransform })
    },

    placeOnBed() {
      const id = selectedIdRef.current
      if (!id) return
      const data = meshMapRef.current.get(id)
      if (!data) return
      data.group.updateMatrixWorld(true)
      const wb = fastWorldBB(data.group)
      data.group.position.y -= wb.min.y
      if (data.group.position.y < 0) data.group.position.y = 0
      data.currentTransform.z = data.group.position.y
      checkAllBounds()
      onTransformChangeRef.current?.(id, { ...data.currentTransform })
    },

    autoArrange() {
      const bv = buildVolumeRef.current
      const SPACING = 5

      // Collect footprints from current world bounding boxes
      const fps: { id: string; w: number; d: number }[] = []
      for (const [id, data] of meshMapRef.current) {
        data.group.updateMatrixWorld(true)
        const wb = fastWorldBB(data.group)
        fps.push({ id, w: wb.max.x - wb.min.x, d: wb.max.z - wb.min.z })
      }
      fps.sort((a, b) => b.w * b.d - a.w * a.d)

      let curX = -bv.width / 2 + SPACING
      let curZ = -bv.depth / 2 + SPACING
      let rowD = 0

      for (let i = 0; i < fps.length; i++) {
        const { id, w, d } = fps[i]
        if (i > 0 && curX + w > bv.width / 2 - SPACING) {
          curX = -bv.width / 2 + SPACING
          curZ += rowD + SPACING
          rowD = 0
        }
        const data = meshMapRef.current.get(id)!
        data.group.position.x = curX + w / 2
        data.group.position.z = curZ + d / 2
        data.group.updateMatrixWorld(true)
        const wb = fastWorldBB(data.group)
        data.group.position.y -= wb.min.y
        if (data.group.position.y < 0) data.group.position.y = 0
        data.currentTransform = extractTransform(data.group, data.currentTransform)
        onTransformChangeRef.current?.(id, { ...data.currentTransform })
        curX += w + SPACING
        rowD = Math.max(rowD, d)
      }
      checkAllBounds()
    },

  }), [clearFace, checkAllBounds])

  // ── Scene setup (once on mount) ───────────────────────────────────────────

  useEffect(() => {
    const mount = mountRef.current
    if (!mount) return

    const scene = new THREE.Scene()
    scene.background = new THREE.Color(0x111827)
    sceneRef.current = scene

    const camera = new THREE.PerspectiveCamera(45, mount.clientWidth / mount.clientHeight, 0.1, 5000)
    camera.position.set(200, 150, 200)
    cameraRef.current = camera

    const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance' })
    renderer.setSize(mount.clientWidth, mount.clientHeight)
    renderer.setPixelRatio(1)
    mount.appendChild(renderer.domElement)
    rendererRef.current = renderer

    // Log GPU info for debugging
    const glCtx = renderer.getContext()
    const dbg = glCtx.getExtension('WEBGL_debug_renderer_info')
    if (dbg) console.log('[STL Viewer GPU]', glCtx.getParameter(dbg.UNMASKED_RENDERER_WEBGL))

    scene.add(new THREE.AmbientLight(0xffffff, 0.7))
    const sun = new THREE.DirectionalLight(0xffffff, 0.8)
    sun.position.set(1, 2, 3)
    scene.add(sun)

    const controls = new OrbitControls(camera, renderer.domElement)
    controls.enableDamping = false
    controls.mouseButtons = {
      LEFT: THREE.MOUSE.ROTATE,
      MIDDLE: THREE.MOUSE.DOLLY,
      RIGHT: THREE.MOUSE.PAN,
    }
    controlsRef.current = controls

    scene.add(new THREE.GridHelper(800, 40, 0x374151, 0x1f2937))

    // ── Gizmo ──────────────────────────────────────────────────────────────
    const gizmo = new GizmoManager(scene, camera)
    gizmoRef.current = gizmo

    // ── Mouse interaction ─────────────────────────────────────────────────

    const raycaster = new THREE.Raycaster()
    const bedPlane  = new THREE.Plane(new THREE.Vector3(0, 1, 0), 0)

    const toNDC = (e: MouseEvent) => {
      const r = renderer.domElement.getBoundingClientRect()
      return new THREE.Vector2(
        ((e.clientX - r.left) / r.width) * 2 - 1,
        -((e.clientY - r.top) / r.height) * 2 + 1,
      )
    }

    let lastCbMs = 0

    const emitTransform = (id: string) => {
      const data = meshMapRef.current.get(id)
      if (!data) return
      data.group.updateMatrixWorld(true)
      data.currentTransform = extractTransform(data.group, data.currentTransform)
      const wb = fastWorldBB(data.group)
      const ws = wb.getSize(new THREE.Vector3())
      onSizeChangeRef.current?.(id, { x: ws.x, y: ws.z, z: ws.y })
      onBoundsChangeRef.current?.(id, modelIsOOB(data.group, buildVolumeRef.current))
      onTransformChangeRef.current?.(id, { ...data.currentTransform })
      paintMesh(id)
    }

    const onMouseDown = (e: MouseEvent) => {
      if (e.button !== 0) return

      raycaster.setFromCamera(toNDC(e), camera)
      emptyClickPxRef.current = null

      // ── 1. Check gizmo handles first ──────────────────────────────────────
      const hitHandle = gizmo.hitTest(raycaster)
      if (hitHandle) {
        const selId = selectedIdRef.current
        if (selId) {
          gizmo.startDrag(hitHandle, raycaster.ray)
          controls.enabled = false
          hasDraggedRef.current = false
          mouseDownPxRef.current = { x: e.clientX, y: e.clientY }
          // Use draggingIdRef to track gizmo drags too (sentinel value)
          draggingIdRef.current = '__gizmo__'
        }
        return
      }

      // ── 2. Check model meshes (via cheap proxy boxes) ─────────────────────
      const allProxies: THREE.Mesh[] = []
      for (const d of meshMapRef.current.values()) allProxies.push(d.proxyMesh)

      if (allProxies.length === 0) {
        emptyClickPxRef.current = { x: e.clientX, y: e.clientY }
        return
      }

      const hits = raycaster.intersectObjects(allProxies, false)
      if (hits.length === 0) {
        // Clicked on empty area — track for potential deselect
        emptyClickPxRef.current = { x: e.clientX, y: e.clientY }
        return
      }

      const hitProxy = hits[0].object as THREE.Mesh
      let hitId: string | null = null
      for (const [id, d] of meshMapRef.current) {
        if (d.proxyMesh === hitProxy) { hitId = id; break }
      }
      if (!hitId) return

      // Switch selection if needed
      if (hitId !== selectedIdRef.current) {
        clearFace(selectedIdRef.current ?? undefined)
        onModelSelectRef.current?.(hitId)
      }

      // Enter bed-plane drag for this model
      draggingIdRef.current  = hitId
      hasDraggedRef.current  = false
      mouseDownPxRef.current = { x: e.clientX, y: e.clientY }
      controls.enabled       = false

      // Store face data for potential face-click
      const data = meshMapRef.current.get(hitId)!
      if (hits[0].face) {
        data.faceNormal   = hits[0].face.normal.clone()
        data.faceHitPoint = hits[0].point.clone()
      }

      // Drag offset relative to bed plane hit
      const bedTarget = new THREE.Vector3()
      raycaster.ray.intersectPlane(bedPlane, bedTarget)
      dragOffsetRef.current = {
        x: data.group.position.x - bedTarget.x,
        z: data.group.position.z - bedTarget.z,
      }
    }

    const onMouseMove = (e: MouseEvent) => {
      const id = draggingIdRef.current
      if (!id) return

      const dx = e.clientX - mouseDownPxRef.current.x
      const dy = e.clientY - mouseDownPxRef.current.y
      if (Math.sqrt(dx * dx + dy * dy) > 3) hasDraggedRef.current = true
      if (!hasDraggedRef.current) return

      raycaster.setFromCamera(toNDC(e), camera)

      // ── Gizmo drag ────────────────────────────────────────────────────────
      if (id === '__gizmo__') {
        const selId = selectedIdRef.current
        if (!selId) return
        const result = gizmo.updateDrag(raycaster.ray)
        if (result.positionChanged || result.rotationChanged) {
          const data = meshMapRef.current.get(selId)
          if (data) {
            data.group.updateMatrixWorld(true)
            if (result.rotationChanged) {
              const wb = fastWorldBB(data.group)
              if (!wb.isEmpty() && wb.min.y < 0) data.group.position.y -= wb.min.y
            }
            gizmo.invalidateCenter()
            data.currentTransform = extractTransform(data.group, data.currentTransform)
            const now = Date.now()
            if (now - lastCbMs >= 40) {
              lastCbMs = now
              onBoundsChangeRef.current?.(selId, modelIsOOB(data.group, buildVolumeRef.current))
              onTransformChangeRef.current?.(selId, { ...data.currentTransform })
            }
            paintMesh(selId)
          }
        }
        return
      }

      // ── Bed-plane drag ────────────────────────────────────────────────────
      const data = meshMapRef.current.get(id)
      if (!data) return

      const target = new THREE.Vector3()
      if (!raycaster.ray.intersectPlane(bedPlane, target)) return

      data.group.position.x = target.x + dragOffsetRef.current.x
      data.group.position.z = target.z + dragOffsetRef.current.z
      data.currentTransform.x = data.group.position.x
      data.currentTransform.y = data.group.position.z
      gizmo.invalidateCenter()

      const now = Date.now()
      if (now - lastCbMs >= 40) {
        lastCbMs = now
        onTransformChangeRef.current?.(id, { ...data.currentTransform })
      }
      paintMesh(id)
    }

    const onMouseUp = (e: MouseEvent) => {
      const id = draggingIdRef.current
      controls.enabled = true

      // Empty-area single click → deselect
      if (!id && emptyClickPxRef.current) {
        const dx = e.clientX - emptyClickPxRef.current.x
        const dy = e.clientY - emptyClickPxRef.current.y
        if (Math.sqrt(dx * dx + dy * dy) <= 3 && selectedIdRef.current) {
          clearFace(selectedIdRef.current)
          onModelSelectRef.current?.(null)
        }
        emptyClickPxRef.current = null
        return
      }

      if (!id) return

      // ── End gizmo drag ────────────────────────────────────────────────────
      if (id === '__gizmo__') {
        gizmo.endDrag()
        const selId = selectedIdRef.current
        if (selId && hasDraggedRef.current) emitTransform(selId)
        draggingIdRef.current = null
        hasDraggedRef.current = false
        return
      }

      if (!hasDraggedRef.current) {
        // Was a click — handle face selection for the selected model
        const data = meshMapRef.current.get(id)
        if (data && id === selectedIdRef.current) {
          raycaster.setFromCamera(toNDC(e), camera)
          const hits = raycaster.intersectObject(data.mesh, false)

          if (hits.length > 0 && hits[0].face) {
            const face = hits[0].face
            data.faceNormal   = face.normal.clone()
            data.faceHitPoint = hits[0].point.clone()

            if (data.arrowHelper && sceneRef.current) sceneRef.current.remove(data.arrowHelper)
            const worldNorm = face.normal.clone().transformDirection(data.mesh.matrixWorld).normalize()
            const sz = fastWorldBB(data.group).getSize(new THREE.Vector3())
            const arrowLen = Math.max(10, sz.length() * 0.2)
            const arrow = new THREE.ArrowHelper(worldNorm, hits[0].point, arrowLen, 0xf97316, arrowLen * 0.3, arrowLen * 0.15)
            if (sceneRef.current) sceneRef.current.add(arrow)
            data.arrowHelper = arrow

            paintMesh(id)
            onFaceSelectedRef.current?.(true)
          } else {
            clearFace(id)
          }
        }
      } else {
        // End of bed-plane drag — emit final position
        emitTransform(id)
      }

      draggingIdRef.current = null
      hasDraggedRef.current = false
    }

    // Double-click on a model → attach gizmo to selected model
    const onDblClick = (e: MouseEvent) => {
      raycaster.setFromCamera(toNDC(e), camera)
      const selId = selectedIdRef.current
      if (!selId) return
      const data = meshMapRef.current.get(selId)
      if (!data) return
      const hits = raycaster.intersectObject(data.mesh, false)
      if (hits.length > 0) {
        gizmoRef.current?.attachTo(data.group)
      }
    }

    renderer.domElement.addEventListener('mousedown', onMouseDown)
    renderer.domElement.addEventListener('dblclick', onDblClick)
    window.addEventListener('mousemove', onMouseMove)
    window.addEventListener('mouseup', onMouseUp)

    // Simple render loop
    const animate = () => {
      animIdRef.current = requestAnimationFrame(animate)
      gizmo.update()
      renderer.render(scene, camera)
    }
    animate()
    ;(renderer as any).__requestRender = () => {}

    // Resize observer
    const ro = new ResizeObserver(() => {
      const m = mountRef.current
      if (!m || !rendererRef.current) return
      camera.aspect = m.clientWidth / m.clientHeight
      camera.updateProjectionMatrix()
      rendererRef.current.setSize(m.clientWidth, m.clientHeight)
    })
    ro.observe(mount)

    setSceneReady(true)

    return () => {
      setSceneReady(false)
      cancelAnimationFrame(animIdRef.current)
      ro.disconnect()
      controls.dispose()
      gizmo.dispose()
      gizmoRef.current = null
      renderer.domElement.removeEventListener('mousedown', onMouseDown)
      renderer.domElement.removeEventListener('dblclick', onDblClick)
      window.removeEventListener('mousemove', onMouseMove)
      window.removeEventListener('mouseup', onMouseUp)
      renderer.dispose()
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement)
      sceneRef.current  = null
      rendererRef.current = null
      cameraRef.current = null
      controlsRef.current = null
    }
  }, [clearFace, paintMesh])

  // ── Build volume box ──────────────────────────────────────────────────────

  useEffect(() => {
    if (!sceneReady) return
    const scene = sceneRef.current!

    if (buildBoxRef.current) {
      scene.remove(buildBoxRef.current)
      buildBoxRef.current.geometry.dispose()
      ;(buildBoxRef.current.material as THREE.Material).dispose()
      buildBoxRef.current = null
    }
    if (bedMeshRef.current) {
      scene.remove(bedMeshRef.current)
      bedMeshRef.current.geometry.dispose()
      ;(bedMeshRef.current.material as THREE.Material).dispose()
      bedMeshRef.current = null
    }

    const { width, depth, height } = buildVolume
    if (width > 0 && depth > 0 && height > 0) {
      // Wireframe envelope
      const boxGeo = new THREE.BoxGeometry(width, height, depth)
      const edges   = new THREE.EdgesGeometry(boxGeo)
      boxGeo.dispose()
      const lineBox = new THREE.LineSegments(
        edges,
        new THREE.LineBasicMaterial({ color: 0x4ade80, transparent: true, opacity: 0.5 }),
      )
      lineBox.position.set(0, height / 2, 0)
      scene.add(lineBox)
      buildBoxRef.current = lineBox

      // Translucent bed plane
      const bed = new THREE.Mesh(
        new THREE.PlaneGeometry(width, depth),
        new THREE.MeshBasicMaterial({ color: 0x14532d, transparent: true, opacity: 0.25, side: THREE.DoubleSide }),
      )
      bed.rotation.x = -Math.PI / 2
      bed.position.y  = 0.05
      scene.add(bed)
      bedMeshRef.current = bed
    }

    checkAllBounds()
  }, [buildVolume, sceneReady, checkAllBounds])

  // ── Models sync ───────────────────────────────────────────────────────────

  useEffect(() => {
    if (!sceneReady) return
    const scene = sceneRef.current!

    const currentIds = new Set(models.map(m => m.id))

    // Remove deleted models
    for (const [id, data] of meshMapRef.current) {
      if (!currentIds.has(id)) {
        scene.remove(data.group)
        data.mesh.geometry.dispose()
        ;(data.mesh.material as THREE.Material).dispose()
        if (data.arrowHelper) scene.remove(data.arrowHelper)
        meshMapRef.current.delete(id)
      }
    }

    // Add new models, update existing
    for (const model of models) {
      const existing  = meshMapRef.current.get(model.id)
      const isLoading = loadingIdsRef.current.has(model.id)

      if (!existing && !isLoading) {
        // New model — load async (guard prevents duplicate loads on re-render)
        loadingIdsRef.current.add(model.id)
        const loader = new STLLoader()

        loader.load(
          model.url,
          (geometry) => {
            loadingIdsRef.current.delete(model.id)
            if (!sceneRef.current) { geometry.dispose(); return }

            // Model may have been removed while loading
            const liveModel = modelsRef.current.find(m => m.id === model.id)
            if (!liveModel) { geometry.dispose(); return }

            // STL files include face normals — use them directly (skip expensive computeVertexNormals)
            if (!geometry.attributes.normal) geometry.computeVertexNormals()
            geometry.center()
            geometry.computeBoundingBox()
            const bb   = geometry.boundingBox!
            const size = new THREE.Vector3()
            bb.getSize(size)
            geometry.translate(0, size.y / 2, 0)
            geometry.computeBoundingBox()

            const mesh = new THREE.Mesh(
              geometry,
              new THREE.MeshLambertMaterial({ color: C_NORMAL }),
            )
            mesh.frustumCulled = true

            // Cheap invisible box proxy for raycasting (12 tris vs 700K)
            const bbCenter = new THREE.Vector3()
            geometry.boundingBox!.getCenter(bbCenter)
            const proxyGeo = new THREE.BoxGeometry(size.x, size.y, size.z)
            const proxyMesh = new THREE.Mesh(
              proxyGeo,
              new THREE.MeshBasicMaterial({ visible: false, side: THREE.DoubleSide }),
            )
            proxyMesh.position.copy(bbCenter)

            const group = new THREE.Group()
            group.add(mesh)
            group.add(proxyMesh)
            applyTransform(group, liveModel.transform)
            sceneRef.current!.add(group)

            const data: MeshData = {
              group, mesh, proxyMesh, url: liveModel.url,
              naturalSize: size.clone(),
              currentTransform: { ...liveModel.transform },
              faceNormal: null, faceHitPoint: null, arrowHelper: null,
            }
            meshMapRef.current.set(model.id, data)

            // First model → frame camera
            if (meshMapRef.current.size === 1) {
              const bv = buildVolumeRef.current
              const d  = Math.max(size.x, size.y, size.z, bv.width, bv.depth) * 1.7
              cameraRef.current!.position.set(d, d * 0.7, d)
              cameraRef.current!.lookAt(0, size.y / 2, 0)
              controlsRef.current!.target.set(0, size.y / 2, 0)
              controlsRef.current!.update()
            }

            // Report natural size in print space (x=X, y=depth, z=height)
            onModelLoadedRef.current?.(model.id, { x: size.x, y: size.z, z: size.y })
            paintMesh(model.id)
            checkAllBounds()
          },
          undefined,
          (err) => {
            loadingIdsRef.current.delete(model.id)
            console.error('STLLoader:', err)
          },
        )

      } else if (existing && existing.url !== model.url) {
        // URL changed — remove and let the next render re-load
        scene.remove(existing.group)
        existing.mesh.geometry.dispose()
        ;(existing.mesh.material as THREE.Material).dispose()
        if (existing.arrowHelper) scene.remove(existing.arrowHelper)
        meshMapRef.current.delete(model.id)

      } else if (existing && draggingIdRef.current !== model.id) {
        // Transform changed from parent (e.g., numeric input)
        applyTransform(existing.group, model.transform)
        existing.currentTransform = { ...model.transform }
        existing.group.updateMatrixWorld(true)
        const wb = fastWorldBB(existing.group)
        const ws = wb.getSize(new THREE.Vector3())
        onSizeChangeRef.current?.(model.id, { x: ws.x, y: ws.z, z: ws.y })
        paintMesh(model.id)
        checkAllBounds()
      }
    }
    // Trigger render after model sync
    const rr = rendererRef.current as any
    if (rr?.__requestRender) rr.__requestRender()
  }, [models, sceneReady, paintMesh, checkAllBounds])

  // ── Selected model BoxHelper ──────────────────────────────────────────────

  useEffect(() => {
    if (!sceneReady) return
    const scene = sceneRef.current!

    if (boxHelperRef.current) {
      scene.remove(boxHelperRef.current.helper)
      boxHelperRef.current = null
    }

    if (selectedId) {
      const data = meshMapRef.current.get(selectedId)
      if (data) {
        // Gizmo is NOT auto-attached on selection — use double-click to show gizmo
        if (data.mesh.geometry.attributes.position) {
          const box = fastWorldBB(data.group)
          const helper = new THREE.Box3Helper(box, 0xfbbf24)
          scene.add(helper)
          boxHelperRef.current = { helper, box }
        }
      }
    } else {
      gizmoRef.current?.attachTo(null)
    }

    paintAll()
  }, [selectedId, sceneReady, paintAll])

  return <div ref={mountRef} className={`w-full h-full ${className}`} />
})

export default StlViewer
