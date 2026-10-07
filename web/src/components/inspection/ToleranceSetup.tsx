/**
 * Tolerance setup: input tolerance, show advisory precision tier.
 * Emits on every change — no confirm button needed.
 */
import { useState, useEffect } from 'react'

const TIER_INFO: Record<string, string> = {
  'COARSE': 'STL format acceptable.',
  'MEDIUM': 'STL allowed if chord error is checked.',
  'FINE': 'High-density mesh or CAD reference required. STL discouraged.',
  'PRECISE': 'Analytic CAD reference + double-precision cloud + characterized scanner required.',
  'ULTRA': 'CAD + certified scanner + 20C thermal control required.',
}

function classifyTier(tol: number): { tier: string; range: string } {
  if (tol >= 0.1) return { tier: 'COARSE', range: '>= 100 um' }
  if (tol >= 0.025) return { tier: 'MEDIUM', range: '25-100 um' }
  if (tol >= 0.01) return { tier: 'FINE', range: '10-25 um' }
  if (tol >= 0.001) return { tier: 'PRECISE', range: '1-10 um' }
  return { tier: 'ULTRA', range: '< 1 um' }
}

interface Props {
  onToleranceConfirmed: (tolerance: number) => void
  initialValue?: number
}

export default function ToleranceSetup({ onToleranceConfirmed, initialValue = 0.1 }: Props) {
  const [tolerance, setTolerance] = useState(String(initialValue))

  const tol = parseFloat(tolerance) || 0
  const tierInfo = tol > 0 ? classifyTier(tol) : null

  // Emit on every valid change.
  useEffect(() => {
    if (tol > 0) onToleranceConfirmed(tol)
  }, [tol])

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Tolerance</h3>
        {tierInfo && (
          <span className="text-[10px] text-gray-500">
            Tier: {tierInfo.tier} ({tierInfo.range})
          </span>
        )}
      </div>

      <div className="flex items-center gap-3">
        <input
          className="w-32 px-3 py-2 bg-gray-800 border border-gray-700 rounded-lg text-sm text-gray-200 font-mono"
          type="number"
          step="0.001"
          min="0.0001"
          value={tolerance}
          onChange={e => setTolerance(e.target.value)}
        />
        <span className="text-xs text-gray-500">mm</span>

        {/* Quick presets */}
        {[0.05, 0.1, 0.2, 0.5].map(v => (
          <button key={v}
            onClick={() => setTolerance(String(v))}
            className={'px-2 py-1 rounded text-[10px] transition ' +
              (tol === v
                ? 'bg-primary/30 text-primary-300 border border-primary/40'
                : 'bg-gray-800 text-gray-500 hover:text-gray-300 border border-gray-700')}
          >
            {v}
          </button>
        ))}
      </div>

      {tierInfo && (
        <p className="text-[10px] text-gray-500 italic">{TIER_INFO[tierInfo.tier]}</p>
      )}
    </div>
  )
}
