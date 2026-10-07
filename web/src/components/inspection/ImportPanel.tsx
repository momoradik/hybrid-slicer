/**
 * Part import panel — click to select a file, part appears immediately.
 *
 * COMPUTE FIREWALL: the viewer's copy is display-only and never feeds
 * analysis. The core reads the original file at double precision.
 */
import { useRef, useState } from 'react'
import { alignmeshApi } from '../../api/alignmesh'

/** Abort any pending preview-mesh request (call before inspect). */
let _previewAbort: AbortController | null = null
export function cancelPreviewMesh() {
  if (_previewAbort) { _previewAbort.abort(); _previewAbort = null }
}

export type PartRole = 'reference' | 'measured'

export interface ImportedPart {
  role: PartRole
  path: string
  format: string
  displayLabel: string
  /** Raw file bytes for immediate 3D preview (display only). */
  fileData: ArrayBuffer
  fileName: string
}

interface Props {
  role: PartRole
  onImport: (part: ImportedPart) => void
  imported?: ImportedPart | null
  onBusyChange?: (busy: boolean) => void
}

const ACCEPT_REF = '.stl,.ply,.step,.stp'
const ACCEPT_MEAS = '.stl,.ply,.xyz,.pts,.asc'

const FORMAT_FROM_EXT: Record<string, { format: string; label: string }> = {
  '.stl':  { format: 'stl',  label: 'STL (tessellated, float32)' },
  '.ply':  { format: 'ply',  label: 'PLY (mesh/cloud)' },
  '.obj':  { format: 'obj',  label: 'OBJ (mesh)' },
  '.step': { format: 'step', label: 'STEP/CAD (analytic BREP)' },
  '.stp':  { format: 'step', label: 'STEP/CAD (analytic BREP)' },
  '.e57':  { format: 'e57',  label: 'E57 (point cloud)' },
  '.xyz':  { format: 'xyz',  label: 'XYZ (point cloud, ASCII)' },
  '.pts':  { format: 'xyz',  label: 'PTS (point cloud)' },
  '.asc':  { format: 'xyz',  label: 'ASC (point cloud)' },
}

export function detectFormat(path: string): { format: string; label: string } {
  const ext = '.' + path.split('.').pop()?.toLowerCase()
  return FORMAT_FROM_EXT[ext] ?? { format: 'unknown', label: 'Unknown format' }
}

export default function ImportPanel({ role, onImport, imported, onBusyChange }: Props) {
  const fileRef = useRef<HTMLInputElement>(null)
  const [uploading, setUploading] = useState(false)
  const [uploadError, setUploadError] = useState<string | null>(null)

  const accept = role === 'reference' ? ACCEPT_REF : ACCEPT_MEAS
  const roleLabel = role === 'reference' ? 'Reference Part' : 'Measured Part'
  const roleColor = role === 'reference' ? 'border-blue-800 hover:border-blue-600' : 'border-green-800 hover:border-green-600'
  const roleBg = role === 'reference' ? 'bg-blue-950/30' : 'bg-green-950/30'
  const roleText = role === 'reference' ? 'text-blue-400' : 'text-green-400'

  const handleFile = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0]
    if (!file) return
    const fmt = detectFormat(file.name)
    const data = await file.arrayBuffer()

    // Upload to core server to get an absolute filesystem path.
    setUploading(true)
    setUploadError(null)
    onBusyChange?.(true)
    try {
      const uploaded = await alignmeshApi.upload(file)

      // For STEP files, pass an empty buffer initially (STLLoader can't parse STEP).
      // The tessellated preview will arrive in the background.
      const isStep = fmt.format === 'step'
      const part: ImportedPart = {
        role,
        path: uploaded.path,
        format: fmt.format,
        displayLabel: isStep ? fmt.label + ' (loading 3D preview...)' : fmt.label,
        fileData: isStep ? new ArrayBuffer(0) : data,
        fileName: file.name,
      }
      onImport(part)

      // For STEP files, fetch coarse tessellation in background for 3D preview.
      if (isStep) {
        cancelPreviewMesh()
        _previewAbort = new AbortController()
        alignmeshApi.previewMesh(uploaded.path, _previewAbort.signal).then(stlData => {
          _previewAbort = null
          onImport({ ...part, displayLabel: fmt.label, fileData: stlData })
        }).catch(() => { /* preview failed — non-blocking */ })
      }
    } catch (err: any) {
      setUploadError('Upload failed: ' + (err?.message || 'unknown error'))
    } finally {
      setUploading(false)
      onBusyChange?.(false)
    }
  }

  return (
    <div>
      <input
        ref={fileRef}
        type="file"
        accept={accept}
        onChange={handleFile}
        className="hidden"
        data-testid={'import-' + role + '-file'}
      />

      {uploadError && (
        <div className="mb-2 text-xs text-red-400 bg-red-950 border border-red-800 rounded-lg p-2">{uploadError}</div>
      )}

      {!imported ? (
        <button
          onClick={() => fileRef.current?.click()}
          disabled={uploading}
          data-testid={'import-' + role + '-btn'}
          className={'w-full rounded-xl border-2 border-dashed p-8 text-center transition cursor-pointer disabled:opacity-50 ' + roleColor + ' ' + roleBg}
        >
          <p className={'text-sm font-medium ' + roleText}>{uploading ? 'Uploading...' : roleLabel}</p>
          <p className="text-xs text-gray-500 mt-1">{uploading ? 'Sending file to core...' : 'Click to select file'}</p>
          <p className="text-[10px] text-gray-600 mt-1">
            {role === 'reference' ? 'STL, PLY, OBJ, STEP' : 'STL, PLY, OBJ, E57, XYZ'}
          </p>
        </button>
      ) : (
        <div className={'rounded-xl border p-3 ' + (role === 'reference' ? 'border-blue-900 bg-blue-950/20' : 'border-green-900 bg-green-950/20')}>
          <div className="flex items-center justify-between">
            <div>
              <p className={'text-xs font-medium uppercase tracking-wider ' + roleText}>{roleLabel}</p>
              <p className="text-sm text-gray-200 mt-0.5">{imported.fileName}</p>
              <p className="text-[10px] text-gray-500">{imported.displayLabel}</p>
            </div>
            <button
              onClick={() => fileRef.current?.click()}
              className="text-xs text-gray-500 hover:text-gray-300 px-2 py-1 rounded bg-gray-800 hover:bg-gray-700 transition"
            >
              Change
            </button>
          </div>
        </div>
      )}
    </div>
  )
}
