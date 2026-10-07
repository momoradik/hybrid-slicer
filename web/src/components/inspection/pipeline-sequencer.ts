/**
 * Pipeline stage sequencer — orchestration logic.
 *
 * COMPUTE FIREWALL:
 *   The UI sequences stages and sends the pipeline to the core.
 *   The CORE executes each stage, composes transforms, and returns
 *   per-stage results + the composed final. The UI displays exactly
 *   what the core returns. No transform math happens here.
 *
 * SAFETY FENCE (ENFORCED, not just warned):
 *   Best-fit alignment (ICP/GICP/VGICP) may inform engineering but
 *   CANNOT be the basis for a datum-controlled conformance verdict.
 *   For datum-controlled features, conformance comes from datum/RPS
 *   alignment. Best-fit-derived results are labeled as engineering/
 *   non-conformance and cannot be presented as datum conformance.
 *
 *   The composition order is the core's; the UI displays the exact
 *   order applied and never silently reorders.
 */

// ═══════════════════════════════════════════════════════════════════════
// Types
// ═══════════════════════════════════════════════════════════════════════

/** Available alignment stage kinds. */
export type StageKind =
  | 'best-fit'       // ICP/GICP/VGICP — engineering/process use
  | 'global-init'    // FPFH-based coarse pose
  | 'landmark'       // Kabsch from user-selected points
  | 'datum'          // 3-2-1 datum/RPS constrained

/** Whether a stage's result can support a conformance verdict. */
export type VerdictScope = 'conformance' | 'engineering'

/** Per-stage result as returned by the core. */
export interface StageResult {
  /** Stage index (0-based, matches the user's sequence order). */
  index: number
  /** What kind of alignment was performed. */
  kind: StageKind
  /** Human-readable stage name. */
  name: string
  /** 4x4 row-major transform (column-vector: point' = T * point). */
  transform: number[]
  /** Alignment residual RMS for this stage (mm). */
  rms: number
  /** Did this stage converge? */
  converged: boolean
  /** Per-stage warnings from the core. */
  warnings: string[]
}

/** Full pipeline result from the core. */
export interface PipelineResult {
  /** Per-stage results, in execution order (index 0 = first stage applied). */
  stages: StageResult[]
  /** Composed final transform: stages[n-1] x ... x stages[0].
   *  This is the core's composition — the UI never recomputes it. */
  composedTransform: number[]
  /** Composed RMS (from the core). */
  composedRms: number
  /** Is the pipeline fully converged (all stages converged)? */
  allConverged: boolean
  /** Whether the composed result can support a conformance verdict. */
  verdictScope: VerdictScope
  /** Overall warnings (includes stage warnings + composition warnings). */
  warnings: string[]
}

/** A stage definition before execution (what the user picks). */
export interface StageDefinition {
  kind: StageKind
  /** Display label. */
  label: string
}

// ═══════════════════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════════════════

/** Human-readable labels for each stage kind. */
export const STAGE_LABELS: Record<StageKind, string> = {
  'best-fit': 'Best-Fit (ICP/GICP)',
  'global-init': 'Global Initialization (FPFH)',
  'landmark': 'Landmark / Kabsch',
  'datum': 'Datum / RPS (3-2-1)',
}

/** Stage kinds that can support a conformance verdict. */
const CONFORMANCE_STAGES: ReadonlySet<StageKind> = new Set(['datum', 'landmark'])

/** Stage kinds that are engineering/process only — never conformance. */
const ENGINEERING_ONLY_STAGES: ReadonlySet<StageKind> = new Set(['best-fit', 'global-init'])

// ═══════════════════════════════════════════════════════════════════════
// Safety fence: verdict scope classification
// ═══════════════════════════════════════════════════════════════════════

/**
 * Classify whether a pipeline's result can support a conformance verdict.
 *
 * SAFETY RULE: if the FINAL stage (the one whose transform is "on top")
 * is a best-fit or global-init, the result is engineering-only. For a
 * conformance verdict, the final stage must be datum or landmark.
 *
 * A pipeline like [global-init → best-fit → datum] IS conformance,
 * because the last stage is datum. A pipeline like [datum → best-fit]
 * is engineering-only, because best-fit is last and dominates.
 */
export function classifyVerdictScope(stages: StageResult[]): VerdictScope {
  if (stages.length === 0) return 'engineering'

  // The final stage determines the verdict scope.
  const finalStage = stages[stages.length - 1]
  if (ENGINEERING_ONLY_STAGES.has(finalStage.kind)) return 'engineering'
  if (CONFORMANCE_STAGES.has(finalStage.kind)) return 'conformance'

  // Unknown stage kind — fail-safe to engineering.
  return 'engineering'
}

/**
 * Can a best-fit-derived result be shown as a datum conformance verdict?
 * ALWAYS returns false. This is the enforced safety fence.
 */
export function canBestFitBeConformance(): false {
  return false
}

/**
 * Get the safety label for a verdict scope.
 */
export function getVerdictScopeLabel(scope: VerdictScope): string {
  if (scope === 'conformance') {
    return 'Datum-constrained conformance result'
  }
  return 'Engineering / process result — NOT for datum conformance'
}

/**
 * Is this stage kind engineering-only (cannot be conformance basis)?
 */
export function isEngineeringOnly(kind: StageKind): boolean {
  return ENGINEERING_ONLY_STAGES.has(kind)
}

// ═══════════════════════════════════════════════════════════════════════
// Composition verification (display-side check, not computation)
// ═══════════════════════════════════════════════════════════════════════

/**
 * Multiply two 4x4 row-major matrices: C = A * B.
 *
 * NOTE: this is used ONLY for TEST VERIFICATION — to check that the
 * core's composed transform matches the expected product of per-stage
 * transforms. The UI never uses this for alignment computation.
 * The core is the authority; this is a test oracle.
 */
export function multiply4x4(a: number[], b: number[]): number[] {
  const c = new Array(16).fill(0)
  for (let row = 0; row < 4; row++) {
    for (let col = 0; col < 4; col++) {
      let sum = 0
      for (let k = 0; k < 4; k++) {
        sum += a[row * 4 + k] * b[k * 4 + col]
      }
      c[row * 4 + col] = sum
    }
  }
  return c
}

/**
 * Compose a chain of transforms right-to-left: result = stages[n-1] * ... * stages[0].
 *
 * TEST-ONLY utility. The core computes the real composition.
 */
export function composeChainForTest(transforms: number[][]): number[] {
  if (transforms.length === 0) {
    // Identity
    return [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
  }
  let result = transforms[0]
  for (let i = 1; i < transforms.length; i++) {
    result = multiply4x4(transforms[i], result)
  }
  return result
}

// ═══════════════════════════════════════════════════════════════════════
// Pipeline payload builder
// ═══════════════════════════════════════════════════════════════════════

/**
 * Build the pipeline request payload to send to the core.
 * The sequence order is the user's chosen order — the core executes
 * stages in this exact order and must not reorder.
 */
export function buildPipelinePayload(stages: StageDefinition[]) {
  return {
    pipeline: stages.map((s, i) => ({
      index: i,
      kind: s.kind,
      label: s.label,
    })),
  }
}

// ═══════════════════════════════════════════════════════════════════════
// Available stage definitions
// ═══════════════════════════════════════════════════════════════════════

export const AVAILABLE_STAGES: StageDefinition[] = [
  { kind: 'global-init', label: STAGE_LABELS['global-init'] },
  { kind: 'best-fit', label: STAGE_LABELS['best-fit'] },
  { kind: 'landmark', label: STAGE_LABELS['landmark'] },
  { kind: 'datum', label: STAGE_LABELS['datum'] },
]
