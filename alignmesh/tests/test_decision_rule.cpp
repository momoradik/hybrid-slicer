#include <gtest/gtest.h>
#include "alignmesh/analysis/decision_rule.h"

using namespace alignmesh::analysis;

namespace {

DecisionInputs make_inputs(double dev, double half_tol, double U,
                            ProofBurden burden = ProofBurden::SUPPLIER_PROVES_CONFORMANCE) {
    DecisionInputs d;
    d.deviation = dev;
    d.lsl = -half_tol;
    d.usl = +half_tol;
    d.expanded_uncertainty = U;
    d.proof_burden = burden;
    return d;
}

} // namespace

// ============================================================================
// Core classification
// ============================================================================

TEST(DecisionRule, ClearPass) {
    auto r = apply_decision_rule(make_inputs(0.030, 0.100, 0.020));
    EXPECT_EQ(r.verdict, Verdict::PASS);
    EXPECT_EQ(r.granular_verdict, Verdict::PASS);
}

TEST(DecisionRule, BorderlineWarning) {
    auto r = apply_decision_rule(make_inputs(0.085, 0.100, 0.020));
    EXPECT_EQ(r.verdict, Verdict::WARNING);
}

TEST(DecisionRule, ClearFail) {
    auto r = apply_decision_rule(make_inputs(0.150, 0.100, 0.020));
    EXPECT_EQ(r.verdict, Verdict::FAIL);
}

TEST(DecisionRule, ExactlyOnAcceptanceLimit) {
    auto r = apply_decision_rule(make_inputs(0.080, 0.100, 0.020));
    EXPECT_EQ(r.verdict, Verdict::WARNING);
}

TEST(DecisionRule, NegativeDeviation) {
    auto r = apply_decision_rule(make_inputs(-0.030, 0.100, 0.020));
    EXPECT_EQ(r.verdict, Verdict::PASS);
}

// ============================================================================
// Customer proves non-conformance (lenient)
// ============================================================================

TEST(DecisionRule, LenientMode) {
    auto r = apply_decision_rule(make_inputs(
        0.085, 0.100, 0.020, ProofBurden::CUSTOMER_PROVES_NONCONFORMANCE));
    EXPECT_EQ(r.verdict, Verdict::PASS);
}

// ============================================================================
// INVERTED GUARD-BAND MUTATION TEST
// ============================================================================

TEST(DecisionRule, InvertedGuardBandMutation) {
    auto r_strict = apply_decision_rule(make_inputs(
        0.085, 0.100, 0.020, ProofBurden::SUPPLIER_PROVES_CONFORMANCE));
    auto r_lenient = apply_decision_rule(make_inputs(
        0.085, 0.100, 0.020, ProofBurden::CUSTOMER_PROVES_NONCONFORMANCE));

    EXPECT_NE(r_strict.verdict, Verdict::PASS);
    EXPECT_EQ(r_lenient.verdict, Verdict::PASS);
    EXPECT_NE(r_strict.verdict, r_lenient.verdict);

    EXPECT_NEAR(r_strict.acceptance_lower, -0.080, 1e-10);
    EXPECT_NEAR(r_strict.acceptance_upper, +0.080, 1e-10);
    EXPECT_NEAR(r_lenient.acceptance_lower, -0.120, 1e-10);
    EXPECT_NEAR(r_lenient.acceptance_upper, +0.120, 1e-10);
}

// ============================================================================
// All 9 quality gates → INVALID
// ============================================================================

TEST(DecisionRule, UnconstrainedInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.fully_constrained = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, NotConvergedInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.converged = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, LowOverlapInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.overlap_ratio = 0.2;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, AlignmentBasisInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.alignment_basis_valid = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, InfeasibleInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.feasible = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, SamplingInadequateInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.sampling_adequate = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, MeshInvalidInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.mesh_valid = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, UncertaintyIncompleteInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.uncertainty_complete = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, ProvenanceInvalid) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.deterministic_path = false;
    EXPECT_EQ(apply_decision_rule(d).verdict, Verdict::INVALID);
}

TEST(DecisionRule, MultipleGateViolations) {
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.fully_constrained = false;
    d.gates.converged = false;
    d.gates.alignment_basis_valid = false;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.verdict, Verdict::INVALID);
    EXPECT_GE(r.gate_violations.size(), 3u);
}

// ============================================================================
// STRUCTURAL: WARNING never → PASS, INVALID always distinct
// ============================================================================

TEST(DecisionRule, WarningNeverBecomesPass) {
    // In 3-tier mode, WARNING collapses to FAIL (safe), never PASS.
    auto d = make_inputs(0.085, 0.100, 0.020);
    d.verdict_mode = VerdictMode::THREE_TIER;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.granular_verdict, Verdict::WARNING);  // granular: WARNING
    EXPECT_EQ(r.verdict, Verdict::FAIL);               // displayed: FAIL (not PASS)
}

TEST(DecisionRule, InvalidAlwaysDistinct) {
    // INVALID never becomes FAIL or PASS.
    auto d = make_inputs(0.010, 0.100, 0.020);
    d.gates.feasible = false;
    d.verdict_mode = VerdictMode::THREE_TIER;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.verdict, Verdict::INVALID);
    EXPECT_EQ(r.granular_verdict, Verdict::INVALID);
}

TEST(DecisionRule, GranularAlwaysRecorded) {
    // Even in 3-tier mode, granular_verdict records the full 4-tier.
    auto d = make_inputs(0.085, 0.100, 0.020);
    d.verdict_mode = VerdictMode::THREE_TIER;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.granular_verdict, Verdict::WARNING);
    EXPECT_EQ(r.verdict, Verdict::FAIL);
}

// ============================================================================
// Symmetric non-conformance rule
// ============================================================================

TEST(DecisionRule, SymmetricRuleAmbiguousOutsideTolerance) {
    // With symmetric rule, outside tolerance but within U → WARNING, not FAIL.
    auto d = make_inputs(0.110, 0.100, 0.020);
    d.symmetric_nonconformance_rule = true;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.verdict, Verdict::WARNING);  // 0.110 is between USL and USL+U
}

TEST(DecisionRule, ConservativeRuleFailsOutsideTolerance) {
    // Default conservative: outside tolerance = FAIL immediately.
    auto d = make_inputs(0.110, 0.100, 0.020);
    d.symmetric_nonconformance_rule = false;
    auto r = apply_decision_rule(d);
    EXPECT_EQ(r.verdict, Verdict::FAIL);
}

// ============================================================================
// Audit trail
// ============================================================================

TEST(DecisionRule, AuditTrailComplete) {
    auto r = apply_decision_rule(make_inputs(0.010, 0.100, 0.020));
    EXPECT_FALSE(r.proof_burden_label.empty());
    EXPECT_FALSE(r.nonconformance_rule_label.empty());
    EXPECT_FALSE(r.details.empty());
    // Should contain tolerance, U, acceptance zone, verdict.
    bool has_tolerance = false, has_verdict = false;
    for (auto& d : r.details) {
        if (d.find("Tolerance") != std::string::npos) has_tolerance = true;
        if (d.find("PASS") != std::string::npos || d.find("FAIL") != std::string::npos ||
            d.find("WARNING") != std::string::npos) has_verdict = true;
    }
    EXPECT_TRUE(has_tolerance);
    EXPECT_TRUE(has_verdict);
}
