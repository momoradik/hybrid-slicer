// AUTO-GENERATED from api/contract.json — DO NOT EDIT
// Regenerate: node api/generate-types.js > ui/drawgen/web/src/api/alignmesh-types.generated.ts
//
// Units for physical fields are documented inline from the contract.

/** ISO 14253-1 conformance decision. PASS=proven conformant, WARNING=inside tolerance but within U of limit (ambiguous), FAIL=proven non-conformant, INVALID=measurement inadequacy (gate failure) */
export type Verdict = 'PASS' | 'WARNING' | 'FAIL' | 'INVALID';

/** ISO 14253-1 bilateral three-zone conformance per RPS direction. PASS=within acceptance zone, FAIL=outside rejection zone, UNDECIDED=in guard band (ambiguous due to measurement uncertainty) */
export type RpsConformance = 'PASS' | 'FAIL' | 'UNDECIDED';

/** Status of projecting a nominal RPS point onto the scanned surface. Any non-OK status excludes the point from alignment and propagates to the DOF report. */
export type ProjectionStatus = 'OK' | 'INSUFFICIENT_DATA' | 'AMBIGUOUS_PATCH' | 'GRAZING' | 'MULTIPLE_INTERSECTION' | 'NO_NORMAL';

/** HealthResponse */
export interface HealthResponse {
  status: string;
}

/** VersionResponse */
export interface VersionResponse {
  /** Core version string */
  version: string;
  /** Compiler ID and version */
  compiler: string;
  /** CPU brand string */
  cpu: string;
  /** Floating-point determinism flags */
  fp_flags: string;
}

/** InspectRequest */
export interface InspectRequest {
  /** Filesystem path to reference part */
  reference: string;
  /** Filesystem path to measured part */
  measured: string;
  /** Tolerance (mm) */
  tolerance: number;
}

/** A single directional constraint for an RPS point. The deviation that matters is the component of (measured − nominal) along direction only. */
export interface RpsConstraintDirection {
  /** Unit direction vector in reference frame (eₓ/e_y/e_z for axis-lock, or surface normal) (dimensionless) */
  direction: number[];
  /** Priority weight (>0). Higher = more important in over-determined solve. (dimensionless) */
  weight: number;
  /** Bilateral tolerance lower limit (negative = material side) (mm) */
  tolerance_lower?: number;
  /** Bilateral tolerance upper limit (positive = free side) (mm) */
  tolerance_upper?: number;
}

/** A single RPS constraint point specification. User-defined on the reference part. */
export interface RpsPoint {
  /** Stable auditable identifier for this point */
  id: string;
  /** Position in reference frame (mm) */
  nominal_position: number[];
  /** One or more directional constraints (axis-lock and/or surface-normal) */
  constraint_directions: RpsConstraintDirection[];
  /** How the constraint direction(s) were determined */
  source: string;
}

/** Per-point result from the RPS alignment. */
export interface RpsPointResult {
  /** Matches RpsPoint.id */
  id: string;
  /** Projected position on scanned surface (after pre-alignment) (mm) */
  measured_position: number[];
  projection_status: ProjectionStatus;
  /** Signed deviation along each constraint direction (same order as constraint_directions) (mm) */
  deviation_per_direction: number[];
  /** ISO 14253-1 conformance per direction */
  conformance_per_direction: RpsConformance[];
  /** Local surface fit residual at this point (projection quality indicator) (mm) */
  patch_roughness?: number;
}

/** DOF accounting from constraint Jacobian rank analysis. */
export interface DofReport {
  /** Total scalar directional constraints (sum of locked directions across all points) (count) */
  num_scalar_constraints: number;
  /** Numerical rank of the 6-column constraint Jacobian (max 6) (count) */
  jacobian_rank: number;
  /** True if rank == 6 */
  fully_constrained: boolean;
  /** True if num_constraints > 6 and rank == 6 */
  over_determined: boolean;
  /** Number of redundant constraints (num_constraints − 6 if over-determined, else 0) (count) */
  redundant_count: number;
  /** Names of unconstrained DOFs when rank < 6 (e.g. 'tx', 'ry') */
  free_dof_names?: string[];
}

/** Gauss-Newton convergence report for the RPS solver. */
export interface RpsConvergence {
  /** Number of Gauss-Newton iterations (count) */
  iterations: number;
  /** Final weighted sum of squared directional residuals (mm²) */
  final_cost: number;
  /** True if pose increment fell below convergence threshold */
  converged: boolean;
}

/** Full result of Stage 2 pre-aligned RPS alignment. Transform convention: measured→nominal (point_nominal = T * point_measured). */
export interface RpsAlignmentResult {
  /** 4x4 row-major SE(3) transform (measured→nominal) (mm (translation)) */
  transform: number[];
  /** Per-point projection, deviation, and conformance */
  per_point: RpsPointResult[];
  dof_report: DofReport;
  convergence: RpsConvergence;
}

/** DeviationStats */
export interface DeviationStats {
  /** Number of measured points (count) */
  n_points: number;
  /** Mean unsigned deviation (mm) */
  mean: number;
  /** Root-mean-square deviation (mm) */
  rms: number;
  /** Maximum unsigned deviation (mm) */
  max: number;
  /** Standard deviation of deviations (mm) */
  std_dev: number;
  /** Percent of points within tolerance (%) */
  percent_within_tolerance: number;
}

/** Fingerprint */
export interface Fingerprint {
  /** Compiler ID + version that built the core */
  compiler: string;
  /** CPU brand string of the machine running the core */
  cpu: string;
}

/** InspectResponse */
export interface InspectResponse {
  /** True if the inspection produced a usable result */
  valid: boolean;
  /** Core software version */
  core_version: string;
  /** ISO 8601 UTC timestamp of the inspection */
  timestamp: string;
  /** SHA-256 hex of the reference file (64 chars) */
  reference_hash: string;
  /** SHA-256 hex of the measured file (64 chars) */
  measured_hash: string;
  verdict: Verdict;
  /** Human-readable verdict label */
  verdict_label: string;
  /** Requested tolerance (mm) */
  tolerance_mm: number;
  /** Alignment method used (coarse-to-fine / best-fit / pre-aligned-rps / landmark) */
  alignment_mode: string;
  /** Alignment residual RMS (mm) */
  alignment_rms: number;
  /** Precision tier name (COARSE / MEDIUM / FINE / PRECISE / ULTRA) */
  precision_tier: string;
  /** Heatmap caveat label (must contain 'CORROBORATING') */
  heatmap_label: string;
  stats: DeviationStats;
  fingerprint: Fingerprint;
  /** Number of per-point display data entries (count) */
  n_display_points: number;
  /** Heatmap color scale minimum (mm) */
  heatmap_min: number;
  /** Heatmap color scale maximum (mm) */
  heatmap_max: number;
  /** True if all 6 DOFs are constrained */
  fully_constrained: boolean;
  /** Number of under-constrained DOFs (count) */
  num_under_constrained: number;
  /** Expanded measurement uncertainty U (mm) */
  expanded_uncertainty: number;
  /** Coverage factor k (default 2 for 95%) */
  coverage_factor: number;
  /** Guard-banded acceptance zone lower limit (mm) */
  acceptance_lower: number;
  /** Guard-banded acceptance zone upper limit (mm) */
  acceptance_upper: number;
  /** 4x4 row-major alignment transform (measured→nominal: point_nom = T * point_meas) (mm (translation), dimensionless (rotation)) */
  transform_matrix: number[];
  /** Per-point signed deviation values (mm) */
  point_deviations: number[];
  /** Sum of all point_deviations (integrity check) (mm) */
  deviation_checksum: number;
  /** Index of the point with max absolute deviation (index) */
  max_deviation_index: number;
  /** Deviation value at the max-deviation point (mm) */
  max_deviation_value: number;
  /** Present only when alignment_mode == 'pre-aligned-rps' */
  rps_result?: RpsAlignmentResult;
  /** RPS projected points on the measured surface (reference frame) */
  rps_projected_points?: { x: number; y: number; z: number; valid: boolean }[];
  /** Warning messages */
  warnings: string[];
  /** Error messages */
  errors: string[];
}

