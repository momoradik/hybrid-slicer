/**
 * Before/after alignment overlay controls.
 *
 * COMPUTE FIREWALL: the applied matrix is the core's transform_matrix
 * verbatim. No alignment is derived in the UI.
 *
 * Toggle states: reference only / measured only / overlay (both).
 */
import type { InspectResponse } from '../../api/alignmesh-types.generated'

export type OverlayMode = 'reference' | 'measured' | 'overlay'

interface Props {
  result: InspectResponse
  mode?: OverlayMode
  onModeChange?: (mode: OverlayMode) => void
}

/** Extract the overlay state for test queries. */
export interface OverlayState {
  mode: OverlayMode
  appliedMatrix: number[]
  referenceVisible: boolean
  measuredVisible: boolean
  matrixMatchesCore: boolean
}

export function getOverlayState(result: InspectResponse, mode: OverlayMode): OverlayState {
  const m = result.transform_matrix ?? []
  return {
    mode,
    appliedMatrix: m,
    referenceVisible: mode === 'reference' || mode === 'overlay',
    measuredVisible: mode === 'measured' || mode === 'overlay',
    matrixMatchesCore: m.length === 16 && m === result.transform_matrix,
  }
}

export default function AlignmentOverlay({ result, mode: externalMode, onModeChange }: Props) {
  const mode = externalMode ?? 'overlay'

  const m = result.transform_matrix
  const hasTransform = m && m.length === 16

  const modes: { id: OverlayMode; label: string; color: string }[] = [
    { id: 'reference', label: 'Reference', color: 'text-blue-400' },
    { id: 'measured', label: 'Measured', color: 'text-green-400' },
    { id: 'overlay', label: 'Overlay', color: 'text-purple-400' },
  ]

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <h4 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
          Alignment Overlay
        </h4>
        <div className="flex gap-1 bg-gray-800 rounded-lg p-0.5">
          {modes.map(md => (
            <button
              key={md.id}
              onClick={() => onModeChange?.(md.id)}
              data-testid={'overlay-' + md.id}
              className={'px-3 py-1 rounded text-xs transition ' +
                (mode === md.id
                  ? 'bg-primary/30 font-medium ' + md.color
                  : 'text-gray-500 hover:text-gray-300')}
            >
              {md.label}
            </button>
          ))}
        </div>
      </div>

      {/* Visual indicator of what's shown */}
      <div className="flex gap-4 text-xs">
        <span className={mode === 'reference' || mode === 'overlay' ? 'text-blue-400' : 'text-gray-700'}>
          {'Reference ' + (mode === 'reference' || mode === 'overlay' ? '(visible)' : '(hidden)')}
        </span>
        <span className={mode === 'measured' || mode === 'overlay' ? 'text-green-400' : 'text-gray-700'}>
          {'Measured ' + (mode === 'measured' || mode === 'overlay' ? '(visible)' : '(hidden)')}
        </span>
      </div>

      {/* Transform info — verbatim from core */}
      {hasTransform && (
        <div className="text-[10px] text-gray-600 font-mono bg-gray-950 rounded p-2">
          <p className="text-gray-500 mb-1">Applied transform (from core, verbatim):</p>
          {[0, 1, 2, 3].map(row => (
            <p key={row}>
              {'[' + m.slice(row * 4, row * 4 + 4).map((v: number) => v.toFixed(6)).join('  ') + ']'}
            </p>
          ))}
        </div>
      )}

      {/* Residual summary */}
      <div className="flex gap-4 text-xs text-gray-400">
        <span>{'Alignment RMS: ' + result.alignment_rms + ' mm'}</span>
        <span>{'Mode: ' + result.alignment_mode}</span>
      </div>
    </div>
  )
}
