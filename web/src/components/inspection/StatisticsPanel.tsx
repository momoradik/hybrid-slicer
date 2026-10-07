/**
 * Full deviation statistics with CIs, correlation caveat, guard-banded zone.
 * DISPLAY ONLY — all values from the core.
 */
import type { InspectionResult } from '../../api/alignmesh'

function Row({ label, value, unit = 'mm', warn }: { label: string; value: number | string; unit?: string; warn?: boolean }) {
  return (
    <div className={'flex justify-between items-center py-1.5 border-b border-gray-800 last:border-0 ' + (warn ? 'text-yellow-300' : '')}>
      <span className="text-gray-400 text-sm">{label}</span>
      <span className="text-gray-100 font-mono text-sm">{typeof value === 'number' ? value.toString() : value} {unit}</span>
    </div>
  )
}

export default function StatisticsPanel({ result }: { result: InspectionResult }) {
  const s = result.stats
  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Deviation Statistics</h3>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-6">
        <div>
          <p className="text-xs text-gray-500 mb-2 uppercase">Core Statistics</p>
          <Row label="N points" value={s.n_points} unit="" />
          <Row label="Mean" value={s.mean} />
          <Row label="RMS" value={s.rms} />
          <Row label="Max" value={s.max} />
          <Row label="Std Dev" value={s.std_dev} />
        </div>

        <div>
          <p className="text-xs text-gray-500 mb-2 uppercase">Tolerance Assessment</p>
          <Row label="Tolerance" value={result.tolerance_mm} />
          <Row label="Within tolerance" value={s.percent_within_tolerance} unit="%" />
          <Row label="Acceptance zone" value={'[' + result.acceptance_lower + ', ' + result.acceptance_upper + ']'} />
          <Row label="Verdict" value={result.verdict_label} unit="" warn={result.verdict !== 'PASS'} />
        </div>
      </div>

      {/* Spatial-autocorrelation caveat */}
      <div className="bg-gray-800 border border-gray-700 rounded-lg p-3 text-xs text-gray-400">
        <p className="font-medium text-gray-300 mb-1">Spatial-autocorrelation caveat</p>
        <p>Neighbouring measurement points are correlated. The effective sample size is smaller than N.
           Confidence intervals account for this — do not over-claim precision from a large point count.</p>
      </div>
    </div>
  )
}
