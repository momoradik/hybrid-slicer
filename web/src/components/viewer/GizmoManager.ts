import * as THREE from 'three'

// ── Types ─────────────────────────────────────────────────────────────────────

export type GizmoHandle =
  | 'move-x' | 'move-y' | 'move-z'
  | 'rotate-x' | 'rotate-y' | 'rotate-z'
  | null

interface DragState {
  handle: GizmoHandle
  axis: THREE.Vector3
  plane: THREE.Plane
  /** for move: projection of start-hit onto axis */
  startAxisVal: number
  /** for rotate: unit vector from gizmo center to start-hit (in rotation plane) */
  startDir: THREE.Vector3
  /** copies of group state at drag start */
  groupStartPos: THREE.Vector3
  groupStartQ: THREE.Quaternion
  gizmoCenter: THREE.Vector3
}

// ── Colors ────────────────────────────────────────────────────────────────────

const C = {
  x:       0xef4444,  // red   — print X
  y:       0x3b82f6,  // blue  — print Z (up in Three.js)
  z:       0x22c55e,  // green — print Y (depth)
  xHover:  0xff7777,
  yHover:  0x7db8ff,
  zHover:  0x5edb82,
}

// ── GizmoManager ──────────────────────────────────────────────────────────────

export class GizmoManager {
  private root: THREE.Group
  private handleObjects: Map<string, THREE.Object3D> = new Map()
  private hitIdToHandle: Map<number, GizmoHandle> = new Map()

  private camera: THREE.PerspectiveCamera
  private scene: THREE.Scene

  private attachedGroup: THREE.Group | null = null
  private drag: DragState | null = null

  constructor(scene: THREE.Scene, camera: THREE.PerspectiveCamera) {
    this.scene = scene
    this.camera = camera
    this.root = new THREE.Group()
    scene.add(this.root)
    this._build()
    this.root.visible = false
  }

  // ── Build geometry ──────────────────────────────────────────────────────────

  private _arrow(color: number): THREE.Group {
    const g = new THREE.Group()

    const bodyMat = new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.92 })
    const tipMat  = new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.92 })

    const body = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.035, 0.65, 10), bodyMat)
    body.position.y = 0.325

    const tip = new THREE.Mesh(new THREE.ConeGeometry(0.095, 0.28, 10), tipMat)
    tip.position.y = 0.82

    // Invisible wider hit zone for easier clicking
    const hitZone = new THREE.Mesh(
      new THREE.CylinderGeometry(0.11, 0.11, 0.95, 8),
      new THREE.MeshBasicMaterial({ visible: false }),
    )
    hitZone.position.y = 0.475

    g.add(body, tip, hitZone)
    return g
  }

  private _ring(color: number): THREE.Mesh {
    return new THREE.Mesh(
      new THREE.TorusGeometry(0.78, 0.038, 8, 48),
      new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true, opacity: 0.88, side: THREE.DoubleSide }),
    )
  }

  private _register(key: string, obj: THREE.Object3D, handle: GizmoHandle) {
    this.handleObjects.set(key, obj)
    obj.traverse(c => {
      if (c instanceof THREE.Mesh) this.hitIdToHandle.set(c.id, handle)
    })
  }

  private _build() {
    // ── Move arrows ──────────────────────────────────────────────────────────
    const ax = this._arrow(C.x)
    ax.rotation.z = -Math.PI / 2          // points along +X
    const ay = this._arrow(C.y)           // already along +Y
    const az = this._arrow(C.z)
    az.rotation.x = Math.PI / 2           // points along +Z

    this.root.add(ax, ay, az)
    this._register('move-x', ax, 'move-x')
    this._register('move-y', ay, 'move-y')
    this._register('move-z', az, 'move-z')

    // ── Rotation rings ────────────────────────────────────────────────────────
    // Default torus is in XY plane (ring faces +Z axis = rotates around Z)
    const rx = this._ring(C.x)
    rx.rotation.y = Math.PI / 2           // now in YZ plane → rotates around X

    const ry = this._ring(C.y)
    ry.rotation.x = Math.PI / 2           // now in XZ plane → rotates around Y (spin on bed)

    const rz = this._ring(C.z)
    // stays in XY plane → rotates around Z

    this.root.add(rx, ry, rz)
    this._register('rotate-x', rx, 'rotate-x')
    this._register('rotate-y', ry, 'rotate-y')
    this._register('rotate-z', rz, 'rotate-z')
  }

  // ── Public API ──────────────────────────────────────────────────────────────

  attachTo(group: THREE.Group | null) {
    this.attachedGroup = group
    this.root.visible  = !!group
    this.drag = null
    this.needsCenterUpdate = true
  }

  /** Call every frame before render. Positions and scales the gizmo. */
  // Cached bounding box center — avoids scanning all vertices every frame
  private cachedCenter = new THREE.Vector3()
  private needsCenterUpdate = true

  /** Call this when the attached model's geometry or transform changes */
  invalidateCenter() { this.needsCenterUpdate = true }

  update() {
    if (!this.attachedGroup) return

    // Only recompute bounding box center when geometry/transform actually changed
    // (NOT every frame — scanning 700K+ vertices 60x/sec was the performance killer)
    if (this.needsCenterUpdate) {
      this.needsCenterUpdate = false
      this.attachedGroup.updateMatrixWorld(true)
      // Use cached geometry BB + matrix transform — O(8) not O(2.1M vertices)
      const mesh = this.attachedGroup.children[0] as THREE.Mesh
      if (mesh?.geometry?.boundingBox) {
        const wb = mesh.geometry.boundingBox.clone().applyMatrix4(mesh.matrixWorld)
        wb.getCenter(this.cachedCenter)
      }
    }

    this.root.position.copy(this.cachedCenter)

    // Scale so gizmo appears a constant fraction of the viewport
    const dist = this.camera.position.distanceTo(this.cachedCenter)
    this.root.scale.setScalar(Math.max(dist * 0.13, 1))

    // Gizmo is always world-axis-aligned
    this.root.rotation.set(0, 0, 0)
  }

  /** Returns which handle is under the cursor, or null. */
  hitTest(raycaster: THREE.Raycaster): GizmoHandle {
    if (!this.root.visible) return null
    const meshes: THREE.Mesh[] = []
    this.root.traverse(c => { if (c instanceof THREE.Mesh) meshes.push(c) })
    const hits = raycaster.intersectObjects(meshes, false)
    if (!hits.length) return null
    return this.hitIdToHandle.get(hits[0].object.id) ?? null
  }

  /** Begin a drag interaction. Returns true if started successfully. */
  startDrag(handle: GizmoHandle, ray: THREE.Ray): boolean {
    if (!handle || !this.attachedGroup) return false
    const group  = this.attachedGroup
    const center = this.root.position.clone()

    const axisForHandle = (h: GizmoHandle): THREE.Vector3 => {
      if (h === 'move-x' || h === 'rotate-x') return new THREE.Vector3(1, 0, 0)
      if (h === 'move-y' || h === 'rotate-y') return new THREE.Vector3(0, 1, 0)
      return new THREE.Vector3(0, 0, 1)
    }
    const axis   = axisForHandle(handle)
    const isMove = handle.startsWith('move')

    let plane: THREE.Plane
    let startAxisVal = 0
    let startDir     = new THREE.Vector3()

    if (isMove) {
      // Plane faces the camera so the drag ray always intersects it
      const camDir = this.camera.position.clone().sub(center).normalize()
      plane        = new THREE.Plane().setFromNormalAndCoplanarPoint(camDir, center)
      const hit    = new THREE.Vector3()
      if (!ray.intersectPlane(plane, hit)) return false
      startAxisVal = hit.dot(axis)
    } else {
      // Plane perpendicular to rotation axis
      plane     = new THREE.Plane().setFromNormalAndCoplanarPoint(axis, center)
      const hit = new THREE.Vector3()
      if (!ray.intersectPlane(plane, hit)) return false
      const dir = hit.clone().sub(center)
      dir.addScaledVector(axis, -axis.dot(dir))   // project onto rotation plane
      if (dir.length() < 0.0001) return false
      startDir = dir.normalize()
    }

    this.drag = {
      handle, axis, plane,
      startAxisVal, startDir,
      groupStartPos: group.position.clone(),
      groupStartQ:   group.quaternion.clone(),
      gizmoCenter:   center.clone(),
    }
    return true
  }

  /**
   * Update drag. Returns flags indicating what changed.
   * Caller should call onTransformChange after this.
   */
  updateDrag(ray: THREE.Ray): { positionChanged: boolean; rotationChanged: boolean } {
    const d = this.drag
    if (!d || !this.attachedGroup) return { positionChanged: false, rotationChanged: false }

    const group = this.attachedGroup
    const hit   = new THREE.Vector3()
    if (!ray.intersectPlane(d.plane, hit)) return { positionChanged: false, rotationChanged: false }

    if (d.handle?.startsWith('move')) {
      const current = hit.dot(d.axis)
      const delta   = current - d.startAxisVal
      group.position.copy(d.groupStartPos).addScaledVector(d.axis, delta)
      return { positionChanged: true, rotationChanged: false }
    } else {
      // Rotation: compute signed angle between startDir and current direction
      const dir = hit.clone().sub(d.gizmoCenter)
      dir.addScaledVector(d.axis, -d.axis.dot(dir))   // project onto rotation plane
      if (dir.length() < 0.0001) return { positionChanged: false, rotationChanged: false }
      dir.normalize()

      const cosA  = Math.min(1, Math.max(-1, d.startDir.dot(dir)))
      const cross = d.startDir.clone().cross(dir)
      const sign  = cross.dot(d.axis) >= 0 ? 1 : -1
      const angle = sign * Math.acos(cosA)

      const q = new THREE.Quaternion().setFromAxisAngle(d.axis, angle)
      // Apply world-space rotation on top of the original quaternion
      group.quaternion.multiplyQuaternions(q, d.groupStartQ)
      return { positionChanged: false, rotationChanged: true }
    }
  }

  endDrag(): GizmoHandle {
    const h = this.drag?.handle ?? null
    this.drag = null
    return h
  }

  isDragging(): boolean { return this.drag !== null }

  dispose() {
    this.root.traverse(c => {
      if (c instanceof THREE.Mesh) {
        c.geometry.dispose()
        const m = c.material
        if (Array.isArray(m)) m.forEach(x => x.dispose())
        else m.dispose()
      }
    })
    this.scene.remove(this.root)
  }
}
