#pragma once

#include <string>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// ISO 14253-1 Conformance Decision Rule + Result Classifier
// ============================================================================
//
// MOST SAFETY-CRITICAL INTERPRETATION IN THE SYSTEM.
//
// Guard-band direction (ISO 14253-1 §4.2–4.3):
//   SUPPLIER_PROVES_CONFORMANCE (default):
//     Acceptance = [LSL + U, USL - U] — zone SHRINKS.
//   CUSTOMER_PROVES_NONCONFORMANCE:
//     Acceptance = [LSL - U, USL + U] — zone EXPANDS.
//
// DELIBERATE CONSERVATIVE DEVIATION from symmetric ISO 14253-1:
//   This implementation classifies results OUTSIDE TOLERANCE as FAIL
//   immediately, without requiring the result to exceed USL+U to
//   "prove non-conformance." This over-rejects (false-reject is the
//   safe direction). The symmetric form (where [USL, USL+U] is
//   ambiguous, not FAIL) is available via symmetric_nonconformance_rule.
//   ALWAYS RECORD which rule was applied on every report.
//
// Classifier: PASS / WARNING / FAIL / INVALID.
// WARNING never auto-promotes to PASS (structurally impossible).
// INVALID always distinct from FAIL (measurement inadequacy ≠ part defect).
// ============================================================================

enum class ProofBurden {
    SUPPLIER_PROVES_CONFORMANCE,
    CUSTOMER_PROVES_NONCONFORMANCE,
};

enum class Verdict {
    PASS,
    WARNING,  // inside tolerance but not proven conformant (ambiguous)
    FAIL,
    INVALID,  // measurement inadequacy — distinct from FAIL
};

/// Display mode for the verdict.
enum class VerdictMode {
    FOUR_TIER,  // PASS / WARNING / FAIL / INVALID (default, most informative)
    THREE_TIER, // PASS / FAIL / INVALID (WARNING collapses to FAIL — safe direction)
};

/// All quality gates. Each must be affirmatively passed for PASS.
struct QualityGates {
    // ---- Registration quality ----
    bool fully_constrained = true;     // Session 8: all 6 DOFs constrained
    bool converged = true;             // Session 5: registration converged
    double overlap_ratio = 1.0;        // Session 5: scan overlap
    double min_overlap = 0.3;

    // ---- Alignment-basis validity (most critical addition) ----
    bool alignment_basis_valid = true; // datum-fit quality (Session 16/17) or
                                       // landmark stability (Session 3)

    // ---- Feasibility ----
    bool feasible = true;              // Session 12: U < tolerance

    // ---- Sampling adequacy ----
    bool sampling_adequate = true;     // Session 10: density resolves feature

    // ---- Data / mesh validation ----
    bool mesh_valid = true;            // Session 2: no holes/degenerate/non-manifold
                                       // in the inspected region

    // ---- Uncertainty budget completeness ----
    bool uncertainty_complete = true;  // no defaulted/missing contributors

    // ---- Result provenance ----
    bool deterministic_path = true;    // official result from deterministic path
};

/// All inputs to the decision rule.
struct DecisionInputs {
    double deviation = 0;        // measured deviation (mm)
    double lsl = 0;              // lower specification limit
    double usl = 0;              // upper specification limit
    double expanded_uncertainty = 0;  // U (mm)
    double coverage_factor = 2.0;

    ProofBurden proof_burden = ProofBurden::SUPPLIER_PROVES_CONFORMANCE;
    VerdictMode verdict_mode = VerdictMode::FOUR_TIER;

    /// Use the symmetric ISO 14253-1 non-conformance rule?
    /// If true: [USL, USL+U] is AMBIGUOUS (not FAIL).
    /// If false (default): outside tolerance = FAIL (conservative).
    bool symmetric_nonconformance_rule = false;

    QualityGates gates;
};

/// Full decision result.
struct DecisionResult {
    /// The displayed/decision verdict (may be collapsed by VerdictMode).
    Verdict verdict = Verdict::INVALID;

    /// The full granular verdict (ALWAYS 4-tier, recorded for audit).
    Verdict granular_verdict = Verdict::INVALID;

    /// Acceptance zone boundaries.
    double acceptance_lower = 0;
    double acceptance_upper = 0;

    double deviation = 0;
    bool inside_tolerance = false;
    bool inside_acceptance = false;
    bool in_ambiguous_zone = false;

    /// Per-gate status.
    bool observability_passed = true;
    bool convergence_passed = true;
    bool overlap_passed = true;
    bool alignment_basis_passed = true;
    bool feasibility_passed = true;
    bool sampling_passed = true;
    bool mesh_valid_passed = true;
    bool uncertainty_complete_passed = true;
    bool provenance_passed = true;

    std::string proof_burden_label;
    std::string nonconformance_rule_label;
    std::vector<std::string> details;
    std::vector<std::string> gate_violations;
};

/// Apply the ISO 14253-1 decision rule.
DecisionResult apply_decision_rule(const DecisionInputs& inputs);

/// Convenience: bilateral tolerance ±half_tol.
DecisionResult apply_decision_rule_bilateral(
    double deviation, double half_tolerance, double expanded_uncertainty,
    ProofBurden burden = ProofBurden::SUPPLIER_PROVES_CONFORMANCE,
    bool fully_constrained = true, bool converged = true,
    double overlap_ratio = 1.0, bool feasible = true);

} // namespace alignmesh::analysis
