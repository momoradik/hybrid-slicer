/**
 * PipelineSequencer — UI for sequencing alignment stages.
 *
 * COMPUTE FIREWALL:
 *   The user picks stages and their order. The core executes and composes.
 *   This component displays the core's per-stage results and composed
 *   final transform verbatim. No alignment math happens here.
 *
 * SAFETY FENCE (ENFORCED):
 *   Best-fit results are labeled "Engineering / process result — NOT for
 *   datum conformance" and structurally cannot be presented as conformance.
 *   The composition order displayed is the core's exact execution order.
 */
import { useState } from 'react'
import {
  type StageKind, type StageDefinition, type PipelineResult,
  type VerdictScope,
  AVAILABLE_STAGES, STAGE_LABELS,
  classifyVerdictScope, getVerdictScopeLabel, isEngineeringOnly,
  buildPipelinePayload,
} from './pipeline-sequencer'

interface Props {
  /** Called when user submits the pipeline for execution. */
  onExecute?: (payload: ReturnType<typeof buildPipelinePayload>) => void
  /** Pipeline result from the core (null = not yet run). */
  result?: PipelineResult | null
}

export default function PipelineSequencer({ onExecute, result }: Props) {
  const [sequence, setSequence] = useState<StageDefinition[]>([])
  const [activeStageView, setActiveStageView] = useState<number>(0)

  // ── Stage management ────────────────────────────────────────────────
  const addStage = (kind: StageKind) => {
    setSequence(prev => [...prev, { kind, label: STAGE_LABELS[kind] }])
  }

  const removeStage = (index: number) => {
    setSequence(prev => prev.filter((_, i) => i !== index))
  }

  const moveStage = (from: number, direction: 'up' | 'down') => {
    setSequence(prev => {
      const next = [...prev]
      const to = direction === 'up' ? from - 1 : from + 1
      if (to < 0 || to >= next.length) return prev
      ;[next[from], next[to]] = [next[to], next[from]]
      return next
    })
  }

  const handleExecute = () => {
    if (sequence.length === 0 || !onExecute) return
    onExecute(buildPipelinePayload(sequence))
  }

  // ── Pre-execution: scope preview ────────────────────────────────────
  const previewScope: VerdictScope = sequence.length === 0
    ? 'engineering'
    : classifyVerdictScope(sequence.map((s, i) => ({
        index: i, kind: s.kind, name: s.label,
        transform: [], rms: 0, converged: true, warnings: [],
      })))

  return (
    <div className="bg-gray-900 border border-gray-800 rounded-xl p-5 space-y-4">
      <h3 className="text-sm font-medium text-gray-300 uppercase tracking-wider">
        Alignment Pipeline Sequencer
      </h3>

      {/* ── Stage picker ─────────────────────────────────────────────── */}
      <div className="space-y-2">
        <p className="text-xs text-gray-500">Add stages (execution order = top to bottom):</p>
        <div className="flex flex-wrap gap-2">
          {AVAILABLE_STAGES.map(s => (
            <button
              key={s.kind}
              onClick={() => addStage(s.kind)}
              data-testid={'add-stage-' + s.kind}
              className="px-3 py-1.5 rounded-lg text-xs bg-gray-800 border border-gray-700
                         text-gray-300 hover:bg-gray-750 hover:border-gray-600 transition"
            >
              + {s.label}
              {isEngineeringOnly(s.kind) && (
                <span className="ml-1 text-yellow-500">(eng.)</span>
              )}
            </button>
          ))}
        </div>
      </div>

      {/* ── Current sequence ──────────────────────────────────────────── */}
      {sequence.length > 0 && (
        <div className="space-y-2">
          <p className="text-xs text-gray-500">Pipeline sequence (core executes in this order):</p>
          <ol className="space-y-1">
            {sequence.map((stage, i) => (
              <li
                key={i}
                data-testid={'pipeline-stage-' + i}
                className={'flex items-center gap-2 rounded-lg border px-3 py-2 text-xs ' +
                  (isEngineeringOnly(stage.kind)
                    ? 'bg-yellow-950/30 border-yellow-900 text-yellow-300'
                    : 'bg-gray-800 border-gray-700 text-gray-300')}
              >
                <span className="font-mono text-gray-600 w-5">{i + 1}.</span>
                <span className="flex-1 font-medium">{stage.label}</span>
                {isEngineeringOnly(stage.kind) && (
                  <span className="text-[10px] text-yellow-500 uppercase">eng. only</span>
                )}
                <button onClick={() => moveStage(i, 'up')} disabled={i === 0}
                  className="text-gray-600 hover:text-gray-300 disabled:opacity-30">
                  {'<'}
                </button>
                <button onClick={() => moveStage(i, 'down')} disabled={i === sequence.length - 1}
                  className="text-gray-600 hover:text-gray-300 disabled:opacity-30">
                  {'>'}
                </button>
                <button onClick={() => removeStage(i)}
                  className="text-red-700 hover:text-red-400 ml-1">
                  x
                </button>
              </li>
            ))}
          </ol>

          {/* Scope preview */}
          <div data-testid="verdict-scope-label" className={
            'rounded-lg border px-3 py-2 text-xs font-medium ' +
            (previewScope === 'conformance'
              ? 'bg-green-950 border-green-800 text-green-300'
              : 'bg-yellow-950 border-yellow-800 text-yellow-300')
          }>
            {getVerdictScopeLabel(previewScope)}
          </div>

          <button
            onClick={handleExecute}
            data-testid="execute-pipeline"
            className="w-full py-2 rounded-lg bg-primary/80 text-sm font-medium
                       text-white hover:bg-primary transition"
          >
            Execute Pipeline ({sequence.length} stage{sequence.length > 1 ? 's' : ''})
          </button>
        </div>
      )}

      {/* ── Results (from core) ───────────────────────────────────────── */}
      {result && (
        <div className="space-y-3 border-t border-gray-800 pt-4">
          <h4 className="text-xs font-medium text-gray-400 uppercase tracking-wider">
            Pipeline Results (from core)
          </h4>

          {/* Verdict scope — ENFORCED label */}
          <div data-testid="result-verdict-scope" className={
            'rounded-lg border px-3 py-2 text-xs font-medium ' +
            (result.verdictScope === 'conformance'
              ? 'bg-green-950 border-green-800 text-green-300'
              : 'bg-yellow-950 border-yellow-800 text-yellow-300')
          }>
            {getVerdictScopeLabel(result.verdictScope)}
          </div>

          {/* Per-stage step-through */}
          <div className="flex gap-1 bg-gray-800 rounded-lg p-0.5">
            {result.stages.map((stage, i) => (
              <button
                key={i}
                onClick={() => setActiveStageView(i)}
                data-testid={'view-stage-' + i}
                className={'px-3 py-1 rounded text-xs transition ' +
                  (activeStageView === i
                    ? 'bg-primary/30 font-medium text-primary-300'
                    : 'text-gray-500 hover:text-gray-300')}
              >
                {i + 1}. {stage.name}
              </button>
            ))}
            <button
              onClick={() => setActiveStageView(-1)}
              data-testid="view-composed"
              className={'px-3 py-1 rounded text-xs transition ' +
                (activeStageView === -1
                  ? 'bg-primary/30 font-medium text-purple-300'
                  : 'text-gray-500 hover:text-gray-300')}
            >
              Composed
            </button>
          </div>

          {/* Active stage detail */}
          {activeStageView >= 0 && activeStageView < result.stages.length && (() => {
            const stage = result.stages[activeStageView]
            return (
              <div data-testid="stage-detail" className="bg-gray-800 rounded-lg p-3 space-y-2">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-medium text-gray-300">
                    Stage {stage.index + 1}: {stage.name}
                  </span>
                  <span className={'text-[10px] px-2 py-0.5 rounded ' +
                    (stage.converged
                      ? 'bg-green-900 text-green-300'
                      : 'bg-red-900 text-red-300')}>
                    {stage.converged ? 'Converged' : 'NOT CONVERGED'}
                  </span>
                </div>
                <div className="text-xs text-gray-400">
                  RMS: {stage.rms.toFixed(6)} mm |
                  Kind: {stage.kind}
                  {isEngineeringOnly(stage.kind) && (
                    <span className="ml-2 text-yellow-500">(engineering only)</span>
                  )}
                </div>
                {stage.warnings.length > 0 && (
                  <div className="text-[10px] text-yellow-400 space-y-0.5">
                    {stage.warnings.map((w, wi) => <p key={wi}>{w}</p>)}
                  </div>
                )}
                {/* Stage transform — verbatim from core */}
                <div className="text-[10px] text-gray-600 font-mono bg-gray-950 rounded p-2">
                  <p className="text-gray-500 mb-1">Stage transform (from core):</p>
                  {[0, 1, 2, 3].map(row => (
                    <p key={row}>
                      {'[' + stage.transform.slice(row * 4, row * 4 + 4)
                        .map(v => v.toFixed(6)).join('  ') + ']'}
                    </p>
                  ))}
                </div>
              </div>
            )
          })()}

          {/* Composed final */}
          {activeStageView === -1 && (
            <div data-testid="composed-detail" className="bg-gray-800 rounded-lg p-3 space-y-2">
              <div className="flex items-center justify-between">
                <span className="text-xs font-medium text-gray-300">
                  Composed Final ({result.stages.length} stages)
                </span>
                <span className={'text-[10px] px-2 py-0.5 rounded ' +
                  (result.allConverged
                    ? 'bg-green-900 text-green-300'
                    : 'bg-red-900 text-red-300')}>
                  {result.allConverged ? 'All Converged' : 'CONVERGENCE ISSUE'}
                </span>
              </div>
              <div className="text-xs text-gray-400">
                Composed RMS: {result.composedRms.toFixed(6)} mm
              </div>
              {/* Composition chain display */}
              <div className="text-[10px] text-gray-500">
                Composition order: {result.stages.map((s, i) =>
                  (i > 0 ? ' x ' : '') + 'T' + (i + 1) + '(' + s.name + ')'
                ).reverse().join('')}
              </div>
              {/* Composed transform — verbatim from core */}
              <div className="text-[10px] text-gray-600 font-mono bg-gray-950 rounded p-2">
                <p className="text-gray-500 mb-1">Composed transform (from core, verbatim):</p>
                {[0, 1, 2, 3].map(row => (
                  <p key={row}>
                    {'[' + result.composedTransform.slice(row * 4, row * 4 + 4)
                      .map(v => v.toFixed(6)).join('  ') + ']'}
                  </p>
                ))}
              </div>
            </div>
          )}

          {/* Overall warnings */}
          {result.warnings.length > 0 && (
            <div className="bg-yellow-950 border border-yellow-800 rounded-lg p-2 text-xs text-yellow-300 space-y-0.5">
              {result.warnings.map((w, i) => <p key={i}>{w}</p>)}
            </div>
          )}

          {/* Safety fence: best-fit-only pipeline warning */}
          {result.verdictScope === 'engineering' && (
            <div data-testid="engineering-fence"
              className="bg-red-950 border border-red-800 rounded-lg p-3 text-xs text-red-300">
              <p className="font-medium">SAFETY: Engineering result only</p>
              <p className="mt-1 text-red-400">
                This pipeline's final stage is best-fit or global initialization.
                Best-fit alignment may inform engineering but CANNOT be the basis
                for a datum-controlled conformance verdict. For datum-controlled
                features, conformance comes from datum/RPS alignment.
              </p>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
