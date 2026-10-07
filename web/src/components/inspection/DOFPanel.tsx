/**
 * Per-DOF observability panel with under-constrained flagging.
 * DISPLAY ONLY: all values from the core response.
 */
import type { InspectResponse } from '../../api/alignmesh-types.generated'

const DOF_LABELS = ['Rot X', 'Rot Y', 'Rot Z', 'Trans X', 'Trans Y', 'Trans Z']

export interface DOFState {
  label: string
  constrained: boolean
}

/** Extract DOF states from a core response for test queries. */
export function getDOFStates(result: InspectResponse): DOFState[] {
  const fc = result.fully_constrained
  const numUnder = result.num_under_constrained ?? 0
  return DOF_LABELS.map((label, i) => ({
    label,
    constrained: fc || i < (6 - numUnder),
  }))
}

interface Props {
  result: InspectResponse
}

export default function DOFPanel({ result }: Props) {
  const dofs = getDOFStates(result)
  const fc = result.fully_constrained
  const numUnder = result.num_under_constrained ?? 0

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <h4 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
          DOF Observability
        </h4>
        <span
          data-testid="dof-status"
          className={'px-2 py-0.5 rounded text-xs font-medium ' +
            (fc ? 'bg-green-900 text-green-300' : 'bg-red-900 text-red-300')}
        >
          {fc ? 'All 6 DOFs constrained' : numUnder + ' DOF(s) under-constrained'}
        </span>
      </div>

      <div className="grid grid-cols-6 gap-2">
        {dofs.map(dof => (
          <div
            key={dof.label}
            data-testid={'dof-' + dof.label.replace(' ', '-').toLowerCase()}
            className={'rounded-lg p-2 text-center text-xs border ' +
              (dof.constrained
                ? 'bg-green-950 border-green-800 text-green-300'
                : 'bg-red-950 border-red-800 text-red-300 animate-pulse')}
          >
            <p className="font-medium">{dof.label}</p>
            <p className="text-[10px] mt-0.5">{dof.constrained ? 'OK' : 'UNDER'}</p>
          </div>
        ))}
      </div>

      {!fc && (
        <div className="bg-red-950 border border-red-800 rounded-lg p-2 text-xs text-red-300">
          <p className="font-medium">SAFETY: alignment has under-constrained DOFs</p>
          <p className="mt-1 text-red-400">
            Result CANNOT pass regardless of RMS (Session 8 observability gate).
          </p>
        </div>
      )}
    </div>
  )
}
