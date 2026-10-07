/**
 * Import status bar — shows format, counts, tier, validation from core.
 * DISPLAY ONLY: all values from the core response.
 */
import type { InspectResponse } from '../../api/alignmesh-types.generated'

interface Props {
  /** The core response (contains metadata about imported files). */
  result?: InspectResponse | null
  refPath: string
  measPath: string
  refFormat: string
  measFormat: string
}

export default function ImportStatusBar({ result, refPath, measPath, refFormat, measFormat }: Props) {
  const tierGatePassed = result ? result.precision_tier !== '' : true
  const hasInvalid = result?.errors?.some(e =>
    e.includes('INVALID') || e.includes('format not allowed') || e.includes('Precision gate')
  )

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-2">
      <h4 className="text-xs text-gray-500 uppercase font-medium">Import Status</h4>

      <div className="grid grid-cols-2 gap-3 text-xs">
        {/* Reference */}
        <div className="space-y-1">
          <p className="text-blue-400 font-medium">Reference</p>
          <p className="text-gray-400 truncate">{refPath || '—'}</p>
          {refFormat && (
            <span className="inline-block px-2 py-0.5 bg-gray-800 border border-gray-700 rounded text-gray-300">
              {refFormat}
            </span>
          )}
          {result && (
            <>
              <p className="text-gray-500">
                {'Hash: '}<span className="font-mono text-[10px]">{result.reference_hash?.slice(0, 16) || '—'}...</span>
              </p>
            </>
          )}
        </div>

        {/* Measured */}
        <div className="space-y-1">
          <p className="text-green-400 font-medium">Measured</p>
          <p className="text-gray-400 truncate">{measPath || '—'}</p>
          {measFormat && (
            <span className="inline-block px-2 py-0.5 bg-gray-800 border border-gray-700 rounded text-gray-300">
              {measFormat}
            </span>
          )}
          {result && (
            <>
              <p className="text-gray-500">
                {'Hash: '}<span className="font-mono text-[10px]">{result.measured_hash?.slice(0, 16) || '—'}...</span>
              </p>
            </>
          )}
        </div>
      </div>

      {/* Tier + validation */}
      {result && (
        <div className="flex items-center gap-3 pt-1 border-t border-gray-800">
          <span className="text-xs text-gray-400">{'Tier: ' + (result.precision_tier || '—')}</span>
          <span className="text-xs text-gray-400">{'Points: ' + result.stats.n_points}</span>
          {tierGatePassed && !hasInvalid && (
            <span className="px-2 py-0.5 bg-green-900 text-green-300 text-[10px] rounded">Gate OK</span>
          )}
          {hasInvalid && (
            <span className="px-2 py-0.5 bg-red-900 text-red-300 text-[10px] rounded">INVALID CLAIM</span>
          )}
        </div>
      )}

      {/* Core warnings about format/tier */}
      {result?.warnings?.filter(w => w.includes('Precision') || w.includes('format') || w.includes('STL')).map((w, i) => (
        <p key={i} className="text-xs text-yellow-400 bg-yellow-950 rounded px-2 py-1">
          {w}
        </p>
      ))}
    </div>
  )
}
