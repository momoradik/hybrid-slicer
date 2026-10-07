/**
 * Landmark pair entry — user enters corresponding points on reference and measured parts.
 * Each row is a pair: (ref x,y,z) ↔ (meas x,y,z).
 * Minimum 3 non-collinear pairs required for Kabsch alignment.
 *
 * COMPUTE FIREWALL: this component collects coordinates only.
 * The core computes the alignment from these points.
 */
import { useState } from 'react'
import type { LandmarkPairData } from '../../api/alignmesh'

interface Props {
  onPairsChanged: (pairs: LandmarkPairData[]) => void
}

interface PairRow {
  id: number
  ref_x: string; ref_y: string; ref_z: string
  meas_x: string; meas_y: string; meas_z: string
}

const emptyRow = (id: number): PairRow => ({
  id, ref_x: '', ref_y: '', ref_z: '', meas_x: '', meas_y: '', meas_z: '',
})

export default function LandmarkPairEntry({ onPairsChanged }: Props) {
  const [rows, setRows] = useState<PairRow[]>([emptyRow(0), emptyRow(1), emptyRow(2)])
  let nextId = rows.length

  const updateRow = (id: number, field: keyof PairRow, value: string) => {
    const updated = rows.map(r => r.id === id ? { ...r, [field]: value } : r)
    setRows(updated)
    // Emit valid pairs to parent.
    const valid = updated
      .filter(r => r.ref_x && r.ref_y && r.ref_z && r.meas_x && r.meas_y && r.meas_z)
      .map(r => ({
        ref_x: parseFloat(r.ref_x) || 0,
        ref_y: parseFloat(r.ref_y) || 0,
        ref_z: parseFloat(r.ref_z) || 0,
        meas_x: parseFloat(r.meas_x) || 0,
        meas_y: parseFloat(r.meas_y) || 0,
        meas_z: parseFloat(r.meas_z) || 0,
        weight: 1.0,
      }))
    onPairsChanged(valid)
  }

  const addRow = () => {
    setRows([...rows, emptyRow(nextId++)])
  }

  const removeRow = (id: number) => {
    if (rows.length <= 3) return
    const updated = rows.filter(r => r.id !== id)
    setRows(updated)
    const valid = updated
      .filter(r => r.ref_x && r.ref_y && r.ref_z && r.meas_x && r.meas_y && r.meas_z)
      .map(r => ({
        ref_x: parseFloat(r.ref_x) || 0, ref_y: parseFloat(r.ref_y) || 0, ref_z: parseFloat(r.ref_z) || 0,
        meas_x: parseFloat(r.meas_x) || 0, meas_y: parseFloat(r.meas_y) || 0, meas_z: parseFloat(r.meas_z) || 0,
        weight: 1.0,
      }))
    onPairsChanged(valid)
  }

  const numCoord = "w-16 px-1.5 py-1 bg-gray-800 border border-gray-700 rounded text-xs text-gray-200 text-center font-mono"

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <h4 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Landmark Pairs</h4>
        <span className="text-[10px] text-gray-500">min. 3 pairs required</span>
      </div>

      <p className="text-xs text-gray-500">
        Enter matching X, Y, Z coordinates from both parts. Use your CAD software or scan viewer to read the coordinates of identifiable features (holes, corners, edges).
      </p>

      {/* Header */}
      <div className="grid grid-cols-[auto_1fr_1fr_auto] gap-2 text-[10px] text-gray-500 uppercase">
        <span className="w-6"></span>
        <span className="text-center text-blue-500">Reference (CAD)</span>
        <span className="text-center text-green-500">Measured (Scan)</span>
        <span className="w-6"></span>
      </div>

      {/* Rows */}
      {rows.map((row, i) => (
        <div key={row.id} className="grid grid-cols-[auto_1fr_1fr_auto] gap-2 items-center">
          <span className="text-xs text-gray-600 w-6 text-right">{i + 1}</span>
          <div className="flex gap-1">
            <input className={numCoord} placeholder="X" value={row.ref_x}
              onChange={e => updateRow(row.id, 'ref_x', e.target.value)} />
            <input className={numCoord} placeholder="Y" value={row.ref_y}
              onChange={e => updateRow(row.id, 'ref_y', e.target.value)} />
            <input className={numCoord} placeholder="Z" value={row.ref_z}
              onChange={e => updateRow(row.id, 'ref_z', e.target.value)} />
          </div>
          <div className="flex gap-1">
            <input className={numCoord} placeholder="X" value={row.meas_x}
              onChange={e => updateRow(row.id, 'meas_x', e.target.value)} />
            <input className={numCoord} placeholder="Y" value={row.meas_y}
              onChange={e => updateRow(row.id, 'meas_y', e.target.value)} />
            <input className={numCoord} placeholder="Z" value={row.meas_z}
              onChange={e => updateRow(row.id, 'meas_z', e.target.value)} />
          </div>
          <button onClick={() => removeRow(row.id)} disabled={rows.length <= 3}
            className="text-red-700 hover:text-red-400 text-xs w-6 disabled:opacity-20">x</button>
        </div>
      ))}

      <button onClick={addRow}
        className="text-xs text-gray-500 hover:text-gray-300 px-2 py-1 rounded bg-gray-800 hover:bg-gray-700 transition">
        + Add pair
      </button>
    </div>
  )
}
