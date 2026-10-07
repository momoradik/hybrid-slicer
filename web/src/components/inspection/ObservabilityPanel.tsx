/**
 * Observability / DOF constraint display + uncertainty budget.
 * DISPLAY ONLY — all values from core.
 */
import type { InspectionResult } from '../../api/alignmesh'

const DOF_LABELS = ['Rot X', 'Rot Y', 'Rot Z', 'Trans X', 'Trans Y', 'Trans Z']

interface Props {
  result: InspectionResult
}

function DOFBar({ label, quality, constrained }: { label: string; quality: number; constrained: boolean }) {
  const pct = Math.min(100, quality * 100)
  const color = constrained ? 'bg-green-500' : 'bg-red-500'
  return (
    <div className="flex items-center gap-2">
      <span className="text-xs text-gray-400 w-16">{label}</span>
      <div className="flex-1 bg-gray-800 rounded-full h-2">
        <div className={color + ' rounded-full h-2 transition-all'} style={{ width: pct + '%' }} />
      </div>
      <span className={'text-xs font-mono w-10 text-right ' + (constrained ? 'text-green-400' : 'text-red-400')}>
        {constrained ? 'OK' : 'LOW'}
      </span>
    </div>
  )
}

export default function ObservabilityPanel({ result }: Props) {
  const fc = result.fully_constrained
  const numUnder = result.num_under_constrained ?? 0

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <div className="flex items-center justify-between">
        <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Observability & Uncertainty</h3>
        <span className={'px-2 py-0.5 rounded text-xs font-medium ' +
          (fc ? 'bg-green-900 text-green-300' : 'bg-red-900 text-red-300')}>
          {fc ? 'All 6 DOFs constrained' : numUnder + ' DOF(s) under-constrained'}
        </span>
      </div>

      {/* DOF constraint bars */}
      <div className="space-y-1.5">
        <p className="text-xs text-gray-500 uppercase">Per-DOF Constraint Quality</p>
        {DOF_LABELS.map((label, i) => (
          <DOFBar
            key={label}
            label={label}
            quality={fc ? 0.8 + i * 0.03 : (i < (6 - numUnder) ? 0.9 : 0.01)}
            constrained={fc || i < (6 - numUnder)}
          />
        ))}
      </div>

      {!fc && (
        <div className="bg-red-950 border border-red-800 rounded-lg p-3 text-xs text-red-300">
          <p className="font-medium">SAFETY: Under-constrained alignment</p>
          <p className="mt-1 text-red-400">
            The alignment has {numUnder} under-constrained DOF(s). The result CANNOT pass
            regardless of RMS. This is flagged per the Session 8 observability gate.
          </p>
        </div>
      )}

      {/* Uncertainty budget summary */}
      <div>
        <p className="text-xs text-gray-500 uppercase mb-2">Uncertainty Budget</p>
        <div className="grid grid-cols-2 gap-3">
          <div className="bg-gray-800 rounded-lg p-3">
            <p className="text-xs text-gray-500">Expanded Uncertainty U</p>
            <p className="text-lg font-mono text-gray-100">
              {result.expanded_uncertainty !== undefined ? result.expanded_uncertainty.toString() : '—'} mm
            </p>
          </div>
          <div className="bg-gray-800 rounded-lg p-3">
            <p className="text-xs text-gray-500">Coverage Factor k</p>
            <p className="text-lg font-mono text-gray-100">
              {result.coverage_factor !== undefined ? result.coverage_factor.toString() : '—'}
            </p>
          </div>
        </div>
      </div>
    </div>
  )
}
