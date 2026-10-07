/**
 * Landmark/RPS point definition panel.
 *
 * Two modes:
 *   Mode A — relative to file origin (0,0,0)
 *   Mode B — relative to a defined reference datum
 *
 * FIREWALL: the UI collects points and converts coordinates (input prep).
 * The CORE computes the fit. The UI displays the core's returned results.
 */
import { useState } from 'react'
import type { InspectResponse } from '../../api/alignmesh-types.generated'
import {
  type LandmarkPoint, type ReferenceDatum,
  IDENTITY_DATUM, datumToFileFrame, countConstrainedDOFs,
} from './landmark-logic'

type Mode = 'file-origin' | 'reference-datum'

interface Props {
  /** Called when the user sends points to the core. */
  onSubmit: (points: LandmarkPoint[]) => void
  /** Core's returned fit result (displayed after submission). */
  fitResult?: InspectResponse | null
}

export default function LandmarkPanel({ onSubmit, fitResult }: Props) {
  const [mode, setMode] = useState<Mode>('file-origin')
  const [datum, setDatum] = useState<ReferenceDatum>({ ...IDENTITY_DATUM })
  const [points, setPoints] = useState<LandmarkPoint[]>([])
  const [nextId, setNextId] = useState(1)

  const dofStatus = countConstrainedDOFs(points)

  const addPoint = () => {
    const id = 'P' + nextId
    const newPt: LandmarkPoint = {
      id,
      entered: [0, 0, 0],
      fileFrame: [0, 0, 0],
      locks: { x: false, y: false, z: false },
      weight: 1.0,
      label: id,
    }
    setPoints([...points, newPt])
    setNextId(nextId + 1)
  }

  const updatePoint = (idx: number, field: string, value: unknown) => {
    const updated = [...points]
    const p = { ...updated[idx] }

    if (field === 'x' || field === 'y' || field === 'z') {
      const ci = field === 'x' ? 0 : field === 'y' ? 1 : 2
      const entered: [number, number, number] = [...p.entered]
      entered[ci] = value as number
      p.entered = entered
      // Convert to file frame.
      p.fileFrame = mode === 'file-origin' ? [...entered] : datumToFileFrame(entered, datum)
    } else if (field === 'lockX' || field === 'lockY' || field === 'lockZ') {
      const axis = field.slice(4).toLowerCase() as 'x' | 'y' | 'z'
      p.locks = { ...p.locks, [axis]: value as boolean }
    } else if (field === 'weight') {
      p.weight = value as number
    }

    updated[idx] = p
    setPoints(updated)
  }

  const removePoint = (idx: number) => {
    setPoints(points.filter((_, i) => i !== idx))
  }

  const handleSubmit = () => {
    // Recompute all file-frame coordinates before sending.
    const final = points.map(p => ({
      ...p,
      fileFrame: mode === 'file-origin' ? [...p.entered] as [number, number, number] : datumToFileFrame(p.entered, datum),
    }))
    onSubmit(final)
  }

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-4">
      <h4 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
        Landmark / RPS Definition
      </h4>

      {/* Mode selector */}
      <div className="flex gap-2">
        <button
          onClick={() => setMode('file-origin')}
          className={'px-3 py-1.5 rounded text-xs transition border ' +
            (mode === 'file-origin' ? 'bg-primary/20 border-primary/50 text-primary-300' : 'bg-gray-800 border-gray-700 text-gray-400')}
        >
          Mode A: File Origin
        </button>
        <button
          onClick={() => setMode('reference-datum')}
          className={'px-3 py-1.5 rounded text-xs transition border ' +
            (mode === 'reference-datum' ? 'bg-primary/20 border-primary/50 text-primary-300' : 'bg-gray-800 border-gray-700 text-gray-400')}
        >
          Mode B: Reference Datum
        </button>
      </div>

      {/* Reference datum editor (Mode B) */}
      {mode === 'reference-datum' && (
        <div className="bg-gray-800 rounded-lg p-3 space-y-2 text-xs">
          <p className="text-gray-400 font-medium">Reference Datum</p>
          <div className="grid grid-cols-4 gap-2">
            <div>
              <label className="text-gray-500">TX</label>
              <input className="input w-full text-xs" type="number" step="0.001" value={datum.translation[0]}
                onChange={e => setDatum({ ...datum, translation: [parseFloat(e.target.value) || 0, datum.translation[1], datum.translation[2]] })} />
            </div>
            <div>
              <label className="text-gray-500">TY</label>
              <input className="input w-full text-xs" type="number" step="0.001" value={datum.translation[1]}
                onChange={e => setDatum({ ...datum, translation: [datum.translation[0], parseFloat(e.target.value) || 0, datum.translation[2]] })} />
            </div>
            <div>
              <label className="text-gray-500">TZ</label>
              <input className="input w-full text-xs" type="number" step="0.001" value={datum.translation[2]}
                onChange={e => setDatum({ ...datum, translation: [datum.translation[0], datum.translation[1], parseFloat(e.target.value) || 0] })} />
            </div>
            <div>
              <label className="text-gray-500">Rot Z (deg)</label>
              <input className="input w-full text-xs" type="number" step="0.1" value={datum.rotationZ}
                onChange={e => setDatum({ ...datum, rotationZ: parseFloat(e.target.value) || 0 })} />
            </div>
          </div>
        </div>
      )}

      {/* Point list */}
      <div className="space-y-2">
        {points.map((p, idx) => (
          <div key={p.id} className="bg-gray-800 rounded-lg p-2 grid grid-cols-12 gap-1 items-center text-xs">
            <span className="col-span-1 text-gray-400 font-mono">{p.label}</span>
            <input className="input col-span-2" type="number" step="0.001" value={p.entered[0]}
              onChange={e => updatePoint(idx, 'x', parseFloat(e.target.value) || 0)} placeholder="X" />
            <input className="input col-span-2" type="number" step="0.001" value={p.entered[1]}
              onChange={e => updatePoint(idx, 'y', parseFloat(e.target.value) || 0)} placeholder="Y" />
            <input className="input col-span-2" type="number" step="0.001" value={p.entered[2]}
              onChange={e => updatePoint(idx, 'z', parseFloat(e.target.value) || 0)} placeholder="Z" />
            <label className="col-span-1 flex items-center gap-0.5 text-gray-500">
              <input type="checkbox" checked={p.locks.x} onChange={e => updatePoint(idx, 'lockX', e.target.checked)} /> X
            </label>
            <label className="col-span-1 flex items-center gap-0.5 text-gray-500">
              <input type="checkbox" checked={p.locks.y} onChange={e => updatePoint(idx, 'lockY', e.target.checked)} /> Y
            </label>
            <label className="col-span-1 flex items-center gap-0.5 text-gray-500">
              <input type="checkbox" checked={p.locks.z} onChange={e => updatePoint(idx, 'lockZ', e.target.checked)} /> Z
            </label>
            <input className="input col-span-1" type="number" step="0.1" min="0" value={p.weight}
              onChange={e => updatePoint(idx, 'weight', parseFloat(e.target.value) || 1)} title="Weight" />
            <button onClick={() => removePoint(idx)} className="text-red-500 hover:text-red-300 text-xs">x</button>
          </div>
        ))}
      </div>

      <div className="flex gap-2">
        <button onClick={addPoint} className="px-3 py-1.5 bg-gray-800 border border-gray-700 text-gray-300 text-xs rounded hover:bg-gray-700">
          + Add Point
        </button>
        <button onClick={handleSubmit} disabled={points.length < 3}
          className="px-4 py-1.5 bg-primary/80 text-white text-xs rounded disabled:opacity-40">
          Send to Core
        </button>
      </div>

      {/* DOF status */}
      <div className={'rounded-lg p-2 text-xs border ' +
        (dofStatus.isComplete ? 'bg-green-950 border-green-800 text-green-300' : 'bg-yellow-950 border-yellow-800 text-yellow-300')}>
        <span className="font-medium">{'DOFs: ' + dofStatus.total + '/6'}</span>
        <span className="ml-2 text-gray-500">{'(X:' + dofStatus.lockedX + ' Y:' + dofStatus.lockedY + ' Z:' + dofStatus.lockedZ + ')'}</span>
        {dofStatus.redundantConstraints > 0 && (
          <span className="ml-2 text-blue-400">{'(' + dofStatus.redundantConstraints + ' redundant)'}</span>
        )}
        {dofStatus.warning && <p className="mt-1 text-xs">{dofStatus.warning}</p>}
      </div>

      {/* Core's returned fit result (display only) */}
      {fitResult && (
        <div className="bg-gray-800 rounded-lg p-3 text-xs space-y-1">
          <p className="text-gray-400 font-medium">Core Fit Result</p>
          <p>{'Verdict: ' + fitResult.verdict_label}</p>
          <p>{'RMS: ' + fitResult.alignment_rms + ' mm'}</p>
          <p>{'Points: ' + fitResult.stats.n_points}</p>
          <p>{'Fully constrained: ' + (fitResult.fully_constrained ? 'Yes' : 'No')}</p>
        </div>
      )}
    </div>
  )
}
