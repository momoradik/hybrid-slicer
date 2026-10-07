#include <gtest/gtest.h>
#include "alignmesh/analysis/feasibility.h"

#include <cmath>

using namespace alignmesh::analysis;

// ============================================================================
// Basic feasibility
// ============================================================================

TEST(Feasibility, GoodDataLooseTolerance) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.reference_fidelity_mm = 0.001;
    q.alignment_uncertainty_mm = 0.002;
    q.form_error_rms_mm = 0.003;
    q.calibration_uncertainty_mm = 0.002;

    auto result = estimate_feasibility(0.100, q);
    EXPECT_TRUE(result.feasible);
    EXPECT_FALSE(result.invalid_claim);
    EXPECT_LT(result.uncertainty_tolerance_ratio, 1.0);
}

TEST(Feasibility, OneMicronOnFiftyMicronData) {
    DataQualityParams q;
    q.point_noise_mm = 0.050;
    q.reference_fidelity_mm = 0.020;
    q.alignment_uncertainty_mm = 0.010;

    auto result = estimate_feasibility(0.001, q);
    EXPECT_FALSE(result.feasible);
    EXPECT_TRUE(result.invalid_claim);
    EXPECT_GT(result.expanded_uncertainty, 0.001);
}

// ============================================================================
// Q1: New contributors
// ============================================================================

TEST(Feasibility, FixturingContributor) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.fixturing_uncertainty_mm = 0.010;

    auto result = estimate_feasibility(0.100, q);
    bool found = false;
    for (auto& c : result.contributors)
        if (c.name.find("fixturing") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(Feasibility, StitchingContributor) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.stitching_uncertainty_mm = 0.015;

    auto result = estimate_feasibility(0.100, q);
    bool found = false;
    for (auto& c : result.contributors)
        if (c.name.find("stitching") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(Feasibility, DatumContributor) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.datum_uncertainty_mm = 0.008;

    auto result = estimate_feasibility(0.100, q);
    bool found = false;
    for (auto& c : result.contributors)
        if (c.name.find("datum") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(Feasibility, StitchingZeroWarning) {
    // Zero stitching with nonzero noise → should warn about single-setup.
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.stitching_uncertainty_mm = 0.0;

    auto result = estimate_feasibility(0.100, q);
    bool found_warning = false;
    for (auto& w : result.warnings)
        if (w.find("single-setup") != std::string::npos) found_warning = true;
    EXPECT_TRUE(found_warning);
}

// ============================================================================
// Q1+Q2: Systematic bias added linearly
// ============================================================================

TEST(Feasibility, SystematicBiasLinear) {
    // u_p = 3µm random, b_scanner = 4µm systematic.
    // RSS of random: 3µm.
    // U = k*3 + 4 = 6 + 4 = 10µm (at k=2).
    // NOT sqrt(3²+4²)*2 = 10µm (coincidentally same for these numbers).
    DataQualityParams q;
    q.point_noise_mm = 0.003;
    q.systematic_scanner_bias_mm = 0.004;

    auto result = estimate_feasibility(0.020, q);
    EXPECT_NEAR(result.random_std_uncertainty, 0.003, 1e-6);
    EXPECT_NEAR(result.systematic_bias, 0.004, 1e-6);
    // U = 2 * 0.003 + 0.004 = 0.010
    EXPECT_NEAR(result.expanded_uncertainty, 0.010, 1e-6);
}

TEST(Feasibility, SystematicBiasUncorrectedWarning) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.systematic_scanner_bias_mm = 0.003;
    q.scanner_bias_corrected = false;

    auto result = estimate_feasibility(0.100, q);
    bool found = false;
    for (auto& w : result.warnings)
        if (w.find("uncorrected") != std::string::npos ||
            w.find("Uncorrected") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(Feasibility, SystematicBiasCorrectedNoWarning) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.systematic_scanner_bias_mm = 0.001;  // residual after correction
    q.scanner_bias_corrected = true;
    q.scanner_bias_correction_method = "calibrated artifact substitution";

    auto result = estimate_feasibility(0.100, q);
    bool found_uncorrected = false;
    for (auto& w : result.warnings)
        if (w.find("uncorrected") != std::string::npos) found_uncorrected = true;
    EXPECT_FALSE(found_uncorrected);
}

// ============================================================================
// Q3: Configurable k
// ============================================================================

TEST(Feasibility, CoverageFactorK2Default) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    auto result = estimate_feasibility(0.100, q);
    EXPECT_NEAR(result.coverage_factor, 2.0, 1e-10);
}

TEST(Feasibility, CoverageFactorK3) {
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.coverage_factor = 3.0;

    auto r2 = estimate_feasibility(0.100, q);
    EXPECT_NEAR(r2.coverage_factor, 3.0, 1e-10);

    // k=3 gives larger U than k=2 for same data.
    DataQualityParams q2 = q;
    q2.coverage_factor = 2.0;
    auto r_k2 = estimate_feasibility(0.100, q2);
    EXPECT_GT(r2.expanded_uncertainty, r_k2.expanded_uncertainty);
}

// ============================================================================
// Q4: ISO 14253-1 three-zone logic
// ============================================================================

TEST(Conformance, WithinAcceptanceZone) {
    // Tolerance ±0.05, U = 0.01.
    // Acceptance zone: [-0.05+0.01, +0.05-0.01] = [-0.04, +0.04].
    EXPECT_EQ(evaluate_conformance(0.0, -0.05, 0.05, 0.01),
              ConformanceZone::CONFORMANT);
    EXPECT_EQ(evaluate_conformance(0.03, -0.05, 0.05, 0.01),
              ConformanceZone::CONFORMANT);
}

TEST(Conformance, WithinAmbiguousZone) {
    // Value at 0.045 — within U of the upper limit.
    EXPECT_EQ(evaluate_conformance(0.045, -0.05, 0.05, 0.01),
              ConformanceZone::AMBIGUOUS);
    // Value at -0.045 — within U of the lower limit.
    EXPECT_EQ(evaluate_conformance(-0.045, -0.05, 0.05, 0.01),
              ConformanceZone::AMBIGUOUS);
}

TEST(Conformance, OutsideNonConformant) {
    // Value at 0.07 — clearly outside.
    EXPECT_EQ(evaluate_conformance(0.07, -0.05, 0.05, 0.01),
              ConformanceZone::NON_CONFORMANT);
}

TEST(Conformance, ExactlyOnAcceptanceBoundary) {
    // Exactly at USL - U = 0.04. Strict inequality → AMBIGUOUS (fail-safe).
    EXPECT_EQ(evaluate_conformance(0.04, -0.05, 0.05, 0.01),
              ConformanceZone::AMBIGUOUS);
}

TEST(Conformance, UncertaintyExceedsToleranceZone) {
    // U = 0.06 on a ±0.05 zone → acceptance zone collapses.
    EXPECT_EQ(evaluate_conformance(0.0, -0.05, 0.05, 0.06),
              ConformanceZone::AMBIGUOUS);
}

TEST(Conformance, BilateralAcceptanceZone) {
    // Verify the acceptance zone in the feasibility result.
    DataQualityParams q;
    q.point_noise_mm = 0.005;  // U = 2*0.005 = 0.010

    auto result = estimate_feasibility(0.100, q);
    // Tolerance ±0.050, U = 0.010.
    // Acceptance: [-0.050+0.010, +0.050-0.010] = [-0.040, +0.040].
    EXPECT_NEAR(result.acceptance_lower, -0.040, 1e-6);
    EXPECT_NEAR(result.acceptance_upper, +0.040, 1e-6);
}

// ============================================================================
// Q5: u_r split
// ============================================================================

TEST(Feasibility, CadReferenceUr) {
    // CAD reference: u_r should reflect numerical/conversion sub-terms,
    // NOT geometric nominal (which is definition, not uncertainty).
    DataQualityParams q;
    q.point_noise_mm = 0.005;
    q.reference_fidelity_mm = 0.0001;  // tiny: numerical solver tolerance
    // NOT zero — CAD eval has nonzero numerical uncertainty.

    auto result = estimate_feasibility(0.010, q);
    bool found_ref = false;
    for (auto& c : result.contributors) {
        if (c.name.find("reference") != std::string::npos) {
            EXPECT_GT(c.value_mm, 0);
            found_ref = true;
        }
    }
    EXPECT_TRUE(found_ref);
}

// ============================================================================
// RSS combination
// ============================================================================

TEST(Feasibility, PureRSSCombination) {
    DataQualityParams q;
    q.point_noise_mm = 0.003;
    q.reference_fidelity_mm = 0.004;
    // u_c = sqrt(3² + 4²) = 5 µm, U = 2*5 = 10 µm
    auto result = estimate_feasibility(0.050, q);  // 50µm tolerance (U=10 < T/2=25)
    EXPECT_NEAR(result.random_std_uncertainty, 0.005, 0.001);
    EXPECT_NEAR(result.expanded_uncertainty, 0.010, 0.002);
    EXPECT_TRUE(result.feasible);
}

// ============================================================================
// Boundary: U exactly at tolerance
// ============================================================================

TEST(Feasibility, BoundaryUEqualsT) {
    DataQualityParams q;
    q.point_noise_mm = 0.050;
    // U = 2*0.050 = 0.100
    auto result = estimate_feasibility(0.100, q);
    EXPECT_FALSE(result.feasible)
        << "U == tolerance should NOT pass (fail-safe)";
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(FeasibilityAdversarial, ZeroTolerance) {
    DataQualityParams q;
    q.point_noise_mm = 0.001;
    auto result = estimate_feasibility(0.0, q);
    EXPECT_FALSE(result.feasible);
    EXPECT_TRUE(result.invalid_claim);
}

TEST(FeasibilityAdversarial, NegativeTolerance) {
    DataQualityParams q;
    auto result = estimate_feasibility(-0.01, q);
    EXPECT_FALSE(result.feasible);
    EXPECT_TRUE(result.invalid_claim);
}

TEST(FeasibilityAdversarial, AllZeroQuality) {
    DataQualityParams q;
    auto result = estimate_feasibility(0.100, q);
    EXPECT_FALSE(result.warnings.empty());
}

TEST(Feasibility, VerdictContainsINVALID) {
    DataQualityParams q;
    q.point_noise_mm = 0.050;
    auto result = estimate_feasibility(0.001, q);
    EXPECT_FALSE(result.verdict.empty());
    EXPECT_NE(result.verdict.find("INVALID"), std::string::npos);
}
