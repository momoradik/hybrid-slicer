/**
 * Verdict panel — PASS/WARNING/FAIL/INVALID with decision-rule details.
 *
 * DISPLAY ONLY: renders the core's verdict verbatim.
 * WARNING and INVALID are visually distinct from PASS — the styles are
 * hardcoded per verdict, not user-configurable, so a WARNING can never
 * be styled to look like a PASS.
 */
import type { Verdict, InspectionResult } from '../../api/alignmesh'

/* Hardcoded per-verdict styles — NOT configurable. */
const VERDICT_PANEL: Record<Verdict, { bg: string; border: string; text: string; icon: string }> = {
  PASS:    { bg: 'bg-green-900',  border: 'border-green-600', text: 'text-green-200', icon: '✓' },
  WARNING: { bg: 'bg-yellow-900', border: 'border-yellow-600', text: 'text-yellow-200', icon: '⚠' },
  FAIL:    { bg: 'bg-red-900',    border: 'border-red-600',    text: 'text-red-200',    icon: '✗' },
  INVALID: { bg: 'bg-gray-800',   border: 'border-gray-500',   text: 'text-gray-300',   icon: '—' },
}

export default function VerdictPanel({ result }: { result: InspectionResult }) {
  const v = result.verdict
  const s = VERDICT_PANEL[v]

  return (
    <div className={'rounded-xl border-2 p-6 ' + s.bg + ' ' + s.border}>
      {/* Large verdict display */}
      <div className="flex items-center gap-4">
        <span className={'text-5xl font-black ' + s.text}>{s.icon}</span>
        <div>
          <p className={'text-4xl font-black tracking-tight ' + s.text}>{result.verdict_label}</p>
          <p className="text-sm text-gray-400 mt-1">
            {'Tolerance: ' + result.tolerance_mm + ' mm | Tier: ' + result.precision_tier}
          </p>
        </div>
      </div>

      {/* Decision rule details */}
      <div className="mt-4 grid grid-cols-2 md:grid-cols-4 gap-3 text-xs">
        <div className="bg-black/20 rounded-lg p-2">
          <p className="text-gray-500">Acceptance zone</p>
          <p className={'font-mono ' + s.text}>
            {'[' + (result.acceptance_lower ?? '?') + ', ' + (result.acceptance_upper ?? '?') + ']'}
          </p>
        </div>
        <div className="bg-black/20 rounded-lg p-2">
          <p className="text-gray-500">Alignment</p>
          <p className={'font-mono ' + s.text}>{result.alignment_mode}</p>
        </div>
        <div className="bg-black/20 rounded-lg p-2">
          <p className="text-gray-500">Align RMS</p>
          <p className={'font-mono ' + s.text}>{result.alignment_rms + ' mm'}</p>
        </div>
        <div className="bg-black/20 rounded-lg p-2">
          <p className="text-gray-500">Max deviation</p>
          <p className={'font-mono ' + s.text}>{result.stats.max + ' mm'}</p>
        </div>
      </div>

      {/* Safety labels for non-PASS */}
      {v === 'WARNING' && (
        <div className="mt-3 text-xs text-yellow-400 bg-yellow-950 rounded-lg p-2 border border-yellow-800">
          Inside tolerance but within U of a limit — conformance NOT proven. Cannot PASS.
        </div>
      )}
      {v === 'INVALID' && (
        <div className="mt-3 text-xs text-gray-400 bg-gray-900 rounded-lg p-2 border border-gray-700">
          Measurement inadequacy — a quality gate failed. Not a part defect finding.
        </div>
      )}
      {v === 'FAIL' && (
        <div className="mt-3 text-xs text-red-400 bg-red-950 rounded-lg p-2 border border-red-800">
          Outside the guard-banded acceptance zone. Non-conformance proven.
        </div>
      )}
    </div>
  )
}
