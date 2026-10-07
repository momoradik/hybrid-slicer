/**
 * RPS result panel — displays per-point projection status, deviations,
 * conformance, and DOF analysis from the RPS alignment.
 *
 * COMPUTE FIREWALL: all values are pre-computed by the core.
 * This component only displays them.
 */
import type { InspectResponse, RpsPointResult, DofReport, ProjectionStatus, RpsConformance } from '../../api/alignmesh-types.generated'

interface Props {
  result: InspectResponse
}

const STATUS_LABELS: Record<ProjectionStatus, { label: string; cls: string }> = {
  OK: { label: 'OK', cls: 'text-green-400' },
  INSUFFICIENT_DATA: { label: 'Insufficient data', cls: 'text-red-400' },
  AMBIGUOUS_PATCH: { label: 'Ambiguous patch', cls: 'text-yellow-400' },
  GRAZING: { label: 'Grazing', cls: 'text-yellow-400' },
  MULTIPLE_INTERSECTION: { label: 'Multiple intersection', cls: 'text-yellow-400' },
  NO_NORMAL: { label: 'No normal', cls: 'text-red-400' },
}

const CONFORMANCE_LABELS: Record<RpsConformance, { label: string; cls: string }> = {
  PASS: { label: 'PASS', cls: 'text-green-400 font-medium' },
  FAIL: { label: 'FAIL', cls: 'text-red-400 font-medium' },
  UNDECIDED: { label: 'UNDECIDED', cls: 'text-yellow-400' },
}

function PointRow({ pt, index }: { pt: RpsPointResult; index: number }) {
  const status = STATUS_LABELS[pt.projection_status] ?? { label: pt.projection_status, cls: 'text-gray-400' }

  return (
    <div className="grid grid-cols-[auto_1fr_1fr_1fr_1fr] gap-3 items-center text-xs py-1.5 border-b border-gray-800 last:border-0">
      <span className="text-gray-600 w-5 text-right font-mono">{index + 1}</span>
      <div className="font-mono text-gray-300">
        ({pt.measured_position.map(v => v.toFixed(3)).join(', ')})
      </div>
      <span className={status.cls}>{status.label}</span>
      <div className="font-mono text-gray-200">
        {pt.deviation_per_direction.map((d, i) => (
          <span key={i} className={Math.abs(d) > 0.1 ? 'text-yellow-400' : ''}>
            {d >= 0 ? '+' : ''}{d.toFixed(4)}{i < pt.deviation_per_direction.length - 1 ? ', ' : ''}
          </span>
        ))}
      </div>
      <div className="flex gap-1">
        {pt.conformance_per_direction.map((c, i) => {
          const conf = CONFORMANCE_LABELS[c] ?? { label: c, cls: 'text-gray-400' }
          return <span key={i} className={conf.cls + ' text-[10px]'}>{conf.label}</span>
        })}
      </div>
    </div>
  )
}

function DofSummary({ dof }: { dof: DofReport }) {
  return (
    <div className="flex flex-wrap gap-3 text-xs">
      <div className="flex items-center gap-1.5">
        <span className="text-gray-500">Constraints:</span>
        <span className="font-mono text-gray-200">{dof.num_scalar_constraints}</span>
      </div>
      <div className="flex items-center gap-1.5">
        <span className="text-gray-500">Jacobian rank:</span>
        <span className={'font-mono ' + (dof.jacobian_rank === 6 ? 'text-green-400' : 'text-red-400')}>
          {dof.jacobian_rank}/6
        </span>
      </div>
      <div className="flex items-center gap-1.5">
        <span className="text-gray-500">Status:</span>
        <span className={dof.fully_constrained ? 'text-green-400' : 'text-red-400'}>
          {dof.fully_constrained
            ? (dof.over_determined ? 'Over-determined (' + dof.redundant_count + ' redundant)' : 'Fully constrained')
            : 'Under-constrained'}
        </span>
      </div>
      {dof.free_dof_names && dof.free_dof_names.length > 0 && (
        <div className="flex items-center gap-1.5">
          <span className="text-gray-500">Free DOFs:</span>
          <span className="text-red-400 font-mono">{dof.free_dof_names.join(', ')}</span>
        </div>
      )}
    </div>
  )
}

export default function RPSResultPanel({ result }: Props) {
  const rps = result.rps_result
  if (!rps) return null

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <div className="flex items-center justify-between">
        <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
          RPS Alignment Result
        </h3>
        <span className="text-[10px] text-gray-500">
          Stage 2 — conformance-bearing alignment
        </span>
      </div>

      {/* Two-stage visual distinction */}
      <div className="grid grid-cols-2 gap-3">
        <div className="bg-gray-800 border border-gray-700 rounded-lg p-3">
          <div className="text-[10px] text-gray-500 uppercase mb-1">Stage 1: Pre-alignment</div>
          <div className="text-xs text-gray-400">
            Coarse-to-fine registration (initialization only)
          </div>
        </div>
        <div className="bg-blue-950 border border-blue-800 rounded-lg p-3">
          <div className="text-[10px] text-blue-400 uppercase mb-1">Stage 2: RPS Alignment</div>
          <div className="text-xs text-blue-300">
            Conformance-bearing directional constrained fit
          </div>
          <div className="text-[10px] text-blue-500 mt-1 font-mono">
            {rps.convergence.iterations} iterations, {rps.convergence.converged ? 'converged' : 'NOT converged'}
          </div>
        </div>
      </div>

      {/* DOF analysis */}
      <div className="bg-gray-800 border border-gray-700 rounded-lg p-3 space-y-2">
        <div className="text-[10px] text-gray-500 uppercase">DOF Constraint Analysis</div>
        <DofSummary dof={rps.dof_report} />
      </div>

      {/* Per-point table */}
      <div>
        <div className="grid grid-cols-[auto_1fr_1fr_1fr_1fr] gap-3 text-[10px] text-gray-500 uppercase pb-2 border-b border-gray-700">
          <span className="w-5">#</span>
          <span>Measured position (mm)</span>
          <span>Projection status</span>
          <span>Deviations (mm)</span>
          <span>Conformance</span>
        </div>
        {rps.per_point.map((pt, i) => (
          <PointRow key={pt.id || i} pt={pt} index={i} />
        ))}
      </div>

      {/* Convergence warning */}
      {!rps.convergence.converged && (
        <div className="bg-red-950 border border-red-800 rounded-lg p-3 text-xs text-red-300">
          RPS solver did NOT converge. Result may not be reliable.
        </div>
      )}
    </div>
  )
}
