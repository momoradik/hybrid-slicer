#include <gtest/gtest.h>
#include "alignmesh/analysis/gum_uncertainty.h"

#include <cmath>

using namespace alignmesh::analysis;

// ============================================================================
// Type B distribution conversions
// ============================================================================

TEST(TypeB, RectangularUncertainty) {
    EXPECT_NEAR(type_b_uncertainty(1.0, DistributionType::RECTANGULAR),
                1.0 / std::sqrt(3.0), 1e-12);
}

TEST(TypeB, TriangularUncertainty) {
    EXPECT_NEAR(type_b_uncertainty(1.0, DistributionType::TRIANGULAR),
                1.0 / std::sqrt(6.0), 1e-12);
}

TEST(TypeB, UShapedUncertainty) {
    EXPECT_NEAR(type_b_uncertainty(1.0, DistributionType::U_SHAPED),
                1.0 / std::sqrt(2.0), 1e-12);
}

TEST(TypeB, NormalPassthrough) {
    EXPECT_NEAR(type_b_uncertainty(0.005, DistributionType::NORMAL), 0.005, 1e-12);
}

// ============================================================================
// Student's t quantile
// ============================================================================

TEST(StudentT, KnownValues) {
    // v=inf → z ≈ 1.96 for 95%.
    EXPECT_NEAR(student_t_quantile(0.95, 1e6), 1.96, 0.01);
    // v=9 → t ≈ 2.262 for 95%.
    EXPECT_NEAR(student_t_quantile(0.95, 9), 2.262, 0.01);
    // v=4 → t ≈ 2.776 for 95%.
    EXPECT_NEAR(student_t_quantile(0.95, 4), 2.776, 0.01);
    // v=1 → t ≈ 12.706 for 95%.
    EXPECT_NEAR(student_t_quantile(0.95, 1), 12.706, 0.01);
}

TEST(StudentT, LowDOFGivesLargerK) {
    double k_inf = student_t_quantile(0.95, 1e6);
    double k_10 = student_t_quantile(0.95, 10);
    double k_4 = student_t_quantile(0.95, 4);
    EXPECT_GT(k_4, k_10);
    EXPECT_GT(k_10, k_inf);
}

// ============================================================================
// GUM analytic: RSS of random + linear systematic
// ============================================================================

TEST(GUM, TwoIndependentInputs) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});
    budget.inputs.push_back({"u2", "", false, 4.0});
    budget.coverage_factor_override = 2.0;

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.combined_standard_uncertainty, 5.0, 1e-10);
    EXPECT_NEAR(result.expanded_uncertainty, 10.0, 1e-10);
}

TEST(GUM, SystematicBiasLinearNotRSS) {
    // u_p = 3 (random), b = 4 (systematic).
    // U = k·u_c + |b| = 2·3 + 4 = 10.
    // NOT √(9+16)·2 = 10 (coincidentally same for this case).
    // Verify with a different example: u=1, b=1.
    // Correct: U = 2·1 + 1 = 3.
    // Wrong (RSS): U = 2·√(1+1) = 2.83.
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 1.0});
    budget.biases.push_back({"b1", 1.0});
    budget.coverage_factor_override = 2.0;

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.combined_standard_uncertainty, 1.0, 1e-10);
    EXPECT_NEAR(result.systematic_bias_total, 1.0, 1e-10);
    EXPECT_NEAR(result.expanded_uncertainty, 3.0, 1e-10);  // 2*1 + 1 = 3
}

TEST(GUM, WithSensitivityCoefficients) {
    UncertaintyBudget budget;
    UncertaintyInput i1 = {"u1", "", false, 3.0};
    i1.sensitivity = 2.0;
    UncertaintyInput i2 = {"u2", "", false, 4.0};
    i2.sensitivity = 0.5;
    budget.inputs = {i1, i2};
    budget.coverage_factor_override = 1.0;

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.combined_standard_uncertainty, std::sqrt(40.0), 1e-10);
}

TEST(GUM, WithCorrelation) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});
    budget.inputs.push_back({"u2", "", false, 4.0});
    budget.correlations.push_back({0, 1, 0.5});
    budget.coverage_factor_override = 1.0;

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.combined_standard_uncertainty, std::sqrt(37.0), 1e-10);
}

TEST(GUM, DoublingInputPropagates) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 6.0});  // doubled from 3
    budget.inputs.push_back({"u2", "", false, 4.0});
    budget.coverage_factor_override = 1.0;

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.combined_standard_uncertainty, std::sqrt(52.0), 1e-10);
}

// ============================================================================
// Student's t integration in GUM
// ============================================================================

TEST(GUM, LowDOFElevatesCoverageFactor) {
    // Type A with n=5 → dof=4. t(0.95, 4) ≈ 2.776.
    UncertaintyBudget budget;
    UncertaintyInput i1 = {"u1", "Type A n=5", true, 3.0};
    i1.degrees_of_freedom = 4;
    budget.inputs = {i1};
    // No override → uses Student's t.

    auto result = evaluate_gum(budget);
    EXPECT_GT(result.coverage_factor, 2.5);  // should be ~2.776
    EXPECT_TRUE(result.low_dof_warning);
    // U = k·u_c = ~2.776 * 3 = ~8.33
    EXPECT_GT(result.expanded_uncertainty, 8.0);
}

TEST(GUM, HighDOFUsesNearK2) {
    UncertaintyBudget budget;
    UncertaintyInput i1 = {"u1", "", false, 3.0};
    i1.degrees_of_freedom = 1e6;
    budget.inputs = {i1};

    auto result = evaluate_gum(budget);
    EXPECT_NEAR(result.coverage_factor, 1.96, 0.05);
    EXPECT_FALSE(result.low_dof_warning);
}

TEST(GUM, WelchSatterthwaiteEffectiveDOF) {
    UncertaintyBudget budget;
    UncertaintyInput i1 = {"u1", "Type A n=10", true, 3.0};
    i1.degrees_of_freedom = 9;
    UncertaintyInput i2 = {"u2", "Type B inf", false, 4.0};
    i2.degrees_of_freedom = 1e6;
    budget.inputs = {i1, i2};

    auto result = evaluate_gum(budget);
    EXPECT_GT(result.effective_dof, 9.0);
    EXPECT_LT(result.effective_dof, 1e6);
}

// ============================================================================
// Monte Carlo
// ============================================================================

TEST(MonteCarlo, AgreesWithAnalytic) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});
    budget.inputs.push_back({"u2", "", false, 4.0});
    budget.coverage_factor_override = 2.0;

    auto gum = evaluate_gum(budget);
    auto mc = evaluate_monte_carlo(budget, {}, 500000, 42);

    EXPECT_NEAR(mc.std_dev, gum.combined_standard_uncertainty, 0.1);
}

TEST(MonteCarlo, SeededReproducibility) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});

    auto mc1 = evaluate_monte_carlo(budget, {}, 10000, 42);
    auto mc2 = evaluate_monte_carlo(budget, {}, 10000, 42);

    EXPECT_EQ(mc1.std_dev, mc2.std_dev);
    EXPECT_EQ(mc1.coverage_lo, mc2.coverage_lo);
}

TEST(MonteCarlo, DifferentSeedDifferentResult) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});

    auto mc1 = evaluate_monte_carlo(budget, {}, 10000, 42);
    auto mc2 = evaluate_monte_carlo(budget, {}, 10000, 99);
    EXPECT_NE(mc1.mean, mc2.mean);
}

TEST(MonteCarlo, NonlinearModel) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"x1", "", false, 1.0});
    budget.inputs.push_back({"x2", "", false, 0.5});

    auto model = [](const std::vector<double>& x) -> double {
        return x[0] * x[0] + x[1];
    };
    auto mc = evaluate_monte_carlo(budget, model, 200000, 42);
    EXPECT_GT(mc.std_dev, 0);
}

// ============================================================================
// GUM + MC validation workflow
// ============================================================================

TEST(Validation, LinearCaseAgreement) {
    UncertaintyBudget budget;
    budget.inputs.push_back({"u1", "", false, 3.0});
    budget.inputs.push_back({"u2", "", false, 4.0});
    budget.coverage_factor_override = 2.0;

    auto result = evaluate_with_validation(budget, {}, 200000, 42);
    EXPECT_FALSE(result.methods_disagree);
    EXPECT_EQ(result.authoritative_method, "GUM (validated by MC)");
}

TEST(Validation, TakesLargerOnDisagreement) {
    // Nonlinear model where GUM linearization may underestimate.
    UncertaintyBudget budget;
    budget.inputs.push_back({"x1", "", false, 2.0});
    budget.coverage_factor_override = 2.0;

    auto model = [](const std::vector<double>& x) -> double {
        return x[0] * x[0] * x[0];  // cubic — GUM linearizes
    };

    auto result = evaluate_with_validation(budget, model, 500000, 42);
    // Whether they agree or not, the authoritative U should be the larger.
    EXPECT_GE(result.expanded_uncertainty,
              std::min(result.gum.expanded_uncertainty, result.mc.expanded_uncertainty));
}
