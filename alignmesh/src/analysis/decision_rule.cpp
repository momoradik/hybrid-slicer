#include "alignmesh/analysis/decision_rule.h"

#include <cmath>
#include <sstream>

namespace alignmesh::analysis {

DecisionResult apply_decision_rule(const DecisionInputs& in) {
    DecisionResult r;
    r.deviation = in.deviation;
    double U = in.expanded_uncertainty;
    double dev = in.deviation;

    // ---- Proof burden label -------------------------------------------------
    if (in.proof_burden == ProofBurden::SUPPLIER_PROVES_CONFORMANCE) {
        r.proof_burden_label = "supplier proves conformance (strict, default)";
    } else {
        r.proof_burden_label = "customer proves non-conformance (lenient)";
    }
    r.nonconformance_rule_label = in.symmetric_nonconformance_rule ?
        "symmetric ISO 14253-1 (outside-tolerance-within-U = ambiguous)" :
        "conservative (outside-tolerance = FAIL immediately)";

    // ---- Guard-banded acceptance zone ---------------------------------------
    if (in.proof_burden == ProofBurden::SUPPLIER_PROVES_CONFORMANCE) {
        r.acceptance_lower = in.lsl + U;
        r.acceptance_upper = in.usl - U;
    } else {
        r.acceptance_lower = in.lsl - U;
        r.acceptance_upper = in.usl + U;
    }

    // ---- Position classification --------------------------------------------
    r.inside_tolerance = (dev > in.lsl && dev < in.usl);
    r.inside_acceptance = (r.acceptance_lower < r.acceptance_upper) &&
                          (dev > r.acceptance_lower && dev < r.acceptance_upper);
    r.in_ambiguous_zone = r.inside_tolerance && !r.inside_acceptance;

    // ---- Quality gates (ALL must be affirmatively true for PASS) -------------

    auto check_gate = [&](bool condition, const char* name,
                          bool& flag, const char* violation_msg) {
        flag = condition;
        if (!condition) {
            r.gate_violations.push_back(std::string(name) + ": " + violation_msg);
        }
    };

    check_gate(in.gates.fully_constrained, "OBSERVABILITY",
               r.observability_passed,
               "not all 6 DOFs constrained");
    check_gate(in.gates.converged, "CONVERGENCE",
               r.convergence_passed,
               "registration did not converge");
    check_gate(in.gates.overlap_ratio >= in.gates.min_overlap, "OVERLAP",
               r.overlap_passed,
               ("insufficient overlap (" + std::to_string(in.gates.overlap_ratio * 100) +
                "%)").c_str());
    check_gate(in.gates.alignment_basis_valid, "ALIGNMENT-BASIS",
               r.alignment_basis_passed,
               "datum fit or landmark stability not validated");
    check_gate(in.gates.feasible, "FEASIBILITY",
               r.feasibility_passed,
               "U >= tolerance — measurement cannot support claim");
    check_gate(in.gates.sampling_adequate, "SAMPLING",
               r.sampling_passed,
               "point density insufficient for toleranced feature");
    check_gate(in.gates.mesh_valid, "DATA-VALIDATION",
               r.mesh_valid_passed,
               "mesh has holes/degenerate/non-manifold in inspected region");
    check_gate(in.gates.uncertainty_complete, "UNCERTAINTY-BUDGET",
               r.uncertainty_complete_passed,
               "uncertainty budget has defaulted or missing contributors");
    check_gate(in.gates.deterministic_path, "PROVENANCE",
               r.provenance_passed,
               "result not from the deterministic official path");

    bool all_gates =
        r.observability_passed && r.convergence_passed && r.overlap_passed &&
        r.alignment_basis_passed && r.feasibility_passed && r.sampling_passed &&
        r.mesh_valid_passed && r.uncertainty_complete_passed && r.provenance_passed;

    // ---- Granular verdict (ALWAYS 4-tier) -----------------------------------
    if (!all_gates) {
        r.granular_verdict = Verdict::INVALID;
    } else if (r.inside_acceptance) {
        r.granular_verdict = Verdict::PASS;
    } else if (r.in_ambiguous_zone) {
        r.granular_verdict = Verdict::WARNING;
    } else if (!r.inside_tolerance && in.symmetric_nonconformance_rule) {
        // Symmetric rule: outside tolerance but within U of the limit
        // is ambiguous (non-conformance not proven).
        bool within_U_of_lower = (dev >= in.lsl - U && dev <= in.lsl);
        bool within_U_of_upper = (dev >= in.usl && dev <= in.usl + U);
        if (within_U_of_lower || within_U_of_upper) {
            r.granular_verdict = Verdict::WARNING;
        } else {
            r.granular_verdict = Verdict::FAIL;
        }
    } else {
        r.granular_verdict = Verdict::FAIL;
    }

    // ---- Displayed verdict (may collapse based on VerdictMode) ---------------
    r.verdict = r.granular_verdict;
    if (in.verdict_mode == VerdictMode::THREE_TIER &&
        r.granular_verdict == Verdict::WARNING) {
        r.verdict = Verdict::FAIL;  // WARNING → FAIL (safe direction)
    }
    // STRUCTURAL: WARNING can NEVER become PASS. No code path exists.
    // STRUCTURAL: INVALID is ALWAYS distinct from FAIL. No code path merges.

    // ---- Audit trail --------------------------------------------------------
    r.details.push_back("Tolerance: [" + std::to_string(in.lsl) + ", " +
                         std::to_string(in.usl) + "] mm");
    r.details.push_back("U = " + std::to_string(U) + " mm (k=" +
                         std::to_string(in.coverage_factor) + ")");
    r.details.push_back("Proof burden: " + r.proof_burden_label);
    r.details.push_back("Non-conformance rule: " + r.nonconformance_rule_label);
    r.details.push_back("Acceptance zone: [" + std::to_string(r.acceptance_lower) +
                         ", " + std::to_string(r.acceptance_upper) + "] mm");

    auto verdict_str = [](Verdict v) -> const char* {
        switch (v) {
        case Verdict::PASS:    return "PASS";
        case Verdict::WARNING: return "WARNING";
        case Verdict::FAIL:    return "FAIL";
        case Verdict::INVALID: return "INVALID";
        }
        return "UNKNOWN";
    };

    r.details.push_back("Deviation = " + std::to_string(dev) +
                         " mm -> granular=" + verdict_str(r.granular_verdict) +
                         ", displayed=" + verdict_str(r.verdict));

    for (auto& g : r.gate_violations)
        r.details.push_back("GATE VIOLATION: " + g);

    return r;
}

DecisionResult apply_decision_rule_bilateral(
        double deviation, double half_tolerance, double expanded_uncertainty,
        ProofBurden burden, bool fully_constrained, bool converged,
        double overlap_ratio, bool feasible) {
    DecisionInputs d;
    d.deviation = deviation;
    d.lsl = -half_tolerance;
    d.usl = +half_tolerance;
    d.expanded_uncertainty = expanded_uncertainty;
    d.proof_burden = burden;
    d.gates.fully_constrained = fully_constrained;
    d.gates.converged = converged;
    d.gates.overlap_ratio = overlap_ratio;
    d.gates.feasible = feasible;
    return apply_decision_rule(d);
}

} // namespace alignmesh::analysis
