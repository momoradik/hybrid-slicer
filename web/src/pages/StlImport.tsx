import { useState, useCallback, useEffect, useRef, useMemo } from 'react'
import { Link } from 'react-router-dom'
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import DisabledHint from '../components/DisabledHint'
import InfoTip from '../components/InfoTip'
import * as THREE from 'three'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js'
import { STLExporter } from 'three/examples/jsm/exporters/STLExporter.js'
import StlViewer, {
  type BuildVolume,
  type ModelTransform,
  type ModelEntry,
  type StlViewerHandle,
  DEFAULT_TRANSFORM,
} from '../components/viewer/StlViewer'
import GCodePreview3D, { type BedInfo } from '../components/viewer/GCodePreview3D'
import { jobsApi, machineProfilesApi, printProfilesApi, materialsApi, toolsApi } from '../api/client'
import { useMachineConnection } from '../hooks/useMachineConnection'
import { useAppStore } from '../store'

// ── STL transform helper ───────────────────────────────────────────────────────
// Applies the user's viewer transform to the STL geometry and exports a new binary
// STL that Cura receives with the correct position/rotation/scale already baked in.
// Coordinate conversion: Three.js Y-up → Cura Z-up (swap Y↔Z axes).
async function buildTransformedStlBlob(file: File, transform: ModelTransform): Promise<File> {
  const loader   = new STLLoader()
  const exporter = new STLExporter()

  const geo = loader.parse(await file.arrayBuffer())

  // Same pre-processing as StlViewer: centre the geometry then lift base to Y=0
  geo.center()
  geo.computeBoundingBox()
  const size = new THREE.Vector3()
  geo.boundingBox!.getSize(size)
  geo.translate(0, size.y / 2, 0)

  // Apply the user transform exactly as StlViewer's applyTransform does
  const mesh  = new THREE.Mesh(geo)
  const group = new THREE.Group()
  group.add(mesh)
  group.position.set(transform.x, transform.z, transform.y)
  group.rotation.set(
    THREE.MathUtils.degToRad(transform.rotX),
    THREE.MathUtils.degToRad(transform.rotZ),
    THREE.MathUtils.degToRad(transform.rotY),
    'XYZ',
  )
  group.scale.set(transform.scaleX, transform.scaleZ, transform.scaleY)
  group.updateMatrixWorld(true)

  // Bake the world matrix into a geometry clone (result is in Three.js world space)
  const baked = geo.clone()
  baked.applyMatrix4(mesh.matrixWorld)

  // Convert Three.js world (Y-up: X=right, Y=height, Z=depth)
  //          → Cura print space (Z-up: X=right, Y=depth, Z=height)
  // i.e. swap Y and Z
  const pos = baked.attributes.position as THREE.BufferAttribute
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i)
    pos.setXYZ(i, x, z, y)
  }
  // Swapping two axes is a reflection (det = −1) which reverses winding.
  // Restore correct winding by swapping vertices 1 and 2 of every triangle.
  for (let i = 0; i < pos.count; i += 3) {
    const ax = pos.getX(i+1), ay = pos.getY(i+1), az = pos.getZ(i+1)
    const bx = pos.getX(i+2), by = pos.getY(i+2), bz = pos.getZ(i+2)
    pos.setXYZ(i+1, bx, by, bz)
    pos.setXYZ(i+2, ax, ay, az)
  }
  pos.needsUpdate = true
  baked.computeVertexNormals()

  const dv = exporter.parse(new THREE.Mesh(baked), { binary: true }) as DataView
  return new File([dv.buffer as ArrayBuffer], file.name, { type: 'application/octet-stream' })
}

// ── Per-model state ───────────────────────────────────────────────────────────

interface ModelState extends ModelEntry {
  file: File
  size: { x: number; y: number; z: number } | null
  isOutOfBounds: boolean
  bedIndex: number  // which bed this model is assigned to (0-based)
}

// ── Module-level persistence (survives React navigation) ──────────────────────

type InfillPattern =
  | 'grid' | 'lines' | 'triangles' | 'trihexagon'
  | 'cubic' | 'cubicsubdiv' | 'tetrahedral' | 'quarter_cubic'
  | 'concentric' | 'zigzag' | 'cross' | 'cross_3d'
  | 'gyroid' | 'lightning'

type SupportPlacement = 'everywhere' | 'touching_buildplate'

interface SavedState {
  models: ModelState[]
  selectedId: string | null
  jobName: string
  machineId: string
  profileId: string
  materialId: string
  generatedJobId: string | null
  activeTab: 'import' | 'preview'
  supportEnabled: boolean
  supportType: 'normal' | 'tree'
  supportPlacement: SupportPlacement
  infillPattern: InfillPattern
  infillDensity: number
  supportInfillPattern: InfillPattern
  supportInfillDensity: number
  adhesionType: 'none' | 'skirt' | 'brim' | 'raft'
  // G-code startup options
  gcodeHoming: boolean
  gcodeLevelling: boolean
  /** Whether this machine's custom G-code blocks are injected into the output. */
  applyCustomGCodeBlocks: boolean
  // Hybrid CNC options (only used when machine type is Hybrid)
  cncToolId: string
  machineEveryN: number
  skipMachiningLayers: number  // skip first N layers before machining starts
  machineInnerWalls: boolean
  avoidSupports: boolean
  supportClearanceMm: number
  autoMachiningFrequency: boolean
  zSafetyOffsetMm: number
  spindleRpmOverride: number | null
}

const _initState: SavedState = {
  models: [], selectedId: null, jobName: '', machineId: '', profileId: '', materialId: '',
  generatedJobId: null, activeTab: 'import', supportEnabled: false, supportType: 'normal',
  supportPlacement: 'everywhere', infillPattern: 'grid', infillDensity: 15,
  supportInfillPattern: 'grid', supportInfillDensity: 15, adhesionType: 'none',
  gcodeHoming: true, gcodeLevelling: false, applyCustomGCodeBlocks: true,
  cncToolId: '', machineEveryN: 10, skipMachiningLayers: 0, machineInnerWalls: false, avoidSupports: false,
  supportClearanceMm: 2.0, autoMachiningFrequency: false, zSafetyOffsetMm: 0,
  spindleRpmOverride: null,
}
let _saved: SavedState = { ..._initState }

// ── Module-level undo stack ───────────────────────────────────────────────────

interface UndoEntry { modelId: string; transform: ModelTransform }
let _undoStack: UndoEntry[] = []

const pushUndo = (modelId: string, transform: ModelTransform) => {
  _undoStack.push({ modelId, transform: { ...transform } })
  if (_undoStack.length > 50) _undoStack.shift()
}

let idCounter = 0
const mkId = () => `model-${++idCounter}`

// ── Component ─────────────────────────────────────────────────────────────────

export default function StlImport() {
  const viewerRef  = useRef<StlViewerHandle>(null)
  const qc = useQueryClient()

  const [models, setModels]               = useState<ModelState[]>(() => _saved.models)
  const [selectedId, setSelectedId]       = useState<string | null>(() => _saved.selectedId)
  const [isDragOver, setIsDragOver]       = useState(false)
  const [hasFaceSelected, setHasFaceSelected] = useState(false)
  const [uniformScale, setUniformScale]   = useState(true)

  const [jobName, setJobName]             = useState(() => _saved.jobName)
  const [machineId, setMachineId]         = useState(() => _saved.machineId)
  const [profileId, setProfileId]         = useState(() => _saved.profileId)
  const [materialId, setMaterialId]       = useState(() => _saved.materialId)
  const [activeTab, setActiveTab]         = useState<'import' | 'preview'>(() => _saved.activeTab)
  const [generatedJobId, setGeneratedJobId] = useState<string | null>(() => _saved.generatedJobId)
  const [supportEnabled, setSupportEnabled] = useState(() => _saved.supportEnabled)
  const [supportType, setSupportType]     = useState<'normal' | 'tree'>(() => _saved.supportType)
  const [supportPlacement, setSupportPlacement] = useState<SupportPlacement>(() => _saved.supportPlacement)
  const [infillPattern, setInfillPattern] = useState<InfillPattern>(() => _saved.infillPattern)
  const [infillDensity, setInfillDensity] = useState<number>(() => _saved.infillDensity)
  const [supportInfillPattern, setSupportInfillPattern] = useState<InfillPattern>(() => _saved.supportInfillPattern)
  const [supportInfillDensity, setSupportInfillDensity] = useState<number>(() => _saved.supportInfillDensity)
  const [adhesionType, setAdhesionType] = useState<'none' | 'skirt' | 'brim' | 'raft'>(() => _saved.adhesionType)

  // G-code startup options
  const [gcodeHoming, setGcodeHoming]         = useState(() => _saved.gcodeHoming)
  const [gcodeLevelling, setGcodeLevelling]   = useState(() => _saved.gcodeLevelling)
  const [applyCustomGCodeBlocks, setApplyCustomGCodeBlocks] = useState(() => _saved.applyCustomGCodeBlocks)

  // Hybrid CNC state (only shown when machine is Hybrid)
  const [cncToolId, setCncToolId]                       = useState(() => _saved.cncToolId)
  const [machineEveryN, setMachineEveryN]               = useState(() => _saved.machineEveryN)
  const [skipMachiningLayers, setSkipMachiningLayers]   = useState(() => _saved.skipMachiningLayers)
  const [machineInnerWalls, setMachineInnerWalls]       = useState(() => _saved.machineInnerWalls)
  const [avoidSupports, setAvoidSupports]               = useState(() => _saved.avoidSupports)
  const [supportClearanceMm, setSupportClearanceMm]     = useState(() => _saved.supportClearanceMm)
  const [autoMachiningFrequency, setAutoMachiningFrequency] = useState(() => _saved.autoMachiningFrequency)
  const [zSafetyOffsetMm, setZSafetyOffsetMm]           = useState(() => _saved.zSafetyOffsetMm)
  const [spindleRpmOverride, setSpindleRpmOverride]     = useState<number | null>(() => _saved.spindleRpmOverride)

  const [buildVolume, setBuildVolume]     = useState<BuildVolume>({ width: 220, depth: 220, height: 250 })
  const [activeBedIndex, setActiveBedIndex] = useState(0)
  const [bedLayerStep, setBedLayerStep] = useState(1)
  const [isMergingBeds, setIsMergingBeds] = useState(false)
  const [_perBedJobIds, setPerBedJobIds] = useState<Record<number, string>>({})

  // (Profile creation/editing is handled on the dedicated pages:
  //  Machine Config, Print Settings, Materials)

  // G-code preview state — per-bed for multi-bed machines
  const [bedPreviews, setBedPreviews]           = useState<Record<number, string>>({})
  const [isPreviewLoading, setIsPreviewLoading] = useState(false)
  const [previewError, setPreviewError]         = useState<string | null>(null)
  const bedFingerprintsRef = useRef<Record<number, string>>({})
  // Convenience: current bed's preview
  const previewGCode = bedPreviews[activeBedIndex] ?? null

  // Start Print state
  const [isPrinting, setIsPrinting]       = useState(false)
  const [printError, setPrintError]       = useState<string | null>(null)

  const { data: machines = [] } = useQuery({ queryKey: ['machines'], queryFn: machineProfilesApi.getAll })
  const { data: profiles = [] } = useQuery({ queryKey: ['printProfiles'], queryFn: printProfilesApi.getAll })
  const { data: materials = [] } = useQuery({ queryKey: ['materials'],     queryFn: materialsApi.getAll })
  const { data: cncTools = [] } = useQuery({ queryKey: ['cncTools'], queryFn: toolsApi.getAll })

  const deleteMachineMutation = useMutation({
    mutationFn: (id: string) => machineProfilesApi.delete(id),
    onSuccess: (_, id) => {
      qc.invalidateQueries({ queryKey: ['machines'] })
      if (machineId === id) setMachineId('')
    },
  })
  const deleteProfileMutation = useMutation({
    mutationFn: (id: string) => printProfilesApi.delete(id),
    onSuccess: (_, id) => {
      qc.invalidateQueries({ queryKey: ['printProfiles'] })
      if (profileId === id) setProfileId('')
    },
  })
  const deleteMaterialMutation = useMutation({
    mutationFn: (id: string) => materialsApi.delete(id),
    onSuccess: (_, id) => {
      qc.invalidateQueries({ queryKey: ['materials'] })
      if (materialId === id) setMaterialId('')
    },
  })

  const uploadMutation = useMutation({
    mutationFn: (fd: FormData) => jobsApi.uploadStl(fd),
  })
  const sliceMutation = useMutation({
    mutationFn: (id: string) => jobsApi.slice(id),
  })

  const { sendCommand } = useMachineConnection()
  const machineConnected = useAppStore(s => s.machineConnected)

  // ── Persist state changes to module-level ──────────────────────────────────
  useEffect(() => { _saved.models = models }, [models])
  useEffect(() => { _saved.selectedId = selectedId }, [selectedId])
  useEffect(() => { _saved.jobName = jobName }, [jobName])
  useEffect(() => { _saved.machineId = machineId }, [machineId])
  useEffect(() => { _saved.profileId = profileId }, [profileId])
  useEffect(() => { _saved.materialId = materialId }, [materialId])
  useEffect(() => { _saved.activeTab = activeTab }, [activeTab])
  useEffect(() => { _saved.generatedJobId = generatedJobId }, [generatedJobId])
  useEffect(() => { _saved.supportEnabled = supportEnabled }, [supportEnabled])
  useEffect(() => { _saved.supportType = supportType }, [supportType])
  useEffect(() => { _saved.supportPlacement = supportPlacement }, [supportPlacement])
  useEffect(() => { _saved.infillPattern = infillPattern }, [infillPattern])
  useEffect(() => { _saved.infillDensity = infillDensity }, [infillDensity])
  useEffect(() => { _saved.supportInfillPattern = supportInfillPattern }, [supportInfillPattern])
  useEffect(() => { _saved.supportInfillDensity = supportInfillDensity }, [supportInfillDensity])
  useEffect(() => { _saved.adhesionType = adhesionType }, [adhesionType])
  useEffect(() => { _saved.gcodeHoming = gcodeHoming }, [gcodeHoming])
  useEffect(() => { _saved.gcodeLevelling = gcodeLevelling }, [gcodeLevelling])
  useEffect(() => { _saved.applyCustomGCodeBlocks = applyCustomGCodeBlocks }, [applyCustomGCodeBlocks])
  useEffect(() => { _saved.cncToolId = cncToolId }, [cncToolId])
  useEffect(() => { _saved.machineEveryN = machineEveryN }, [machineEveryN])
  useEffect(() => { _saved.skipMachiningLayers = skipMachiningLayers }, [skipMachiningLayers])
  useEffect(() => { _saved.machineInnerWalls = machineInnerWalls }, [machineInnerWalls])
  useEffect(() => { _saved.avoidSupports = avoidSupports }, [avoidSupports])
  useEffect(() => { _saved.supportClearanceMm = supportClearanceMm }, [supportClearanceMm])
  useEffect(() => { _saved.autoMachiningFrequency = autoMachiningFrequency }, [autoMachiningFrequency])
  useEffect(() => { _saved.zSafetyOffsetMm = zSafetyOffsetMm }, [zSafetyOffsetMm])
  useEffect(() => { _saved.spindleRpmOverride = spindleRpmOverride }, [spindleRpmOverride])

  // Parse beds from the selected machine (handles both PascalCase and camelCase JSON)
  const selectedMachineBeds = (() => {
    const m = machines.find(m => m.id === machineId)
    if (!m) return [{ index: 0, widthMm: 220, depthMm: 220, heightMm: 250, positionXMm: 0, positionYMm: 0 }]
    try {
      const raw = JSON.parse(m.bedsJson || '[]') as any[]
      if (raw.length > 0) {
        return raw.map((b: any, i: number) => ({
          index: b.index ?? b.Index ?? i,
          widthMm: b.widthMm ?? b.WidthMm ?? m.bedWidthMm,
          depthMm: b.depthMm ?? b.DepthMm ?? m.bedDepthMm,
          heightMm: b.heightMm ?? b.HeightMm ?? m.bedHeightMm,
          positionXMm: b.positionXMm ?? b.PositionXMm ?? 0,
          positionYMm: b.positionYMm ?? b.PositionYMm ?? 0,
        }))
      }
    } catch { /* fall through */ }
    return [{ index: 0, widthMm: m.bedWidthMm, depthMm: m.bedDepthMm, heightMm: m.bedHeightMm, positionXMm: 0, positionYMm: 0 }]
  })()

  // Sync build volume from active bed
  useEffect(() => {
    if (!machineId) return
    const bed = selectedMachineBeds[activeBedIndex] ?? selectedMachineBeds[0]
    if (bed) setBuildVolume({ width: bed.widthMm, depth: bed.depthMm, height: bed.heightMm })
  }, [machineId, machines, activeBedIndex]) // eslint-disable-line react-hooks/exhaustive-deps

  // Clear face-selected badge when selection changes
  useEffect(() => { setHasFaceSelected(false) }, [selectedId])

  // ── Keyboard shortcuts ────────────────────────────────────────────────────
  useEffect(() => {
    const handler = (e: KeyboardEvent) => {
      const tag = (document.activeElement as HTMLElement)?.tagName
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return

      if (e.key === 'Delete' || e.key === 'Backspace') {
        if (selectedId) removeModel(selectedId)
        return
      }
      if ((e.ctrlKey || e.metaKey) && e.key === 'z') {
        e.preventDefault()
        const entry = _undoStack.pop()
        if (entry) {
          setModels(prev => prev.map(m => m.id === entry.modelId
            ? { ...m, transform: { ...entry.transform } } : m))
        }
      }
    }
    window.addEventListener('keydown', handler)
    return () => window.removeEventListener('keydown', handler)
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedId])

  // ── Model helpers ──────────────────────────────────────────────────────────

  const selectedModel   = models.find(m => m.id === selectedId) ?? null
  const selectedMachine = machines.find(m => m.id === machineId)
  const selectedProfile = profiles.find(p => p.id === profileId)
  const isHybrid = selectedMachine?.type === 'Hybrid'
  const selectedCncTool = cncTools.find(t => t.id === cncToolId)

  const addFile = useCallback((f: File) => {
    if (!f.name.toLowerCase().endsWith('.stl')) return
    const id  = mkId()
    const url = URL.createObjectURL(f)
    const entry: ModelState = {
      id, file: f, url,
      name: f.name.replace(/\.stl$/i, ''),
      transform: { ...DEFAULT_TRANSFORM },
      size: null,
      isOutOfBounds: false,
      bedIndex: activeBedIndex,
    }
    setModels(prev => {
      if (prev.length === 0) setJobName(entry.name)
      return [...prev, entry]
    })
    setSelectedId(id)
  }, [activeBedIndex])

  const removeModel = (id: string) => {
    setModels(prev => {
      const next = prev.filter(m => m.id !== id)
      URL.revokeObjectURL(prev.find(m => m.id === id)?.url ?? '')
      return next
    })
    if (selectedId === id) setSelectedId(models.find(m => m.id !== id)?.id ?? null)
    _undoStack = _undoStack.filter(e => e.modelId !== id)
  }

  // Track time of last undo push per model (to batch drag transforms)
  const lastUndoPushTimeRef = useRef<Record<string, number>>({})

  // Throttle React state updates during drag — the 3D viewer handles its own
  // rendering internally. We only sync React state every 200ms to avoid
  // re-rendering the entire 1700-line component on every mouse move frame.
  const pendingTransformRef = useRef<{ id: string; t: ModelTransform } | null>(null)
  const transformTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)

  const updateTransform = useCallback((id: string, t: ModelTransform) => {
    pendingTransformRef.current = { id, t }
    if (transformTimerRef.current) return // already scheduled
    transformTimerRef.current = setTimeout(() => {
      transformTimerRef.current = null
      const pending = pendingTransformRef.current
      if (!pending) return
      pendingTransformRef.current = null
      const now = Date.now()
      const last = lastUndoPushTimeRef.current[pending.id] ?? 0
      setModels(prev => {
        if (now - last > 400) {
          const old = prev.find(m => m.id === pending.id)
          if (old) pushUndo(pending.id, old.transform)
          lastUndoPushTimeRef.current[pending.id] = now
        }
        return prev.map(m => m.id === pending.id ? { ...m, transform: pending.t } : m)
      })
    }, 200)
  }, [])

  const patchSelected = (patch: Partial<ModelTransform>) => {
    if (!selectedModel) return
    pushUndo(selectedModel.id, selectedModel.transform)
    const next = { ...selectedModel.transform, ...patch }
    setModels(prev => prev.map(m => m.id === selectedModel.id ? { ...m, transform: next } : m))
  }

  const patchScale = (axis: 'scaleX' | 'scaleY' | 'scaleZ', val: number) => {
    if (!selectedModel) return
    pushUndo(selectedModel.id, selectedModel.transform)
    const base = selectedModel.transform
    const next = uniformScale
      ? { ...base, scaleX: val, scaleY: val, scaleZ: val }
      : { ...base, [axis]: val }
    setModels(prev => prev.map(m => m.id === selectedModel.id ? { ...m, transform: next } : m))
  }

  // ── Viewer callbacks ───────────────────────────────────────────────────────

  const handleModelLoaded = useCallback((id: string, size: { x: number; y: number; z: number }) => {
    setModels(prev => prev.map(m => m.id === id ? { ...m, size } : m))
  }, [])

  const pendingSizeRef = useRef<{ id: string; size: { x: number; y: number; z: number } } | null>(null)
  const sizeTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const handleSizeChange = useCallback((id: string, size: { x: number; y: number; z: number }) => {
    pendingSizeRef.current = { id, size }
    if (sizeTimerRef.current) return
    sizeTimerRef.current = setTimeout(() => {
      sizeTimerRef.current = null
      const p = pendingSizeRef.current
      if (p) { pendingSizeRef.current = null; setModels(prev => prev.map(m => m.id === p.id ? { ...m, size: p.size } : m)) }
    }, 200)
  }, [])

  const pendingBoundsRef = useRef<{ id: string; out: boolean } | null>(null)
  const boundsTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const handleBoundsChange = useCallback((id: string, out: boolean) => {
    pendingBoundsRef.current = { id, out }
    if (boundsTimerRef.current) return
    boundsTimerRef.current = setTimeout(() => {
      boundsTimerRef.current = null
      const p = pendingBoundsRef.current
      if (p) { pendingBoundsRef.current = null; setModels(prev => prev.map(m => m.id === p.id ? { ...m, isOutOfBounds: p.out } : m)) }
    }, 200)
  }, [])

  // ── Drop / input ───────────────────────────────────────────────────────────

  const handleDrop = useCallback((e: React.DragEvent) => {
    e.preventDefault()
    setIsDragOver(false)
    Array.from(e.dataTransfer.files).forEach(f => addFile(f))
  }, [addFile])

  const handleFileInput = (e: React.ChangeEvent<HTMLInputElement>) => {
    Array.from(e.target.files ?? []).forEach(f => addFile(f))
    e.target.value = ''
  }

  // ── Submit (Generate G-code) ───────────────────────────────────────────────

  const handleSubmit = async () => {
    if (!machineId || !profileId || !materialId || !jobName) return

    if (selectedMachineBeds.length > 1) {
      // Multi-bed: slice each bed separately, then merge into one final file
      setIsMergingBeds(true)
      try {
        const jobIds: string[] = []
        for (let bi = 0; bi < selectedMachineBeds.length; bi++) {
          const bedModels = models.filter(m => (m.bedIndex ?? 0) === bi)
          if (bedModels.length === 0) continue
          const primary = bedModels[0]
          const fd = new FormData()
          const transformedFile = await buildTransformedStlBlob(primary.file, primary.transform)
          fd.append('file', transformedFile, primary.file.name)
          fd.append('jobName', `${jobName}_bed${bi + 1}`)
          fd.append('machineProfileId', machineId)
          fd.append('printProfileId', profileId)
          fd.append('materialId', materialId)
          fd.append('supportEnabled', supportEnabled.toString())
          fd.append('supportType', supportType)
          fd.append('supportPlacement', supportPlacement)
          fd.append('infillPattern', infillPattern)
          fd.append('infillDensityPct', infillDensity.toString())
          fd.append('supportInfillPattern', supportInfillPattern)
          fd.append('supportInfillDensityPct', supportInfillDensity.toString())
          fd.append('adhesionType', adhesionType)
          fd.append('gcodeHoming', gcodeHoming.toString())
          fd.append('gcodeLevelling', gcodeLevelling.toString())
          fd.append('applyCustomGCodeBlocks', applyCustomGCodeBlocks.toString())
          fd.append('bedIndex', bi.toString())
          const { jobId } = await jobsApi.uploadStl(fd)
          await jobsApi.slice(jobId)
          jobIds.push(jobId)
          setPerBedJobIds(prev => ({ ...prev, [bi]: jobId }))
        }

        const useHybrid = isHybrid && !!cncToolId

        if (jobIds.length >= 2) {
          // Multi-bed merge — backend auto-generates CNC toolpaths per bed when hybrid
          const mergeResult = await jobsApi.mergeBeds(
            jobIds, useHybrid ? machineEveryN : bedLayerStep, jobName,
            useHybrid,
            useHybrid ? {
              cncToolId, machineEveryNLayers: machineEveryN,
              machineInnerWalls, avoidSupports, supportClearanceMm,
              autoMachiningFrequency, zSafetyOffsetMm, spindleRpmOverride,
            } : undefined,
          )
          qc.invalidateQueries({ queryKey: ['jobs'] })
          setGeneratedJobId(mergeResult.jobId)
        } else if (jobIds.length === 1) {
          // Only 1 bed had models — still generate CNC + merge for hybrid
          if (useHybrid) {
            await jobsApi.generateToolpaths(
              jobIds[0], cncToolId, machineEveryN,
              machineInnerWalls, avoidSupports, supportClearanceMm,
              autoMachiningFrequency, zSafetyOffsetMm, spindleRpmOverride,
              0, 0, null, 0, 0, null, skipMachiningLayers,
            )
            await jobsApi.planHybrid(jobIds[0], machineEveryN)
          }
          qc.invalidateQueries({ queryKey: ['jobs'] })
          setGeneratedJobId(jobIds[0])
        }
        setActiveTab('preview')
      } finally {
        setIsMergingBeds(false)
      }
    } else {
      // Single bed: existing flow
      const primary = selectedModel ?? models[0]
      if (!primary) return
      const fd = new FormData()
      const transformedFile = await buildTransformedStlBlob(primary.file, primary.transform)
      fd.append('file', transformedFile, primary.file.name)
      fd.append('jobName', jobName)
      fd.append('machineProfileId', machineId)
      fd.append('printProfileId', profileId)
      fd.append('materialId', materialId)
      fd.append('supportEnabled', supportEnabled.toString())
      fd.append('supportType', supportType)
      fd.append('supportPlacement', supportPlacement)
      fd.append('infillPattern', infillPattern)
      fd.append('infillDensityPct', infillDensity.toString())
      fd.append('supportInfillPattern', supportInfillPattern)
      fd.append('supportInfillDensityPct', supportInfillDensity.toString())
      fd.append('adhesionType', adhesionType)
      fd.append('gcodeHoming', gcodeHoming.toString())
      fd.append('gcodeLevelling', gcodeLevelling.toString())
      fd.append('applyCustomGCodeBlocks', applyCustomGCodeBlocks.toString())
      const { jobId } = await uploadMutation.mutateAsync(fd)
      await sliceMutation.mutateAsync(jobId)

      // For hybrid single-bed: also generate toolpaths and merge into a
      // single hybrid.gcode (multi-bed handles this server-side via mergeBeds).
      // Without plan-hybrid, HybridGCodePath stays null and the "Download
      // Hybrid G-code" link returns 400.
      if (isHybrid && cncToolId) {
        await jobsApi.generateToolpaths(
          jobId, cncToolId, machineEveryN,
          machineInnerWalls, avoidSupports, supportClearanceMm,
          autoMachiningFrequency, zSafetyOffsetMm, spindleRpmOverride,
          0, 0, null, 0, 0, null, skipMachiningLayers,
        )
        await jobsApi.planHybrid(jobId, machineEveryN)
      }

      qc.invalidateQueries({ queryKey: ['jobs'] })
      setGeneratedJobId(jobId)
      setActiveTab('preview')
    }
  }

  // ── Start Print ────────────────────────────────────────────────────────────

  const handleStartPrint = async () => {
    if (!generatedJobId || !machineConnected) return
    setIsPrinting(true)
    setPrintError(null)
    try {
      const gcode = await jobsApi.getPrintGCode(generatedJobId)
      const lines = gcode.split('\n').filter(l => l.trim() && !l.startsWith(';'))
      for (const line of lines) {
        await sendCommand(line)
      }
    } catch (err) {
      setPrintError(err instanceof Error ? err.message : 'Print failed')
    } finally {
      setIsPrinting(false)
    }
  }

  // ── Preview (slice → fetch G-code → render in viewer) ─────────────────────

  const handlePreview = async () => {
    const bedModels = models.filter(m => (m.bedIndex ?? 0) === activeBedIndex)
    const primary = bedModels.find(m => m.id === selectedId) ?? bedModels[0]
    if (!primary || !machineId || !profileId || !materialId) return
    setIsPreviewLoading(true)
    setPreviewError(null)
    setBedPreviews(prev => { const n = { ...prev }; delete n[activeBedIndex]; return n })
    const fp = bedFingerprint(activeBedIndex)
    let previewJobId: string | null = null
    try {
      const fd = new FormData()
      const transformedFile = await buildTransformedStlBlob(primary.file, primary.transform)
      fd.append('file', transformedFile, primary.file.name)
      fd.append('jobName', `${jobName || primary.name}_preview`)
      fd.append('machineProfileId', machineId)
      fd.append('printProfileId', profileId)
      fd.append('materialId', materialId)
      fd.append('supportEnabled', supportEnabled.toString())
      fd.append('supportType', supportType)
      fd.append('supportPlacement', supportPlacement)
      fd.append('infillPattern', infillPattern)
      fd.append('infillDensityPct', infillDensity.toString())
      fd.append('supportInfillPattern', supportInfillPattern)
      fd.append('supportInfillDensityPct', supportInfillDensity.toString())
      fd.append('adhesionType', adhesionType)
      fd.append('gcodeHoming', gcodeHoming.toString())
      fd.append('gcodeLevelling', gcodeLevelling.toString())
      fd.append('applyCustomGCodeBlocks', applyCustomGCodeBlocks.toString())
      if (primary.bedIndex != null && selectedMachineBeds.length > 1)
        fd.append('bedIndex', primary.bedIndex.toString())
      const { jobId } = await jobsApi.uploadStl(fd)
      previewJobId = jobId
      await jobsApi.slice(jobId)
      const gcode = await jobsApi.getPrintGCode(jobId)
      bedFingerprintsRef.current[activeBedIndex] = fp
      setBedPreviews(prev => ({ ...prev, [activeBedIndex]: gcode }))
      setActiveTab('preview')
    } catch (err) {
      const msg = (err as any)?.response?.data?.detail
        ?? (err as any)?.response?.data?.message
        ?? (err instanceof Error ? err.message : 'Preview failed')
      setPreviewError(msg)
    } finally {
      setIsPreviewLoading(false)
      if (previewJobId) jobsApi.deleteJob(previewJobId).catch(() => {})
    }
  }

  const canSubmit  = models.length > 0 && !!machineId && !!profileId && !!materialId && !!jobName
    && !uploadMutation.isPending && !sliceMutation.isPending && !isMergingBeds
  const canPreview = models.length > 0 && !!machineId && !!profileId && !!materialId
    && !isPreviewLoading && !uploadMutation.isPending && !sliceMutation.isPending
  const anyOOB     = models.some(m => m.isOutOfBounds)

  const effectiveSize = selectedModel?.size ?? null

  // Per-bed fingerprint — only the models on that bed + global settings
  const bedFingerprint = (bi: number) => {
    const bedModels = models.filter(m => (m.bedIndex ?? 0) === bi)
    if (bedModels.length === 0 || !machineId || !profileId) return ''
    return JSON.stringify({
      models: bedModels.map(m => ({ id: m.id, t: m.transform })),
      machineId, profileId, materialId, supportEnabled, supportType, supportPlacement,
      infillPattern, infillDensity, supportInfillPattern, supportInfillDensity, adhesionType, bi,
    })
  }
  const currentFingerprint = bedFingerprint(activeBedIndex)

  // Clear only the active bed's preview when its inputs change
  useEffect(() => {
    const fp = bedFingerprint(activeBedIndex)
    if (fp !== bedFingerprintsRef.current[activeBedIndex]) {
      setBedPreviews(prev => { const n = { ...prev }; delete n[activeBedIndex]; return n })
      setPreviewError(null)
    }
  }, [currentFingerprint, activeBedIndex]) // eslint-disable-line react-hooks/exhaustive-deps


  // ── Render ─────────────────────────────────────────────────────────────────

  return (
    <div className="flex flex-col gap-0 h-[calc(100vh-8rem)]">
      <div className="flex border-b border-gray-800 mb-4 flex-shrink-0">
        <TabBtn active={activeTab === 'import'} onClick={() => setActiveTab('import')}>Import STL</TabBtn>
        <DisabledHint when={Object.keys(bedPreviews).length === 0 && !generatedJobId} reason="Slice or generate G-code first to see a preview.">
          <TabBtn
            active={activeTab === 'preview'}
            disabled={Object.keys(bedPreviews).length === 0 && !generatedJobId}
            onClick={() => setActiveTab('preview')}
          >
            G-code Preview
          </TabBtn>
        </DisabledHint>
      </div>

      {activeTab === 'preview' && (Object.keys(bedPreviews).length > 0 || generatedJobId) ? (
        <div className="flex flex-col gap-3 flex-1 min-h-0">
          {/* Bed preview tabs for multi-bed (hidden for hybrid — combined view) */}
          {selectedMachineBeds.length > 1 && !(generatedJobId && isHybrid) && (
            <div className="flex gap-1 flex-shrink-0">
              {selectedMachineBeds.map((_: any, bi: number) => (
                <button key={bi} onClick={() => setActiveBedIndex(bi)}
                  className={`px-3 py-1 text-xs rounded-lg border transition ${
                    bi === activeBedIndex
                      ? 'bg-indigo-800 border-indigo-500 text-white'
                      : bedPreviews[bi]
                        ? 'bg-gray-800 border-gray-600 text-gray-300'
                        : 'bg-gray-900 border-gray-800 text-gray-600'
                  }`}>
                  Bed {bi + 1} {bedPreviews[bi] ? '✓' : ''}
                </button>
              ))}
            </div>
          )}
          <div className="flex items-center gap-3 flex-shrink-0">
            {generatedJobId ? (
              <>
                {machineConnected ? (
                  <button
                    onClick={handleStartPrint}
                    disabled={isPrinting}
                    className="px-4 py-2 text-sm rounded-lg bg-green-700/80 hover:bg-green-700 disabled:opacity-40
                               text-white font-medium transition border border-green-600"
                  >
                    {isPrinting ? 'Printing…' : 'Start Print'}
                  </button>
                ) : (
                  <span className="text-xs text-gray-500 italic">Machine not connected</span>
                )}
                <Link
                  to={`/jobs/${generatedJobId}/gcode`}
                  className="px-3 py-1.5 text-xs rounded-lg bg-gray-800 hover:bg-gray-700 text-gray-300 border border-gray-700 transition"
                >
                  View Full G-code
                </Link>
                <a
                  href={`/api/jobs/${generatedJobId}/print-gcode/download`}
                  download={`${jobName || 'part'}_extrusion.gcode`}
                  className="px-3 py-1.5 text-xs rounded-lg bg-blue-900/60 hover:bg-blue-800 text-blue-200 border border-blue-700 transition"
                >
                  Download Extrusion G-code
                </a>
                {isHybrid && (
                  <a
                    href={`/api/jobs/${generatedJobId}/gcode`}
                    download={`${jobName || 'part'}_hybrid.gcode`}
                    className="px-3 py-1.5 text-xs rounded-lg bg-purple-900/60 hover:bg-purple-800 text-purple-200 border border-purple-700 transition"
                  >
                    Download Hybrid G-code
                  </a>
                )}
                <Link
                  to="/hybrid-preview"
                  className="px-3 py-1.5 text-xs rounded-lg bg-cyan-900/80 hover:bg-cyan-800 text-cyan-200 border border-cyan-700 transition"
                >
                  Hybrid Preview →
                </Link>
              </>
            ) : (
              <span className="text-xs text-amber-400/80">Preview only — click &ldquo;Generate G-code&rdquo; to save this job</span>
            )}
            {printError && <span className="text-red-400 text-xs">{printError}</span>}
          </div>
          {generatedJobId
            ? <GCodePreview jobId={generatedJobId} buildVolume={buildVolume} lineWidth={selectedProfile?.nozzleDiameterMm || 0.4}
                travelX={selectedMachine?.travelXMm} travelY={selectedMachine?.travelYMm} travelZ={selectedMachine?.bedHeightMm}
                originX={selectedMachine?.originXMm} originY={selectedMachine?.originYMm}
                beds={selectedMachineBeds} originIsBedCenter />
            : <GCodePreviewInline gcode={previewGCode!} buildVolume={buildVolume} lineWidth={selectedProfile?.nozzleDiameterMm || 0.4}
                travelX={selectedMachine?.travelXMm} travelY={selectedMachine?.travelYMm} travelZ={selectedMachine?.bedHeightMm}
                originX={selectedMachine?.originXMm} originY={selectedMachine?.originYMm}
                beds={selectedMachineBeds}
                originIsBedCenter />
          }
        </div>
      ) : (
      <div className="grid grid-cols-2 gap-6 flex-1 min-h-0">

      {/* ── Left: STL Viewer + G-code Preview ──────────────────────────── */}
      <div className="flex flex-col gap-3 min-h-0">

      {/* Bed selector above viewer for multi-bed */}
      {selectedMachineBeds.length > 1 && (
        <div className="flex gap-1 flex-shrink-0">
          {selectedMachineBeds.map((_: any, bi: number) => {
            const bedModels = models.filter(m => (m.bedIndex ?? 0) === bi)
            return (
              <button key={bi} onClick={() => setActiveBedIndex(bi)}
                className={`flex-1 px-2 py-1.5 text-xs rounded-lg border transition ${
                  bi === activeBedIndex
                    ? 'bg-indigo-800 border-indigo-500 text-white'
                    : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'
                }`}>
                Bed {bi + 1}
                <span className="ml-1 text-gray-500">
                  {(selectedMachineBeds[bi] as any)?.widthMm ?? '?'}×{(selectedMachineBeds[bi] as any)?.depthMm ?? '?'}
                </span>
                <span className="ml-1 text-gray-600">({bedModels.length})</span>
              </button>
            )
          })}
        </div>
      )}

      {/* STL 3D viewer */}
      <div
        className={`bg-gray-900 rounded-xl overflow-hidden relative border-2 transition-colors flex-1 min-h-0
          ${isDragOver ? 'border-primary border-dashed' : 'border-gray-700 border-dashed'}`}
        onDrop={handleDrop}
        onDragOver={e => { e.preventDefault(); setIsDragOver(true) }}
        onDragLeave={() => setIsDragOver(false)}
      >
        {(() => {
          const bedModelsForViewer = selectedMachineBeds.length > 1
            ? models.filter(m => (m.bedIndex ?? 0) === activeBedIndex)
            : models
          return bedModelsForViewer.length > 0 ? (
          <>
            <StlViewer
              ref={viewerRef}
              models={bedModelsForViewer}
              selectedId={selectedId}
              className="w-full h-full"
              buildVolume={buildVolume}
              onModelLoaded={handleModelLoaded}
              onSizeChange={handleSizeChange}
              onBoundsChange={handleBoundsChange}
              onTransformChange={updateTransform}
              onModelSelect={setSelectedId}
              onFaceSelected={setHasFaceSelected}
            />

            {/* Overlay badges */}
            <div className="absolute top-3 left-3 flex flex-col gap-2 pointer-events-none select-none">
              {/* Active bed indicator for multi-bed */}
              {selectedMachineBeds.length > 1 && (
                <div className="bg-indigo-900/85 text-indigo-200 text-xs px-3 py-1.5 rounded-lg backdrop-blur-sm">
                  Bed {activeBedIndex + 1} — {buildVolume.width}×{buildVolume.depth}×{buildVolume.height} mm
                </div>
              )}
              {anyOOB && (
                <div className="bg-red-900/85 text-red-200 text-xs px-3 py-1.5 rounded-lg backdrop-blur-sm">
                  Model outside build volume
                </div>
              )}
              {hasFaceSelected && (
                <div className="bg-orange-900/85 text-orange-200 text-xs px-3 py-1.5 rounded-lg backdrop-blur-sm">
                  Face selected — click &ldquo;Place Face on Bed&rdquo;
                </div>
              )}
              {effectiveSize && (
                <div className="bg-gray-900/80 text-gray-300 text-xs px-3 py-1.5 rounded-lg backdrop-blur-sm">
                  {effectiveSize.x.toFixed(1)} × {effectiveSize.y.toFixed(1)} × {effectiveSize.z.toFixed(1)} mm
                </div>
              )}
            </div>

            {/* Hints */}
            <div className="absolute bottom-3 left-3 pointer-events-none select-none">
              <p className="text-gray-600 text-xs">
                Click to select · Dbl-click for transform handles · Delete to remove · Ctrl+Z to undo
              </p>
            </div>

            {/* Add model button */}
            <label className="absolute bottom-3 right-3 cursor-pointer text-xs px-3 py-1.5 rounded-lg
                              bg-gray-800/80 hover:bg-gray-700/90 text-gray-400 hover:text-gray-200
                              transition backdrop-blur-sm">
              + Add Model
              <input type="file" accept=".stl" multiple className="hidden" onChange={handleFileInput} />
            </label>
          </>
        ) : (
          <div className="absolute inset-0 flex flex-col items-center justify-center text-gray-500 gap-3 select-none">
            <svg className="w-14 h-14 opacity-40" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1}
                d="M20 7l-8-4-8 4m16 0l-8 4m8-4v10l-8 4m0-10L4 7m8 4v10" />
            </svg>
            <p className="text-sm font-medium">Drag &amp; drop STL here{selectedMachineBeds.length > 1 ? ` (Bed ${activeBedIndex + 1})` : ''}</p>
            <label className="cursor-pointer text-xs px-4 py-2 rounded-lg bg-gray-800 hover:bg-gray-700 transition text-gray-300">
              or Browse files
              <input type="file" accept=".stl" multiple className="hidden" onChange={handleFileInput} />
            </label>
          </div>
        )
        })()}
      </div>

      {/* Slicing status indicator (while slicing for preview) */}
      {isPreviewLoading && (
        <div className="bg-gray-950 rounded-xl border border-gray-700 flex-shrink-0 h-10 flex items-center justify-center gap-2 text-gray-500 text-sm">
          <svg className="w-4 h-4 animate-spin" fill="none" viewBox="0 0 24 24">
            <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" />
            <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8v8H4z" />
          </svg>
          Slicing…
        </div>
      )}
      {previewError && (
        <div className="bg-red-950/40 rounded-xl border border-red-800 flex-shrink-0 px-4 py-2 text-red-400 text-xs">
          {previewError}
        </div>
      )}

      </div>{/* end left flex column */}

      {/* ── Right: Config Panel ──────────────────────────────────────────── */}
      <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 overflow-y-auto flex flex-col gap-5">

        {/* Job Setup */}
        <section className="space-y-3">
          <SectionHeader>Job Setup</SectionHeader>
          <Field label="Job Name">
            <input className="input" value={jobName} onChange={e => setJobName(e.target.value)} placeholder="My part…" />
          </Field>

          {/* Machine Configuration */}
          <Field label="Machine">
            <div className="flex gap-1.5">
              <select className="input flex-1" value={machineId} onChange={e => setMachineId(e.target.value)}>
                <option value="">Select…</option>
                {machines.map(m => <option key={m.id} value={m.id}>{m.name}</option>)}
              </select>
              <Link to="/machine-config"
                className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-400 hover:text-gray-200 transition"
                title="Open Machine Configuration"
              >Manage</Link>
              {machineId && (
                <button
                  onClick={() => { if (confirm('Delete this machine configuration?')) deleteMachineMutation.mutate(machineId) }}
                  className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-red-900/60 border border-gray-700 text-gray-400 hover:text-red-300 transition"
                  title="Delete"
                >✕</button>
              )}
            </div>
          </Field>

          <Field label="Print Settings">
            <div className="flex gap-1.5">
              <select className="input flex-1" value={profileId} onChange={e => setProfileId(e.target.value)}>
                <option value="">Select…</option>
                {profiles.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}
              </select>
              <Link to="/print-settings"
                className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-400 hover:text-gray-200 transition"
                title="Open Print Settings"
              >Manage</Link>
              {profileId && (
                <button
                  onClick={() => { if (confirm('Delete this print settings profile?')) deleteProfileMutation.mutate(profileId) }}
                  className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-red-900/60 border border-gray-700 text-gray-400 hover:text-red-300 transition"
                  title="Delete"
                >✕</button>
              )}
            </div>
          </Field>
          <Field label="Material">
            <div className="flex gap-1.5">
              <select className="input flex-1" value={materialId} onChange={e => setMaterialId(e.target.value)}>
                <option value="">Select…</option>
                {materials.map(m => <option key={m.id} value={m.id}>{m.name} ({m.type})</option>)}
              </select>
              <Link to="/materials"
                className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-gray-700 border border-gray-700 text-gray-400 hover:text-gray-200 transition"
                title="Open Materials"
              >Manage</Link>
              {materialId && (
                <button
                  onClick={() => { if (confirm('Delete this material?')) deleteMaterialMutation.mutate(materialId) }}
                  className="px-2 py-1 text-xs rounded bg-gray-800 hover:bg-red-900/60 border border-gray-700 text-gray-400 hover:text-red-300 transition"
                  title="Delete"
                >✕</button>
              )}
            </div>
          </Field>
        </section>

        {/* Active profile settings summary */}
        {(selectedProfile || selectedMachine) && (
          <div className="flex flex-wrap gap-1.5 text-xs">
            {selectedMachine && (
              <span className="bg-gray-800 border border-gray-700 rounded px-2 py-0.5 text-gray-400">
                {selectedMachine.bedWidthMm}×{selectedMachine.bedDepthMm}×{selectedMachine.bedHeightMm} mm
              </span>
            )}
            {selectedProfile && (
              <>
                <span className="bg-gray-800 border border-gray-700 rounded px-2 py-0.5 text-gray-400">
                  Layer {selectedProfile.layerHeightMm} mm
                </span>
                <span className="bg-gray-800 border border-gray-700 rounded px-2 py-0.5 text-gray-400">
                  {selectedProfile.printSpeedMmS} mm/s
                </span>
                <span className="bg-gray-800 border border-gray-700 rounded px-2 py-0.5 text-gray-400">
                  Flow {selectedProfile.materialFlowPct ?? 100}%
                </span>
                <span className="bg-gray-800 border border-gray-700 rounded px-2 py-0.5 text-gray-400">
                  {selectedProfile.printTemperatureDegC}°C / {selectedProfile.bedTemperatureDegC}°C
                </span>
              </>
            )}
          </div>
        )}

        <Divider />

        {/* Bed Adhesion */}
        <section className="space-y-2">
          <SectionHeader>Bed Adhesion</SectionHeader>
          <Field label="Type">
            <select className="input" value={adhesionType}
              onChange={e => setAdhesionType(e.target.value as 'none' | 'skirt' | 'brim' | 'raft')}>
              <option value="none">None</option>
              <option value="skirt">Skirt</option>
              <option value="brim">Brim</option>
              <option value="raft">Raft</option>
            </select>
          </Field>
        </section>

        <Divider />

        {/* Support Settings */}
        <section className="space-y-2">
          <SectionHeader>Support</SectionHeader>
          <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
            <input
              type="checkbox"
              checked={supportEnabled}
              onChange={e => setSupportEnabled(e.target.checked)}
              className="accent-primary"
            />
            Enable Support
          </label>
          {supportEnabled && (
            <>
              <div className="space-y-1 mt-1">
                <label className="text-xs text-gray-400">Structure</label>
                <div className="flex gap-3">
                  {(['normal', 'tree'] as const).map(t => (
                    <label key={t} className="flex items-center gap-1.5 text-sm text-gray-300 cursor-pointer">
                      <input
                        type="radio"
                        name="supportType"
                        value={t}
                        checked={supportType === t}
                        onChange={() => setSupportType(t)}
                        className="accent-primary"
                      />
                      {t.charAt(0).toUpperCase() + t.slice(1)}
                    </label>
                  ))}
                </div>
              </div>
              <div className="space-y-1">
                <label className="text-xs text-gray-400">Placement</label>
                <div className="flex gap-3">
                  <label className="flex items-center gap-1.5 text-sm text-gray-300 cursor-pointer">
                    <input
                      type="radio"
                      name="supportPlacement"
                      value="everywhere"
                      checked={supportPlacement === 'everywhere'}
                      onChange={() => setSupportPlacement('everywhere')}
                      className="accent-primary"
                    />
                    Everywhere
                  </label>
                  <label className="flex items-center gap-1.5 text-sm text-gray-300 cursor-pointer">
                    <input
                      type="radio"
                      name="supportPlacement"
                      value="touching_buildplate"
                      checked={supportPlacement === 'touching_buildplate'}
                      onChange={() => setSupportPlacement('touching_buildplate')}
                      className="accent-primary"
                    />
                    Touching Buildplate
                  </label>
                </div>
              </div>
              <Field label="Support Infill Density">
                <div className="flex items-center gap-2">
                  <input
                    type="range"
                    min={0} max={100} step={1}
                    value={supportInfillDensity}
                    onChange={e => setSupportInfillDensity(+e.target.value)}
                    className="flex-1 accent-primary"
                  />
                  <div className="flex items-center gap-1">
                    <input
                      type="number"
                      min={0} max={100} step={1}
                      value={supportInfillDensity}
                      onChange={e => {
                        const v = Math.min(100, Math.max(0, +e.target.value))
                        setSupportInfillDensity(isNaN(v) ? 15 : v)
                      }}
                      className="input w-16 text-center"
                    />
                    <span className="text-xs text-gray-500">%</span>
                  </div>
                </div>
              </Field>
              <Field label="Support Infill Pattern">
                <select className="input" value={supportInfillPattern} onChange={e => setSupportInfillPattern(e.target.value as InfillPattern)}>
                  <option value="grid">Grid</option>
                  <option value="lines">Lines</option>
                  <option value="triangles">Triangles</option>
                  <option value="zigzag">Zig-Zag</option>
                  <option value="concentric">Concentric</option>
                  <option value="cross">Cross</option>
                  <option value="gyroid">Gyroid</option>
                </select>
              </Field>
            </>
          )}
        </section>

        <Divider />

        {/* Infill */}
        <section className="space-y-2">
          <SectionHeader>Infill</SectionHeader>
          <Field label="Density">
            <div className="flex items-center gap-2">
              <input
                type="range"
                min={0} max={100} step={1}
                value={infillDensity}
                onChange={e => setInfillDensity(+e.target.value)}
                className="flex-1 accent-primary"
              />
              <div className="flex items-center gap-1">
                <input
                  type="number"
                  min={0} max={100} step={1}
                  value={infillDensity}
                  onChange={e => {
                    const v = Math.min(100, Math.max(0, +e.target.value))
                    setInfillDensity(isNaN(v) ? 15 : v)
                  }}
                  className="input w-16 text-center"
                />
                <span className="text-xs text-gray-500">%</span>
              </div>
            </div>
            <div className="flex justify-between text-xs text-gray-600 mt-0.5">
              <span>0%</span><span>50%</span><span>100%</span>
            </div>
          </Field>
          <Field label="Pattern">
            <select className="input" value={infillPattern} onChange={e => setInfillPattern(e.target.value as InfillPattern)}>
              <option value="grid">Grid</option>
              <option value="lines">Lines</option>
              <option value="triangles">Triangles</option>
              <option value="trihexagon">Tri-Hexagon</option>
              <option value="cubic">Cubic</option>
              <option value="cubicsubdiv">Cubic Subdivision</option>
              <option value="tetrahedral">Octet</option>
              <option value="quarter_cubic">Quarter Cubic</option>
              <option value="concentric">Concentric</option>
              <option value="zigzag">Zig-Zag</option>
              <option value="cross">Cross</option>
              <option value="cross_3d">Cross 3D</option>
              <option value="gyroid">Gyroid</option>
              <option value="lightning">Lightning</option>
            </select>
          </Field>
        </section>

        <Divider />

        {/* G-code Startup */}
        <section className="space-y-2">
          <SectionHeader>G-code Startup</SectionHeader>
          <div className="space-y-2">
            <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
              <input type="checkbox" checked={gcodeHoming}
                onChange={e => setGcodeHoming(e.target.checked)} className="accent-primary" />
              Home all axes (G28)
            </label>
            <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
              <input type="checkbox" checked={gcodeLevelling}
                onChange={e => setGcodeLevelling(e.target.checked)} className="accent-primary" />
              Bed levelling (G29)
            </label>
          </div>
          <p className="text-[10px] text-gray-600">
            Selected commands are inserted at the start of the G-code, replacing Cura defaults.
          </p>
        </section>

        <Divider />

        {/* Custom G-code blocks opt-in */}
        <section className="space-y-2">
          <SectionHeader>Custom G-code Blocks</SectionHeader>
          <label className="flex items-start gap-2 text-sm text-gray-300 cursor-pointer">
            <input
              type="checkbox"
              checked={applyCustomGCodeBlocks}
              onChange={e => setApplyCustomGCodeBlocks(e.target.checked)}
              className="accent-primary mt-0.5"
            />
            <span className="flex items-center gap-1.5">
              Apply custom G-code blocks
              <InfoTip text="When ticked, the active blocks defined for this machine on the G-code Customisation page — plus any shared blocks — are injected into the generated G-code. Untick to slice this job without them." />
            </span>
          </label>
          <p className="text-[10px] text-gray-600">
            {applyCustomGCodeBlocks
              ? 'This machine’s active blocks (and shared blocks) will be included in the output.'
              : 'No custom blocks will be injected for this job. Blocks stay defined; they are just skipped here.'}
          </p>
        </section>

        {/* Multi-bed sequencing (only for multi-bed machines) */}
        {selectedMachineBeds.length > 1 && (
          <>
            <Divider />
            <section className="space-y-2">
              <SectionHeader>Multi-Bed Sequencing</SectionHeader>
              <Field label="Layer step (layers per bed before switching)">
                <div className="flex items-center gap-2">
                  <input type="range" min={1} max={50} step={1} value={bedLayerStep}
                    onChange={e => setBedLayerStep(+e.target.value)} className="flex-1 accent-primary" />
                  <input type="number" min={1} max={100} value={bedLayerStep}
                    onChange={e => { const v = Math.max(1, +e.target.value); setBedLayerStep(isNaN(v) ? 1 : v) }}
                    className="input w-16 text-center" />
                </div>
                <p className="text-xs text-gray-600 mt-1">
                  Print {bedLayerStep} layer{bedLayerStep !== 1 ? 's' : ''} on each bed before switching to the next.
                </p>
              </Field>
            </section>
          </>
        )}

        {/* Hybrid CNC options (only for Hybrid machines) */}
        {isHybrid && (
          <>
            <Divider />
            <section className="space-y-3">
              <SectionHeader>CNC / Hybrid</SectionHeader>

              <Field label="CNC Tool">
                <select className="input" value={cncToolId} onChange={e => setCncToolId(e.target.value)}>
                  <option value="">Select…</option>
                  {cncTools.map(t => (
                    <option key={t.id} value={t.id}>
                      {t.name} (Ø{t.diameterMm} mm · flute {t.fluteLengthMm} mm)
                    </option>
                  ))}
                </select>
                {selectedCncTool && (
                  <div className="flex gap-3 text-[11px] text-gray-500 mt-1 flex-wrap">
                    <span><span className="text-violet-400">Ø</span> {selectedCncTool.diameterMm} mm</span>
                    <span><span className="text-orange-400">flute</span> {selectedCncTool.fluteLengthMm} mm</span>
                    <span><span className="text-blue-400">length</span> {selectedCncTool.toolLengthMm ?? '—'} mm</span>
                    <span className="text-gray-600">RPM {selectedCncTool.recommendedRpm.toLocaleString()}</span>
                    <span className="text-gray-600">Feed {selectedCncTool.recommendedFeedMmPerMin} mm/min</span>
                  </div>
                )}
              </Field>

              <Field label="Skip first N layers before machining">
                <div className="flex items-center gap-2">
                  <input type="number" min={0} max={500} step={1} value={skipMachiningLayers}
                    onChange={e => setSkipMachiningLayers(Math.max(0, Math.round(+e.target.value) || 0))}
                    className="input w-20 text-center" />
                  <span className="text-xs text-gray-500">layers</span>
                  {selectedProfile && skipMachiningLayers > 0 && (
                    <span className="text-xs text-gray-600">
                      ≈ {(skipMachiningLayers * (selectedProfile.layerHeightMm ?? 0.2)).toFixed(1)} mm
                    </span>
                  )}
                </div>
              </Field>

              {!autoMachiningFrequency && (
                <Field label={`Machine every N part layers (N = ${machineEveryN})`}>
                  <div className="flex items-center gap-2">
                    <input type="range" min={1} max={50} step={1} value={machineEveryN}
                      onChange={e => setMachineEveryN(+e.target.value)} className="flex-1 accent-primary" />
                    <input type="number" min={1} max={200} value={machineEveryN}
                      onChange={e => { const v = Math.max(1, +e.target.value); setMachineEveryN(isNaN(v) ? 10 : v) }}
                      className="input w-16 text-center" />
                  </div>
                </Field>
              )}

              {autoMachiningFrequency && selectedCncTool && (
                <p className="text-xs text-cyan-400/80 bg-cyan-950/30 rounded-lg px-3 py-2">
                  Auto mode: machining scheduled based on flute length ({selectedCncTool.fluteLengthMm} mm)
                  and geometry look-ahead. Manual N is ignored.
                </p>
              )}

              <Field label="Z Safety Offset (mm)">
                <div className="flex items-center gap-2">
                  <input type="number" min={0} max={5} step={0.05} value={zSafetyOffsetMm}
                    onChange={e => setZSafetyOffsetMm(Math.max(0, Math.min(5, +e.target.value)))}
                    className="input w-24 text-center" />
                  <span className="text-xs text-gray-600">raises CNC passes above nominal layer</span>
                </div>
              </Field>

              {selectedCncTool && (
                <Field label="Spindle RPM Override">
                  <div className="flex items-center gap-2">
                    <input type="number" min={0} max={60000} step={100}
                      value={spindleRpmOverride ?? selectedCncTool.recommendedRpm}
                      onChange={e => {
                        const v = +e.target.value
                        setSpindleRpmOverride(v === selectedCncTool.recommendedRpm ? null : v)
                      }}
                      className="input w-28 text-center" />
                    {spindleRpmOverride !== null && spindleRpmOverride !== selectedCncTool.recommendedRpm && (
                      <button onClick={() => setSpindleRpmOverride(null)}
                        className="text-xs text-gray-500 hover:text-gray-300">Reset</button>
                    )}
                  </div>
                </Field>
              )}

              <div className="space-y-2">
                <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
                  <input type="checkbox" checked={autoMachiningFrequency}
                    onChange={e => setAutoMachiningFrequency(e.target.checked)} className="accent-primary" />
                  Auto machining frequency
                </label>
                <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
                  <input type="checkbox" checked={machineInnerWalls}
                    onChange={e => setMachineInnerWalls(e.target.checked)} className="accent-primary" />
                  Machine inner walls
                </label>
                <label className="flex items-center gap-2 text-sm text-gray-300 cursor-pointer">
                  <input type="checkbox" checked={avoidSupports}
                    onChange={e => setAvoidSupports(e.target.checked)} className="accent-primary" />
                  Avoid supports
                </label>
                {avoidSupports && (
                  <Field label="Support clearance (mm)">
                    <input type="number" min={0} max={10} step={0.1} value={supportClearanceMm}
                      onChange={e => setSupportClearanceMm(Math.max(0, +e.target.value))}
                      className="input w-24 text-center" />
                  </Field>
                )}
              </div>
            </section>
          </>
        )}


        {models.length > 0 && (
          <>
            <Divider />

            {/* Bed tabs (multi-bed) */}
            {selectedMachineBeds.length > 1 && (
              <div className="flex gap-1 flex-wrap">
                {selectedMachineBeds.map((_: any, bi: number) => (
                  <button key={bi} onClick={() => setActiveBedIndex(bi)}
                    className={`px-3 py-1 text-xs rounded-lg border transition ${
                      bi === activeBedIndex
                        ? 'bg-indigo-800 border-indigo-500 text-white'
                        : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'
                    }`}>
                    Bed {bi + 1}
                    <span className="ml-1 text-gray-500">
                      ({models.filter(m => (m.bedIndex ?? 0) === bi).length})
                    </span>
                  </button>
                ))}
              </div>
            )}

            {/* Model List */}
            <section className="space-y-2">
              <div className="flex items-center justify-between">
                <SectionHeader>
                  {selectedMachineBeds.length > 1
                    ? `Bed ${activeBedIndex + 1} — Models (${models.filter(m => (m.bedIndex ?? 0) === activeBedIndex).length})`
                    : `Models (${models.length})`}
                </SectionHeader>
                {models.filter(m => (m.bedIndex ?? 0) === activeBedIndex).length > 1 && (
                  <button
                    onClick={() => viewerRef.current?.autoArrange()}
                    className="text-xs px-3 py-1 rounded-lg bg-indigo-700/60 hover:bg-indigo-700 text-indigo-200 border border-indigo-600 transition"
                  >
                    Auto-Arrange
                  </button>
                )}
              </div>

              <div className="space-y-1 max-h-36 overflow-y-auto pr-1">
                {models.filter(m => (m.bedIndex ?? 0) === activeBedIndex).map(m => (
                  <div
                    key={m.id}
                    onClick={() => setSelectedId(m.id)}
                    className={`flex items-center gap-2 px-3 py-2 rounded-lg cursor-pointer transition-colors text-xs
                      ${m.id === selectedId
                        ? 'bg-indigo-900/60 border border-indigo-600 text-white'
                        : 'bg-gray-800/60 border border-gray-700 text-gray-300 hover:bg-gray-800'}`}
                  >
                    <span className={`w-2 h-2 rounded-full flex-shrink-0 ${m.isOutOfBounds ? 'bg-red-500' : 'bg-green-500'}`} />
                    <span className="truncate flex-1">{m.name}</span>
                    {m.size && (
                      <span className="text-gray-500 flex-shrink-0">
                        {m.size.x.toFixed(0)}×{m.size.y.toFixed(0)}×{m.size.z.toFixed(0)}
                      </span>
                    )}
                    {/* Bed reassignment (multi-bed only) */}
                    {selectedMachineBeds.length > 1 && (
                      <select
                        value={m.bedIndex}
                        onClick={e => e.stopPropagation()}
                        onChange={e => {
                          const newBed = +e.target.value
                          setModels(prev => prev.map(p => p.id === m.id ? { ...p, bedIndex: newBed } : p))
                        }}
                        className="bg-gray-700 text-gray-300 text-[10px] rounded px-1 py-0.5 border border-gray-600"
                        title="Assign to bed"
                      >
                        {selectedMachineBeds.map((_: any, bi: number) => (
                          <option key={bi} value={bi}>B{bi + 1}</option>
                        ))}
                      </select>
                    )}
                    <button
                      onClick={e => { e.stopPropagation(); removeModel(m.id) }}
                      className="text-gray-600 hover:text-red-400 transition ml-1 flex-shrink-0"
                      title="Remove"
                    >✕</button>
                  </div>
                ))}
              </div>
            </section>

            {/* Selected model transform controls */}
            {selectedModel && (
              <>
                <Divider />
                <section className="space-y-4">
                  <SectionHeader>Transform — {selectedModel.name}</SectionHeader>

                  {/* Position */}
                  <div className="space-y-1.5">
                    <label className="text-xs text-gray-400 font-medium">Position (mm)</label>
                    <div className="grid grid-cols-3 gap-2">
                      <Field label="X">
                        <NumInput value={+selectedModel.transform.x.toFixed(2)} step={0.5}
                          onChange={v => patchSelected({ x: v })} />
                      </Field>
                      <Field label="Y">
                        <NumInput value={+selectedModel.transform.y.toFixed(2)} step={0.5}
                          onChange={v => patchSelected({ y: v })} />
                      </Field>
                      <Field label="Z lift">
                        <NumInput value={+selectedModel.transform.z.toFixed(2)} min={0} max={buildVolume.height} step={0.5}
                          onChange={v => patchSelected({ z: v })} />
                      </Field>
                    </div>
                  </div>

                  {/* Rotation */}
                  <div className="space-y-1.5">
                    <label className="text-xs text-gray-400 font-medium">Rotation (°)</label>
                    <div className="grid grid-cols-3 gap-2">
                      <Field label="X (tilt)">
                        <NumInput value={+selectedModel.transform.rotX.toFixed(1)} step={1}
                          onChange={v => patchSelected({ rotX: v })} />
                      </Field>
                      <Field label="Y (tilt)">
                        <NumInput value={+selectedModel.transform.rotY.toFixed(1)} step={1}
                          onChange={v => patchSelected({ rotY: v })} />
                      </Field>
                      <Field label="Z (spin)">
                        <NumInput value={+selectedModel.transform.rotZ.toFixed(1)} step={1}
                          onChange={v => patchSelected({ rotZ: v })} />
                      </Field>
                    </div>
                  </div>

                  {/* Scale */}
                  <div className="space-y-1.5">
                    <div className="flex items-center justify-between">
                      <label className="text-xs text-gray-400 font-medium">Scale</label>
                      <button
                        onClick={() => setUniformScale(u => !u)}
                        className={`text-xs px-2 py-0.5 rounded border transition
                          ${uniformScale
                            ? 'bg-indigo-800/60 border-indigo-600 text-indigo-200'
                            : 'bg-gray-800 border-gray-600 text-gray-400'}`}
                      >
                        {uniformScale ? '🔒 Uniform' : '🔓 Free'}
                      </button>
                    </div>
                    <div className="grid grid-cols-3 gap-2">
                      <Field label="X">
                        <NumInput value={+selectedModel.transform.scaleX.toFixed(3)} min={0.001} step={0.01}
                          onChange={v => patchScale('scaleX', v)} />
                      </Field>
                      <Field label="Y">
                        <NumInput value={+selectedModel.transform.scaleY.toFixed(3)} min={0.001} step={0.01}
                          onChange={v => patchScale('scaleY', v)} />
                      </Field>
                      <Field label="Z">
                        <NumInput value={+selectedModel.transform.scaleZ.toFixed(3)} min={0.001} step={0.01}
                          onChange={v => patchScale('scaleZ', v)} />
                      </Field>
                    </div>
                    <div className="flex gap-1.5 flex-wrap mt-1">
                      {[50, 75, 100, 125, 150, 200].map(pct => (
                        <button
                          key={pct}
                          onClick={() => {
                            pushUndo(selectedModel.id, selectedModel.transform)
                            const s = pct / 100
                            setModels(prev => prev.map(m => m.id === selectedModel.id
                              ? { ...m, transform: { ...m.transform, scaleX: s, scaleY: s, scaleZ: s } } : m))
                          }}
                          className={`text-xs px-2 py-0.5 rounded border transition
                            ${Math.abs(selectedModel.transform.scaleX * 100 - pct) < 0.5
                              ? 'bg-indigo-700/60 border-indigo-500 text-white'
                              : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'}`}
                        >
                          {pct}%
                        </button>
                      ))}
                    </div>
                  </div>

                  {/* Placement buttons */}
                  <div className="space-y-2">
                    <div className="flex gap-2 flex-wrap">
                      <PosButton onClick={() => viewerRef.current?.centerOnBed()}>Center on Bed</PosButton>
                      <PosButton onClick={() => viewerRef.current?.placeOnBed()}>Place on Bed</PosButton>
                      <PosButton onClick={() => viewerRef.current?.resetTransform()}>Reset All</PosButton>
                    </div>

                    <DisabledHint when={!hasFaceSelected} reason="Click a face on the model to select it, then use this to orient it flat on the bed.">
                      <button
                        onClick={() => viewerRef.current?.placeFaceOnBed()}
                        disabled={!hasFaceSelected}
                        className={`w-full py-2 text-sm rounded-lg font-medium transition-colors border
                          ${hasFaceSelected
                            ? 'bg-orange-600/80 hover:bg-orange-600 border-orange-500 text-white cursor-pointer'
                            : 'bg-gray-800 border-gray-700 text-gray-500 cursor-not-allowed opacity-50'}`}
                      >
                        Place Selected Face on Bed
                      </button>
                    </DisabledHint>
                  </div>

                  {selectedModel.isOutOfBounds && (
                    <p className="text-xs text-red-400 bg-red-950/40 rounded-lg px-3 py-2">
                      Model outside build volume. Adjust position or scale.
                    </p>
                  )}
                </section>
              </>
            )}
          </>
        )}

        <div className="flex-1" />

        {models.length > 1 && (
          <p className="text-xs text-gray-500 text-center">
            Submitting: <span className="text-gray-300">{(selectedModel ?? models[0]).name}</span>
          </p>
        )}

        <div className="flex gap-2">
          <DisabledHint when={!canPreview} reason={
            models.length === 0 ? 'Import an STL model first.' :
            !machineId ? 'Select a machine.' :
            !profileId ? 'Select print settings.' :
            !materialId ? 'Select a material.' :
            'Please wait for the current operation to finish.'
          }>
            <button
              onClick={handlePreview}
              disabled={!canPreview}
              className="flex-1 py-2.5 bg-gray-700/80 hover:bg-gray-700 disabled:opacity-40 disabled:cursor-not-allowed
                         text-white rounded-lg font-medium transition-colors border border-gray-600"
            >
              {isPreviewLoading ? 'Slicing…' : 'Slice'}
            </button>
          </DisabledHint>
          <DisabledHint when={!canSubmit} reason={
            models.length === 0 ? 'Import an STL model first.' :
            !machineId ? 'Select a machine.' :
            !profileId ? 'Select print settings.' :
            !materialId ? 'Select a material.' :
            !jobName ? 'Enter a job name.' :
            'Please wait for the current operation to finish.'
          }>
            <button
              onClick={handleSubmit}
              disabled={!canSubmit}
              className="flex-1 py-2.5 bg-primary/80 hover:bg-primary disabled:opacity-40 disabled:cursor-not-allowed
                         text-white rounded-lg font-medium transition-colors"
            >
              {isMergingBeds ? 'Slicing beds…' : uploadMutation.isPending ? 'Uploading…' : sliceMutation.isPending ? 'Generating…' : 'Generate G-code'}
            </button>
          </DisabledHint>
        </div>

        {(uploadMutation.isError || sliceMutation.isError) && (
          <p className="text-red-400 text-sm whitespace-pre-wrap">
            {uploadMutation.isError
              ? 'Upload failed.'
              : `Slicing failed: ${
                  (sliceMutation.error as any)?.response?.data?.detail
                  ?? (sliceMutation.error as any)?.response?.data?.message
                  ?? (sliceMutation.error as any)?.message
                  ?? 'Unknown error'
                }`}
          </p>
        )}
      </div>
      </div>
      )}

    </div>
  )
}

// ── Small helpers ──────────────────────────────────────────────────────────────

function SectionHeader({ children }: { children: React.ReactNode }) {
  return <h4 className="text-sm font-semibold text-white tracking-wide">{children}</h4>
}

function Divider() {
  return <hr className="border-gray-800" />
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="space-y-1">
      <label className="text-xs text-gray-400">{label}</label>
      {children}
    </div>
  )
}

function NumInput({
  value, min, max, step = 1, onChange,
}: {
  value: number; min?: number; max?: number; step?: number
  onChange: (v: number) => void
}) {
  const [editing, setEditing] = useState(false)
  const [text, setText] = useState('')
  return (
    <input
      type="number"
      className="input text-sm"
      value={editing ? text : value}
      min={min}
      max={max}
      step={step}
      onFocus={e => { setEditing(true); setText(e.target.value) }}
      onChange={e => {
        setText(e.target.value)
        const v = parseFloat(e.target.value)
        if (!isNaN(v)) onChange(v)
      }}
      onBlur={() => {
        setEditing(false)
        const v = parseFloat(text)
        onChange(isNaN(v) ? 0 : v)
      }}
    />
  )
}

function PosButton({ onClick, children }: { onClick: () => void; children: React.ReactNode }) {
  return (
    <button
      onClick={onClick}
      className="px-3 py-1.5 text-xs rounded-lg bg-gray-800 hover:bg-gray-700 text-gray-300
                 hover:text-white transition-colors border border-gray-700"
    >
      {children}
    </button>
  )
}

function TabBtn({ active, disabled, onClick, children }: {
  active: boolean; disabled?: boolean; onClick: () => void; children: React.ReactNode
}) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      className={`px-5 py-2.5 text-sm font-medium border-b-2 -mb-px transition-colors
        ${active
          ? 'border-primary text-white'
          : 'border-transparent text-gray-500 hover:text-gray-300 disabled:opacity-30 disabled:cursor-not-allowed'}`}
    >
      {children}
    </button>
  )
}

// ── G-code layer parsing ───────────────────────────────────────────────────────

interface GCodeLayer { layerNum: number; lines: string[] }

function parseGCodeLayers(gcode: string): GCodeLayer[] {
  const layers: GCodeLayer[] = []
  let current: GCodeLayer | null = null
  for (const line of gcode.split('\n')) {
    const m = line.match(/^;LAYER:(\d+)/)
    if (m) {
      if (current) layers.push(current)
      current = { layerNum: parseInt(m[1]), lines: [line] }
    } else if (current) {
      current.lines.push(line)
    }
  }
  if (current) layers.push(current)
  return layers
}

// ── G-code text layer viewer ──────────────────────────────────────────────────

function GCodeLayerViewer({ gcode }: { gcode: string }) {
  const layers = useMemo(() => parseGCodeLayers(gcode), [gcode])
  const [selectedIdx, setSelectedIdx] = useState(0)
  const textRef = useRef<HTMLPreElement>(null)

  // Scroll text to top when layer changes
  useEffect(() => {
    if (textRef.current) textRef.current.scrollTop = 0
  }, [selectedIdx])

  const layer = layers[selectedIdx]

  if (layers.length === 0) {
    return (
      <div className="flex items-center justify-center flex-1 text-gray-500 text-sm">
        No layer data found in G-code.
      </div>
    )
  }

  return (
    <div className="flex flex-1 min-h-0 overflow-hidden rounded-xl border border-gray-700">
      {/* Layer list sidebar */}
      <div className="w-16 flex-shrink-0 overflow-y-auto bg-gray-950 border-r border-gray-700">
        <div className="text-xs text-gray-500 px-2 py-1 sticky top-0 bg-gray-950 border-b border-gray-800 text-center">
          Layer
        </div>
        {layers.map((l, i) => (
          <button
            key={l.layerNum}
            onClick={() => setSelectedIdx(i)}
            className={`w-full px-1 py-1 text-xs text-center transition-colors
              ${i === selectedIdx
                ? 'bg-blue-900/60 text-blue-300 font-medium'
                : 'text-gray-500 hover:bg-gray-800 hover:text-gray-300'}`}
          >
            {l.layerNum}
          </button>
        ))}
      </div>
      {/* G-code text panel */}
      <div className="flex-1 overflow-hidden flex flex-col bg-gray-950">
        <div className="text-xs text-gray-500 px-3 py-1 border-b border-gray-800 flex items-center gap-2 flex-shrink-0">
          <span className="text-blue-400 font-medium">Layer {layer?.layerNum ?? 0}</span>
          <span>·</span>
          <span>{layer?.lines.length ?? 0} lines</span>
        </div>
        <pre
          ref={textRef}
          className="flex-1 overflow-auto p-3 text-xs text-green-400 font-mono leading-relaxed"
          style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-all' }}
        >
          {layer?.lines.join('\n') ?? ''}
        </pre>
      </div>
    </div>
  )
}

// ── Inline G-code preview (uses already-fetched gcode string) ────────────────

function GCodePreviewInline({ gcode, buildVolume, lineWidth = 0.4, travelX, travelY, travelZ, originX, originY, beds, originIsBedCenter }: {
  gcode: string
  buildVolume: BuildVolume
  lineWidth?: number
  travelX?: number
  travelY?: number
  travelZ?: number
  originX?: number
  originY?: number
  beds?: BedInfo[]
  originIsBedCenter?: boolean
}) {
  const [view, setView] = useState<'3d' | 'layers'>('3d')
  return (
    <div className="flex flex-col flex-1 min-h-0 gap-2">
      <div className="flex gap-1 flex-shrink-0">
        <button
          onClick={() => setView('3d')}
          className={`px-3 py-1 text-xs rounded-lg border transition-colors
            ${view === '3d' ? 'bg-blue-900/60 border-blue-600 text-blue-200' : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'}`}
        >3D Preview</button>
        <button
          onClick={() => setView('layers')}
          className={`px-3 py-1 text-xs rounded-lg border transition-colors
            ${view === 'layers' ? 'bg-blue-900/60 border-blue-600 text-blue-200' : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'}`}
        >G-code Layers</button>
      </div>
      {view === '3d'
        ? <GCodePreview3D gcode={gcode} buildVolume={buildVolume} lineWidth={lineWidth} className="flex-1 min-h-0"
            travelX={travelX} travelY={travelY} travelZ={travelZ} originX={originX} originY={originY}
            beds={beds} originIsBedCenter={originIsBedCenter} />
        : <GCodeLayerViewer gcode={gcode} />
      }
    </div>
  )
}

// ── G-code preview component (3D + layer text) ────────────────────────────────

function GCodePreview({ jobId, buildVolume, lineWidth = 0.4, travelX, travelY, travelZ, originX, originY, beds, originIsBedCenter }: {
  jobId: string
  buildVolume: BuildVolume
  lineWidth?: number
  travelX?: number; travelY?: number; travelZ?: number
  originX?: number; originY?: number
  beds?: { index: number; widthMm: number; depthMm: number; positionXMm: number; positionYMm: number }[]
  originIsBedCenter?: boolean
}) {
  const [view, setView] = useState<'3d' | 'layers'>('3d')

  const { data: gcode, isLoading, isError } = useQuery({
    queryKey: ['print-gcode', jobId],
    queryFn: () => jobsApi.getPrintGCode(jobId),
  })

  if (isLoading) return (
    <div className="flex items-center justify-center flex-1 text-gray-400 text-sm gap-2">
      <svg className="w-4 h-4 animate-spin" fill="none" viewBox="0 0 24 24">
        <circle className="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="4" />
        <path className="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8v8H4z" />
      </svg>
      Loading G-code…
    </div>
  )
  if (isError || !gcode) return (
    <div className="flex items-center justify-center flex-1 text-red-400 text-sm">
      Failed to load G-code.
    </div>
  )

  return (
    <div className="flex flex-col flex-1 min-h-0 gap-2">
      {/* Sub-view toggle */}
      <div className="flex gap-1 flex-shrink-0">
        <button
          onClick={() => setView('3d')}
          className={`px-3 py-1 text-xs rounded-lg border transition-colors
            ${view === '3d'
              ? 'bg-blue-900/60 border-blue-600 text-blue-200'
              : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'}`}
        >
          3D Preview
        </button>
        <button
          onClick={() => setView('layers')}
          className={`px-3 py-1 text-xs rounded-lg border transition-colors
            ${view === 'layers'
              ? 'bg-blue-900/60 border-blue-600 text-blue-200'
              : 'bg-gray-800 border-gray-700 text-gray-400 hover:text-gray-200'}`}
        >
          G-code Layers
        </button>
      </div>
      {view === '3d'
        ? <GCodePreview3D gcode={gcode} buildVolume={buildVolume} lineWidth={lineWidth} className="flex-1 min-h-0"
            travelX={travelX} travelY={travelY} travelZ={travelZ} originX={originX} originY={originY} beds={beds} originIsBedCenter={originIsBedCenter} />
        : <GCodeLayerViewer gcode={gcode} />
      }
    </div>
  )
}
