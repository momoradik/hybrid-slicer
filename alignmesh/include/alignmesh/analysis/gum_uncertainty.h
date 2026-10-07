#pragma once

#include "alignmesh/numerics/seeded_rng.h"

#include <Eigen/Core>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// GUM Uncertainty Engine (JCGM 100:2008 + JCGM 101:2008)
// ============================================================================
//
// VIM (JCGM 200) terminology:
//   measurement uncertainty — dispersion parameter, NOT error
//   measurement error — measured − true (unknowable)
//   trueness — closeness of mean to true value
//   precision — closeness of agreement between measurements
//   accuracy — combines trueness + precision (NOT a synonym for uncertainty)
//
// Combination (JCGM 100 §5.1, ISO 15530-3):
//   U = k(ν_eff) · √(Σ (cᵢ·uᵢ)² + 2·Σ cᵢcⱼrᵢⱼuᵢuⱼ)  +  |b|
//       ──────────── random terms (RSS) ────────────────   systematic
//
//   - k = t_p(ν_eff) from Student's t for finite DOF (GUM §G.6.4)
//   - |b| = uncorrected systematic bias, added LINEARLY
//
// GUM analytic is PRIMARY. Monte Carlo (JCGM 101) is the validation check
// and authoritative fallback when LPU conditions fail.
// ============================================================================

enum class DistributionType {
    NORMAL,       // u = stated_value (already 1-sigma)
    RECTANGULAR,  // u = half_width / √3
    TRIANGULAR,   // u = half_width / √6
    U_SHAPED,     // u = half_width / √2
};

/// A single uncertainty input (random contributor).
struct UncertaintyInput {
    std::string name;
    std::string description;

    bool is_type_a = false;  // Type A (statistical) vs Type B (other means)

    double standard_uncertainty = 0;  // u(xi) in mm
    DistributionType distribution = DistributionType::NORMAL;
    double half_width = 0;

    double sensitivity = 1.0;  // ∂f/∂xi
    double degrees_of_freedom = 1e6;  // Type A: n-1; Type B: ∞
};

/// Systematic bias (added linearly, NOT in quadrature).
struct SystematicBias {
    std::string name;
    double value_mm = 0;       // |b| magnitude
    bool corrected = false;    // if true, value_mm is the correction residual
    std::string correction_method;
};

/// Correlation between two contributors.
struct Correlation {
    int index_i = 0;
    int index_j = 0;
    double correlation_coefficient = 0;
};

/// The full uncertainty budget.
struct UncertaintyBudget {
    std::vector<UncertaintyInput> inputs;      // random contributors (RSS)
    std::vector<SystematicBias> biases;        // systematic (linear add)
    std::vector<Correlation> correlations;

    /// Requested coverage probability (default 0.95).
    double coverage_probability = 0.95;

    /// Override coverage factor (0 = use Student's t from ν_eff).
    double coverage_factor_override = 0;
};

/// GUM analytic result.
struct GUMResult {
    double combined_standard_uncertainty = 0;  // u_c (random RSS)
    double systematic_bias_total = 0;          // |b| (linear)
    double expanded_uncertainty = 0;           // U = k·u_c + |b|

    double coverage_factor = 0;                // k (may be t_p(ν_eff))
    double effective_dof = 0;                  // ν_eff (Welch-Satterthwaite)
    double coverage_probability = 0.95;

    bool low_dof_warning = false;              // ν_eff < 10

    std::vector<double> contribution_magnitudes;  // |cᵢ·uᵢ|
    std::vector<std::string> details;
};

/// Monte Carlo result (JCGM 101).
struct MonteCarloResult {
    double mean = 0;
    double std_dev = 0;

    double coverage_lo = 0;   // shortest coverage interval lower bound
    double coverage_hi = 0;   // shortest coverage interval upper bound
    double coverage_probability = 0.95;
    double expanded_uncertainty = 0;  // (hi - lo) / 2

    uint64_t seed = 0;
    int n_trials = 0;
};

/// Combined GUM + MC validation result.
struct UncertaintyResult {
    GUMResult gum;
    MonteCarloResult mc;

    /// The authoritative expanded uncertainty.
    double expanded_uncertainty = 0;

    /// Which method is authoritative.
    std::string authoritative_method;

    /// True if GUM and MC disagree beyond tolerance.
    bool methods_disagree = false;
    std::string disagreement_detail;

    std::vector<std::string> warnings;
};

// ---- API -------------------------------------------------------------------

/// Evaluate using GUM analytical method (JCGM 100).
GUMResult evaluate_gum(const UncertaintyBudget& budget);

/// Evaluate using Monte Carlo (JCGM 101).
MonteCarloResult evaluate_monte_carlo(
    const UncertaintyBudget& budget,
    const std::function<double(const std::vector<double>&)>& model = {},
    int n_trials = 100000,
    uint64_t seed = 42,
    double coverage_p = 0.95);

/// Evaluate with GUM primary + MC validation (recommended workflow).
UncertaintyResult evaluate_with_validation(
    const UncertaintyBudget& budget,
    const std::function<double(const std::vector<double>&)>& model = {},
    int mc_trials = 100000,
    uint64_t mc_seed = 42);

/// Type B distribution → standard uncertainty.
double type_b_uncertainty(double half_width, DistributionType dist);

/// Student's t quantile for coverage probability p and ν degrees of freedom.
double student_t_quantile(double p, double nu);

} // namespace alignmesh::analysis
