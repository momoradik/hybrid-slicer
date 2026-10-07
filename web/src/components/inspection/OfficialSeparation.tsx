/**
 * Official / AI / Override separation panel.
 *
 * Spec section 16: official deterministic results, AI/heuristic suggestions,
 * user overrides, and warnings/blocked claims must be visually segregated
 * and labeled. An AI/heuristic suggestion must NEVER appear as an official value.
 */
import type { InspectionResult } from '../../api/alignmesh'

interface Props {
  result: InspectionResult
  /** Any AI/heuristic suggestions returned by the core. */
  suggestions?: { text: string; confidence: number }[]
  /** Any user overrides applied. */
  overrides?: { field: string; original: string; overridden: string }[]
}

export default function OfficialSeparation({ result, suggestions = [], overrides = [] }: Props) {
  return (
    <div className="space-y-3">

      {/* ── OFFICIAL RESULT (deterministic, from core) ────────────── */}
      <div className="bg-gray-900 border-2 border-green-800 rounded-xl p-4">
        <div className="flex items-center gap-2 mb-3">
          <span className="px-2 py-0.5 bg-green-900 text-green-300 text-[10px] font-bold uppercase rounded">
            Official — Deterministic
          </span>
        </div>
        <div className="grid grid-cols-2 md:grid-cols-4 gap-3 text-xs">
          <div>
            <p className="text-gray-500">Verdict</p>
            <p className="text-gray-100 font-mono font-bold">{result.verdict_label}</p>
          </div>
          <div>
            <p className="text-gray-500">RMS</p>
            <p className="text-gray-100 font-mono">{result.stats.rms + ' mm'}</p>
          </div>
          <div>
            <p className="text-gray-500">Max deviation</p>
            <p className="text-gray-100 font-mono">{result.stats.max + ' mm'}</p>
          </div>
          <div>
            <p className="text-gray-500">Within tolerance</p>
            <p className="text-gray-100 font-mono">{result.stats.percent_within_tolerance + '%'}</p>
          </div>
        </div>
        <p className="text-[10px] text-gray-600 mt-2">
          {'Core: ' + result.core_version + ' | ' + result.fingerprint.compiler}
        </p>
      </div>

      {/* ── AI / HEURISTIC SUGGESTIONS (non-authoritative) ─────────── */}
      {suggestions.length > 0 && (
        <div className="bg-gray-900 border border-purple-800 rounded-xl p-4">
          <div className="flex items-center gap-2 mb-2">
            <span className="px-2 py-0.5 bg-purple-900 text-purple-300 text-[10px] font-bold uppercase rounded">
              Heuristic — Not Official
            </span>
          </div>
          <ul className="text-xs text-purple-300 space-y-1">
            {suggestions.map((s, i) => (
              <li key={i} className="flex justify-between">
                <span>{s.text}</span>
                <span className="text-purple-500">{'confidence: ' + (s.confidence * 100).toFixed(0) + '%'}</span>
              </li>
            ))}
          </ul>
          <p className="text-[10px] text-purple-600 mt-2 italic">
            Suggestions are deterministic heuristics, not AI predictions. They do not affect the official verdict.
          </p>
        </div>
      )}

      {/* ── USER OVERRIDES (if any) ────────────────────────────────── */}
      {overrides.length > 0 && (
        <div className="bg-gray-900 border border-orange-800 rounded-xl p-4">
          <div className="flex items-center gap-2 mb-2">
            <span className="px-2 py-0.5 bg-orange-900 text-orange-300 text-[10px] font-bold uppercase rounded">
              User Override — Audit Required
            </span>
          </div>
          <ul className="text-xs text-orange-300 space-y-1">
            {overrides.map((o, i) => (
              <li key={i}>
                {o.field + ': ' + o.original + ' -> ' + o.overridden}
              </li>
            ))}
          </ul>
        </div>
      )}

      {/* ── WARNINGS / BLOCKED CLAIMS ──────────────────────────────── */}
      {result.warnings.length > 0 && (
        <div className="bg-gray-900 border border-yellow-800 rounded-xl p-4">
          <div className="flex items-center gap-2 mb-2">
            <span className="px-2 py-0.5 bg-yellow-900 text-yellow-300 text-[10px] font-bold uppercase rounded">
              Warnings / Blocked Claims
            </span>
          </div>
          <ul className="text-xs text-yellow-300 space-y-1">
            {result.warnings.map((w, i) => <li key={i}>{'- ' + w}</li>)}
          </ul>
        </div>
      )}
    </div>
  )
}
