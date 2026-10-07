/**
 * Alignment setup: choose mode + rotation search angle.
 * SETUP SCREEN — sends mode and settings to core, core computes alignment.
 * UI never solves alignment or fitting math.
 */
import { useState } from 'react'

type AlignmentMode = 'coarse-to-fine' | 'best-fit' | 'pre-aligned-rps' | 'landmark'

interface Props {
  onModeSelected: (mode: AlignmentMode) => void
  onAngleStepChanged?: (degrees: number) => void
  currentMode: string
  angleStep?: number
}

interface ModeInfo {
  value: AlignmentMode
  label: string
  desc: string
  enabled: boolean
  disabledReason?: string
}

const MODES: ModeInfo[] = [
  { value: 'coarse-to-fine', label: 'Coarse-to-Fine Registration', desc: 'Global registration (TEASER++) → GICP fine registration (small_gicp). Best for unknown initial orientation.', enabled: true },
  { value: 'best-fit', label: 'Best-Fit (ICP)', desc: 'Minimize overall surface deviation. Engineering/process use — NOT for datum-controlled features.', enabled: true },
  { value: 'pre-aligned-rps', label: 'Pre-aligned RPS Alignment', desc: 'Coarse-to-fine pre-alignment → 3-2-1 Reference Point System constrained fit (ISO 5459). Primary conformance mode for datum-controlled inspection.', enabled: true },
  { value: 'landmark', label: 'Landmark', desc: 'Align from user-selected matching points on both parts (Kabsch + ICP).', enabled: true },
]

/** Candidate count: core uses n_steps = round(360/angle), candidates = n_steps^3 */
function candidateCount(deg: number): number {
  if (deg <= 0) return 0
  const n = Math.max(1, Math.round(360 / deg))
  return n * n * n
}

/** Rough time estimate. Baseline: 216 candidates (30°) ≈ 20s on a typical machine. */
function estimateTime(candidates: number): string {
  const seconds = (candidates / 216) * 20
  if (seconds < 60) return '~' + Math.round(seconds) + 's'
  if (seconds < 3600) return '~' + Math.round(seconds / 60) + 'min'
  const hrs = seconds / 3600
  return '~' + hrs.toFixed(1) + 'h'
}

export default function AlignmentSetup({ onModeSelected, onAngleStepChanged, currentMode, angleStep = 30 }: Props) {
  const [selected, setSelected] = useState<AlignmentMode>(currentMode as AlignmentMode || 'coarse-to-fine')
  const [angle, setAngle] = useState(angleStep)

  const candidates = candidateCount(angle)
  const timeEst = estimateTime(candidates)

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">Alignment Mode</h3>

      <div className="grid grid-cols-1 md:grid-cols-2 gap-3">
        {MODES.map(m => (
          <button
            key={m.value}
            onClick={() => {
              if (!m.enabled) return
              setSelected(m.value)
              onModeSelected(m.value)
            }}
            disabled={!m.enabled}
            className={'rounded-lg border p-3 text-left transition ' +
              (!m.enabled
                ? 'bg-gray-900 border-gray-800 text-gray-600 cursor-not-allowed opacity-50'
                : selected === m.value
                  ? 'bg-primary/20 border-primary/50 text-primary-300'
                  : 'bg-gray-800 border-gray-700 text-gray-400 hover:bg-gray-750 hover:border-gray-600')}
          >
            <div className="flex items-center gap-2 mb-1">
              <span className="font-medium text-sm">{m.label}</span>
            </div>
            <p className="text-xs">{m.desc}</p>
            {!m.enabled && m.disabledReason && (
              <p className="text-[10px] text-yellow-600 mt-1 italic">{m.disabledReason}</p>
            )}
          </button>
        ))}
      </div>

      {/* Rotation search angle — shown for coarse-to-fine, best-fit, and pre-aligned RPS (Stage 1) */}
      {(selected === 'coarse-to-fine' || selected === 'best-fit' || selected === 'pre-aligned-rps') && (
        <div className="bg-gray-800 border border-gray-700 rounded-lg p-3 space-y-3">
          <div className="flex items-center justify-between">
            <span className="text-xs text-gray-400">Rotation search step</span>
            <span className="text-xs text-gray-300 font-mono">
              {angle}° — {candidates.toLocaleString()} orientations — {timeEst}
            </span>
          </div>
          <input
            type="range"
            min={1}
            max={90}
            step={1}
            value={angle}
            onChange={e => {
              const v = parseInt(e.target.value)
              setAngle(v)
              onAngleStepChanged?.(v)
            }}
            className="w-full accent-primary"
          />
          <div className="flex justify-between text-[10px] text-gray-600">
            <span>1° (slow)</span>
            <span>30°</span>
            <span>90° (fast)</span>
          </div>
          {angle < 10 && (
            <div className="bg-yellow-950 border border-yellow-800 rounded-lg p-2 text-[11px] text-yellow-300">
              Below 10° generates {candidates.toLocaleString()} candidates — estimated {timeEst}. Consider 15–30° for most parts.
            </div>
          )}
        </div>
      )}

      {selected === 'best-fit' && (
        <div className="bg-yellow-950 border border-yellow-800 rounded-lg p-3 text-xs text-yellow-300">
          <p className="font-medium">Best-fit warning</p>
          <p className="mt-1 text-yellow-400">
            Best-fit alignment smears deviations across the surface and can hide localized defects.
            Use datum-constrained alignment for GD&T conformance decisions.
          </p>
        </div>
      )}
    </div>
  )
}
