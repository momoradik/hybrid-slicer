/**
 * RPS point entry — user enters up to 6 reference points with coordinates,
 * lock directions (X/Y/Z/normal), and weights.
 *
 * Includes a POINT SET LIBRARY: save named sets to localStorage, select
 * a saved set to populate the form, rename/delete/overwrite sets.
 *
 * COMPUTE FIREWALL: this component collects coordinates and lock
 * specifications only. No alignment math.
 */
import { useState, useEffect } from 'react'
import type { RPSPointData, RPSLockData } from '../../api/alignmesh'
import {
  loadAllSets, saveSet, updateSet, deleteSet, renameSet,
  type RPSPointSet, type StoredRPSPoint,
} from './rps-point-library'

interface Props {
  onPointsChanged: (points: RPSPointData[]) => void
}

interface RPSRow {
  id: number
  x: string; y: string; z: string
  lockX: boolean; lockY: boolean; lockZ: boolean; lockNormal: boolean
  weight: string
}

const emptyRow = (id: number): RPSRow => ({
  id, x: '', y: '', z: '',
  lockX: false, lockY: false, lockZ: true, lockNormal: false,
  weight: '1',
})

function rowToData(r: RPSRow): RPSPointData | null {
  const x = parseFloat(r.x), y = parseFloat(r.y), z = parseFloat(r.z)
  if (isNaN(x) || isNaN(y) || isNaN(z)) return null
  const w = parseFloat(r.weight) || 1
  const locks: RPSLockData[] = []
  if (r.lockX) locks.push({ axis: 'x', weight: w })
  if (r.lockY) locks.push({ axis: 'y', weight: w })
  if (r.lockZ) locks.push({ axis: 'z', weight: w })
  if (r.lockNormal) locks.push({ axis: 'normal', weight: w })
  if (locks.length === 0) return null
  return { x, y, z, locks }
}

function rowFromStored(pt: StoredRPSPoint, id: number): RPSRow {
  return {
    id,
    x: String(pt.x), y: String(pt.y), z: String(pt.z),
    lockX: pt.lockX, lockY: pt.lockY, lockZ: pt.lockZ, lockNormal: pt.lockNormal,
    weight: String(pt.weight),
  }
}

function rowToStored(r: RPSRow): StoredRPSPoint | null {
  const x = parseFloat(r.x), y = parseFloat(r.y), z = parseFloat(r.z)
  if (isNaN(x) || isNaN(y) || isNaN(z)) return null
  return {
    x, y, z,
    lockX: r.lockX, lockY: r.lockY, lockZ: r.lockZ, lockNormal: r.lockNormal,
    weight: parseFloat(r.weight) || 1,
  }
}

export default function RPSPointEntry({ onPointsChanged }: Props) {
  const [rows, setRows] = useState<RPSRow[]>([
    emptyRow(0), emptyRow(1), emptyRow(2),
    emptyRow(3), emptyRow(4), emptyRow(5),
  ])
  const [library, setLibrary] = useState<RPSPointSet[]>([])
  const [selectedSetId, setSelectedSetId] = useState<string | null>(null)
  const [saveName, setSaveName] = useState('')
  const [showSaveDialog, setShowSaveDialog] = useState(false)
  const [renamingId, setRenamingId] = useState<string | null>(null)
  const [renameValue, setRenameValue] = useState('')

  // Load library on mount.
  useEffect(() => { setLibrary(loadAllSets()) }, [])

  const emit = (updated: RPSRow[]) => {
    const valid = updated.map(rowToData).filter((d): d is RPSPointData => d !== null)
    onPointsChanged(valid)
  }

  const updateRow = (id: number, field: keyof RPSRow, value: string | boolean) => {
    const updated = rows.map(r => r.id === id ? { ...r, [field]: value } : r)
    setRows(updated)
    emit(updated)
  }

  const addRow = () => {
    // No upper limit — user can add as many points as needed
    const updated = [...rows, emptyRow(rows.length)]
    setRows(updated)
  }

  const removeRow = (id: number) => {
    if (rows.length <= 1) return
    const updated = rows.filter(r => r.id !== id)
    setRows(updated)
    emit(updated)
  }

  // ── Library actions ──────────────────────────────────────────────

  const handleSelectSet = (setId: string) => {
    const s = library.find(s => s.id === setId)
    if (!s) return
    setSelectedSetId(setId)
    const loaded = s.points.map((pt, i) => rowFromStored(pt, i))
    setRows(loaded)
    emit(loaded)
  }

  const handleSaveNew = () => {
    const name = saveName.trim()
    if (!name) return
    const points = rows.map(rowToStored).filter((p): p is StoredRPSPoint => p !== null)
    if (points.length === 0) return
    const newSet = saveSet(name, points)
    setLibrary(loadAllSets())
    setSelectedSetId(newSet.id)
    setShowSaveDialog(false)
    setSaveName('')
  }

  const handleOverwrite = () => {
    if (!selectedSetId) return
    const s = library.find(s => s.id === selectedSetId)
    if (!s) return
    const points = rows.map(rowToStored).filter((p): p is StoredRPSPoint => p !== null)
    updateSet(selectedSetId, s.name, points)
    setLibrary(loadAllSets())
  }

  const handleDelete = (id: string) => {
    deleteSet(id)
    setLibrary(loadAllSets())
    if (selectedSetId === id) setSelectedSetId(null)
  }

  const handleRename = (id: string) => {
    const v = renameValue.trim()
    if (!v) return
    renameSet(id, v)
    setLibrary(loadAllSets())
    setRenamingId(null)
    setRenameValue('')
  }

  const handleClear = () => {
    const cleared = [emptyRow(0), emptyRow(1), emptyRow(2), emptyRow(3), emptyRow(4), emptyRow(5)]
    setRows(cleared)
    setSelectedSetId(null)
    emit(cleared)
  }

  const numCoord = "w-16 px-1.5 py-1 bg-gray-800 border border-gray-700 rounded text-xs text-gray-200 text-center font-mono"
  const checkCls = "w-4 h-4 accent-primary"
  const validCount = rows.map(rowToData).filter(d => d !== null).length
  const selectedSet = library.find(s => s.id === selectedSetId)

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <h4 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
          RPS Reference Points
        </h4>
        <span className="text-[10px] text-gray-500">
          {validCount} of {rows.length} valid
        </span>
      </div>

      {/* ── Point Set Library ────────────────────────────────────── */}
      <div className="bg-gray-800 border border-gray-700 rounded-lg p-3 space-y-2">
        <div className="flex items-center justify-between">
          <span className="text-[10px] text-gray-500 uppercase tracking-wider">Point Set Library</span>
          <div className="flex gap-1">
            <button onClick={() => { setShowSaveDialog(true); setSaveName('') }}
              className="text-[10px] px-2 py-0.5 rounded bg-primary/20 text-primary-300 hover:bg-primary/30 transition">
              Save as new
            </button>
            {selectedSetId && (
              <button onClick={handleOverwrite}
                className="text-[10px] px-2 py-0.5 rounded bg-green-900/40 text-green-400 hover:bg-green-900/60 transition">
                Update "{selectedSet?.name}"
              </button>
            )}
            <button onClick={handleClear}
              className="text-[10px] px-2 py-0.5 rounded bg-gray-700 text-gray-400 hover:bg-gray-600 transition">
              Clear
            </button>
          </div>
        </div>

        {/* Save dialog */}
        {showSaveDialog && (
          <div className="flex gap-2 items-center">
            <input
              className="flex-1 px-2 py-1 bg-gray-900 border border-gray-600 rounded text-xs text-gray-200"
              placeholder="Set name (e.g. 'Bracket A datums')"
              value={saveName}
              onChange={e => setSaveName(e.target.value)}
              onKeyDown={e => e.key === 'Enter' && handleSaveNew()}
              autoFocus
            />
            <button onClick={handleSaveNew}
              disabled={!saveName.trim()}
              className="text-[10px] px-2 py-1 rounded bg-primary/30 text-primary-300 hover:bg-primary/40 disabled:opacity-30 transition">
              Save
            </button>
            <button onClick={() => setShowSaveDialog(false)}
              className="text-[10px] px-2 py-1 rounded bg-gray-700 text-gray-400 hover:bg-gray-600 transition">
              Cancel
            </button>
          </div>
        )}

        {/* Saved sets list */}
        {library.length > 0 ? (
          <div className="space-y-1 max-h-32 overflow-y-auto">
            {library.map(s => (
              <div key={s.id}
                className={'flex items-center gap-2 px-2 py-1 rounded text-xs cursor-pointer transition ' +
                  (selectedSetId === s.id
                    ? 'bg-primary/20 text-primary-300 border border-primary/30'
                    : 'bg-gray-900 text-gray-400 hover:bg-gray-850 hover:text-gray-300')}>
                {renamingId === s.id ? (
                  <input
                    className="flex-1 px-1 py-0.5 bg-gray-800 border border-gray-600 rounded text-xs text-gray-200"
                    value={renameValue}
                    onChange={e => setRenameValue(e.target.value)}
                    onKeyDown={e => { if (e.key === 'Enter') handleRename(s.id); if (e.key === 'Escape') setRenamingId(null) }}
                    onBlur={() => setRenamingId(null)}
                    autoFocus
                  />
                ) : (
                  <span className="flex-1 truncate" onClick={() => handleSelectSet(s.id)}>
                    {s.name}
                  </span>
                )}
                <span className="text-[9px] text-gray-600 shrink-0">
                  {s.points.length}pt
                </span>
                <button onClick={e => { e.stopPropagation(); setRenamingId(s.id); setRenameValue(s.name) }}
                  className="text-gray-600 hover:text-gray-300 text-[10px]" title="Rename">
                  ab
                </button>
                <button onClick={e => { e.stopPropagation(); handleDelete(s.id) }}
                  className="text-gray-600 hover:text-red-400 text-[10px]" title="Delete">
                  x
                </button>
              </div>
            ))}
          </div>
        ) : (
          <p className="text-[10px] text-gray-600 italic">No saved sets. Define points below, then save.</p>
        )}
      </div>

      {/* ── Point entry grid ─────────────────────────────────────── */}
      <p className="text-xs text-gray-500">
        Enter coordinates in the reference part's frame. Check which axes to lock for each point.
      </p>

      {/* Header */}
      <div className="grid grid-cols-[auto_1fr_auto_auto_auto_auto_auto_auto] gap-2 text-[10px] text-gray-500 uppercase items-center">
        <span className="w-5"></span>
        <span className="text-center text-blue-500">Position (reference frame)</span>
        <span className="text-center w-8">X</span>
        <span className="text-center w-8">Y</span>
        <span className="text-center w-8">Z</span>
        <span className="text-center w-8">N</span>
        <span className="text-center w-12">Wt</span>
        <span className="w-5"></span>
      </div>

      {/* Rows */}
      {rows.map((row, i) => (
        <div key={row.id} className="grid grid-cols-[auto_1fr_auto_auto_auto_auto_auto_auto] gap-2 items-center">
          <span className="text-xs text-gray-600 w-5 text-right">{i + 1}</span>
          <div className="flex gap-1">
            <input className={numCoord} placeholder="X" value={row.x}
              onChange={e => updateRow(row.id, 'x', e.target.value)} />
            <input className={numCoord} placeholder="Y" value={row.y}
              onChange={e => updateRow(row.id, 'y', e.target.value)} />
            <input className={numCoord} placeholder="Z" value={row.z}
              onChange={e => updateRow(row.id, 'z', e.target.value)} />
          </div>
          <label className="w-8 flex justify-center" title="Lock X axis">
            <input type="checkbox" checked={row.lockX} className={checkCls}
              onChange={e => updateRow(row.id, 'lockX', e.target.checked)} />
          </label>
          <label className="w-8 flex justify-center" title="Lock Y axis">
            <input type="checkbox" checked={row.lockY} className={checkCls}
              onChange={e => updateRow(row.id, 'lockY', e.target.checked)} />
          </label>
          <label className="w-8 flex justify-center" title="Lock Z axis">
            <input type="checkbox" checked={row.lockZ} className={checkCls}
              onChange={e => updateRow(row.id, 'lockZ', e.target.checked)} />
          </label>
          <label className="w-8 flex justify-center" title="Lock along surface normal">
            <input type="checkbox" checked={row.lockNormal} className={checkCls}
              onChange={e => updateRow(row.id, 'lockNormal', e.target.checked)} />
          </label>
          <input className="w-12 px-1.5 py-1 bg-gray-800 border border-gray-700 rounded text-xs text-gray-200 text-center font-mono"
            placeholder="1" value={row.weight}
            onChange={e => updateRow(row.id, 'weight', e.target.value)} />
          <button onClick={() => removeRow(row.id)} disabled={rows.length <= 1}
            className="text-red-700 hover:text-red-400 text-xs w-5 disabled:opacity-20">x</button>
        </div>
      ))}

      <button onClick={addRow}
        className="text-xs text-gray-500 hover:text-gray-300 px-2 py-1 rounded bg-gray-800 hover:bg-gray-700 transition">
        + Add point
      </button>

      {/* Two-stage explanation */}
      <div className="bg-blue-950 border border-blue-800 rounded-lg p-3 text-xs text-blue-300 space-y-1">
        <p className="font-medium">Pre-aligned RPS: two-stage pipeline</p>
        <p className="text-blue-400">
          <strong>Stage 1 (pre-alignment)</strong>: Coarse-to-fine registration (TEASER++ + GICP)
          brings the scan into approximate correspondence. This is NOT the reported alignment.
        </p>
        <p className="text-blue-400">
          <strong>Stage 2 (RPS alignment)</strong>: Weighted directional constrained fit of
          the reference points above. This is the conformance-bearing alignment.
        </p>
      </div>
    </div>
  )
}
