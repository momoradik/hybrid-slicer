#include "alignmesh/analysis/gum_uncertainty.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// Type B distribution → standard uncertainty (JCGM 100 §4.3.7)
// ============================================================================

double type_b_uncertainty(double half_width, DistributionType dist) {
    switch (dist) {
    case DistributionType::NORMAL:      return half_width;
    case DistributionType::RECTANGULAR: return half_width / std::sqrt(3.0);
    case DistributionType::TRIANGULAR:  return half_width / std::sqrt(6.0);
    case DistributionType::U_SHAPED:    return half_width / std::sqrt(2.0);
    }
    return half_width;
}

// ============================================================================
// Student's t quantile (two-tailed)
// ============================================================================
// Approximation for t_{p,ν} using the Abramowitz & Stegun / Hill method.
// For ν > 1000, returns the normal quantile.

double student_t_quantile(double p, double nu) {
    if (nu <= 0) return 2.0;
    if (nu > 1e5) {
        // Normal approximation for large ν.
        // z for p = 0.95 (two-tailed) → 1.96; p=0.975 one-tail.
        // Convert two-tailed p to one-tailed: alpha/2 = (1-p)/2.
        double alpha2 = (1.0 - p) / 2.0;
        // Rational approximation of the normal quantile (Abramowitz & Stegun 26.2.23).
        double t_val = std::sqrt(-2.0 * std::log(alpha2));
        double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
        double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
        double z = t_val - (c0 + c1*t_val + c2*t_val*t_val) /
                            (1.0 + d1*t_val + d2*t_val*t_val + d3*t_val*t_val*t_val);
        return z;
    }

    // Lookup table for common cases (ν, t_{0.95}) — two-tailed 95%.
    // Values from JCGM 100 Table G.2.
    if (std::abs(p - 0.95) < 0.001) {
        struct Entry { double nu; double t; };
        static const Entry table[] = {
            {1, 12.706}, {2, 4.303}, {3, 3.182}, {4, 2.776}, {5, 2.571},
            {6, 2.447}, {7, 2.365}, {8, 2.306}, {9, 2.262}, {10, 2.228},
            {12, 2.179}, {15, 2.131}, {20, 2.086}, {25, 2.060}, {30, 2.042},
            {40, 2.021}, {50, 2.009}, {60, 2.000}, {80, 1.990}, {100, 1.984},
            {200, 1.972}, {500, 1.965}, {1000, 1.962},
        };
        // Find bracketing entries and interpolate.
        int n_entries = sizeof(table) / sizeof(table[0]);
        if (nu <= table[0].nu) return table[0].t;
        if (nu >= table[n_entries-1].nu) return table[n_entries-1].t;
        for (int i = 1; i < n_entries; ++i) {
            if (nu <= table[i].nu) {
                double frac = (nu - table[i-1].nu) / (table[i].nu - table[i-1].nu);
                return table[i-1].t + frac * (table[i].t - table[i-1].t);
            }
        }
    }

    // For other coverage probabilities, use normal approximation.
    double alpha2 = (1.0 - p) / 2.0;
    double t_val = std::sqrt(-2.0 * std::log(alpha2));
    double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
    double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
    double z = t_val - (c0 + c1*t_val + c2*t_val*t_val) /
                        (1.0 + d1*t_val + d2*t_val*t_val + d3*t_val*t_val*t_val);
    // Cornish-Fisher expansion for finite ν.
    double g1 = (z*z*z + z) / (4.0 * nu);
    return z + g1;
}

// ============================================================================
// GUM analytical (JCGM 100 §5.1)
// ============================================================================

GUMResult evaluate_gum(const UncertaintyBudget& budget) {
    GUMResult result;
    result.coverage_probability = budget.coverage_probability;

    auto n = budget.inputs.size();

    // Compute cᵢ·u(xᵢ) for each input.
    std::vector<double> ciu(n);
    result.contribution_magnitudes.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        ciu[i] = budget.inputs[i].sensitivity * budget.inputs[i].standard_uncertainty;
        result.contribution_magnitudes[i] = std::abs(ciu[i]);
    }

    // u_c² = Σ(cᵢuᵢ)² + 2·Σ cᵢcⱼrᵢⱼuᵢuⱼ
    double sum_sq = 0;
    for (std::size_t i = 0; i < n; ++i)
        sum_sq += ciu[i] * ciu[i];
    for (auto& corr : budget.correlations) {
        auto i = static_cast<std::size_t>(corr.index_i);
        auto j = static_cast<std::size_t>(corr.index_j);
        if (i < n && j < n)
            sum_sq += 2.0 * ciu[i] * ciu[j] * corr.correlation_coefficient;
    }
    result.combined_standard_uncertainty = std::sqrt(std::max(sum_sq, 0.0));

    // Welch-Satterthwaite effective DOF (JCGM 100 §G.4).
    double uc4 = sum_sq * sum_sq;
    double ws_denom = 0;
    for (std::size_t i = 0; i < n; ++i) {
        double ciu4 = ciu[i] * ciu[i] * ciu[i] * ciu[i];
        double vi = budget.inputs[i].degrees_of_freedom;
        if (vi > 0) ws_denom += ciu4 / vi;
    }
    result.effective_dof = (ws_denom > 0) ? uc4 / ws_denom : 1e6;
    result.low_dof_warning = (result.effective_dof < 10);

    // Coverage factor: Student's t when ν_eff is finite, override if set.
    if (budget.coverage_factor_override > 0) {
        result.coverage_factor = budget.coverage_factor_override;
    } else {
        result.coverage_factor = student_t_quantile(
            budget.coverage_probability, result.effective_dof);
    }

    // Systematic bias: |b| summed linearly (NOT in quadrature).
    result.systematic_bias_total = 0;
    for (auto& bias : budget.biases) {
        result.systematic_bias_total += std::abs(bias.value_mm);
    }

    // U = k · u_c + |b|
    result.expanded_uncertainty =
        result.coverage_factor * result.combined_standard_uncertainty +
        result.systematic_bias_total;

    // Details.
    for (std::size_t i = 0; i < n; ++i) {
        auto& inp = budget.inputs[i];
        std::ostringstream oss;
        oss << (inp.is_type_a ? "[A] " : "[B] ") << inp.name
            << ": u=" << inp.standard_uncertainty
            << ", c=" << inp.sensitivity
            << ", |cu|=" << result.contribution_magnitudes[i]
            << ", dof=" << inp.degrees_of_freedom;
        result.details.push_back(oss.str());
    }
    for (auto& bias : budget.biases) {
        result.details.push_back(
            "[SYSTEMATIC] " + bias.name + ": |b|=" + std::to_string(bias.value_mm) +
            (bias.corrected ? " (corrected: " + bias.correction_method + ")" : " (UNCORRECTED)"));
    }
    {
        std::ostringstream oss;
        oss << "u_c=" << result.combined_standard_uncertainty
            << ", |b|=" << result.systematic_bias_total
            << ", v_eff=" << result.effective_dof
            << ", k=t(" << budget.coverage_probability << "," << result.effective_dof
            << ")=" << result.coverage_factor
            << ", U=" << result.expanded_uncertainty;
        result.details.push_back(oss.str());
    }
    if (result.low_dof_warning) {
        result.details.push_back(
            "WARNING: low effective DOF (v_eff=" +
            std::to_string(result.effective_dof) +
            ") — coverage factor elevated from k=2 to k=" +
            std::to_string(result.coverage_factor));
    }

    return result;
}

// ============================================================================
// Monte Carlo (JCGM 101)
// ============================================================================

MonteCarloResult evaluate_monte_carlo(
        const UncertaintyBudget& budget,
        const std::function<double(const std::vector<double>&)>& model,
        int n_trials, uint64_t seed, double coverage_p) {
    MonteCarloResult result;
    result.seed = seed;
    result.n_trials = n_trials;
    result.coverage_probability = coverage_p;

    auto n = budget.inputs.size();
    if (n == 0) return result;

    numerics::SeededRng rng(seed);
    std::vector<double> outputs(static_cast<std::size_t>(n_trials));

    for (int trial = 0; trial < n_trials; ++trial) {
        std::vector<double> sample(n);
        for (std::size_t i = 0; i < n; ++i) {
            auto& inp = budget.inputs[i];
            double u = inp.standard_uncertainty;
            switch (inp.distribution) {
            case DistributionType::NORMAL: {
                std::normal_distribution<double> d(0.0, u);
                sample[i] = d(rng);
                break;
            }
            case DistributionType::RECTANGULAR: {
                double a = inp.half_width > 0 ? inp.half_width : u * std::sqrt(3.0);
                std::uniform_real_distribution<double> d(-a, a);
                sample[i] = d(rng);
                break;
            }
            case DistributionType::TRIANGULAR: {
                double a = inp.half_width > 0 ? inp.half_width : u * std::sqrt(6.0);
                std::uniform_real_distribution<double> d(-a/2, a/2);
                sample[i] = d(rng) + d(rng);
                break;
            }
            case DistributionType::U_SHAPED: {
                double a = inp.half_width > 0 ? inp.half_width : u * std::sqrt(2.0);
                std::uniform_real_distribution<double> d(0.0, 6.28318530717959);
                sample[i] = a * std::sin(d(rng));
                break;
            }
            }
        }

        // Add systematic biases as fixed offsets.
        double bias_total = 0;
        for (auto& b : budget.biases) {
            bias_total += b.value_mm;  // signed, applied as offset
        }

        if (model) {
            outputs[static_cast<std::size_t>(trial)] = model(sample) + bias_total;
        } else {
            double y = bias_total;
            for (std::size_t i = 0; i < n; ++i)
                y += budget.inputs[i].sensitivity * sample[i];
            outputs[static_cast<std::size_t>(trial)] = y;
        }
    }

    // Statistics.
    double sum = numerics::neumaier_sum(outputs.begin(), outputs.end());
    result.mean = sum / n_trials;
    std::vector<double> dev_sq(static_cast<std::size_t>(n_trials));
    for (int i = 0; i < n_trials; ++i) {
        double d = outputs[static_cast<std::size_t>(i)] - result.mean;
        dev_sq[static_cast<std::size_t>(i)] = d * d;
    }
    result.std_dev = std::sqrt(
        numerics::neumaier_sum(dev_sq.begin(), dev_sq.end()) / (n_trials - 1));

    // Shortest coverage interval (JCGM 101 §7.7).
    std::sort(outputs.begin(), outputs.end());
    int cov_count = static_cast<int>(std::ceil(coverage_p * n_trials));
    cov_count = std::min(cov_count, n_trials);

    double best_width = std::numeric_limits<double>::max();
    int best_start = 0;
    for (int i = 0; i <= n_trials - cov_count; ++i) {
        double w = outputs[static_cast<std::size_t>(i + cov_count - 1)] -
                   outputs[static_cast<std::size_t>(i)];
        if (w < best_width) { best_width = w; best_start = i; }
    }
    result.coverage_lo = outputs[static_cast<std::size_t>(best_start)];
    result.coverage_hi = outputs[static_cast<std::size_t>(best_start + cov_count - 1)];
    result.expanded_uncertainty = (result.coverage_hi - result.coverage_lo) / 2.0;

    return result;
}

// ============================================================================
// GUM + MC validation workflow
// ============================================================================

UncertaintyResult evaluate_with_validation(
        const UncertaintyBudget& budget,
        const std::function<double(const std::vector<double>&)>& model,
        int mc_trials, uint64_t mc_seed) {
    UncertaintyResult result;

    // GUM primary.
    result.gum = evaluate_gum(budget);

    // MC validation.
    result.mc = evaluate_monte_carlo(budget, model, mc_trials, mc_seed,
                                      budget.coverage_probability);

    // Compare: JCGM 101 §8 validation.
    // Tolerance for agreement: numerical tolerance δ on coverage endpoints.
    double delta = 0.05 * result.gum.expanded_uncertainty;  // 5% of U
    double gum_lo = -result.gum.expanded_uncertainty;
    double gum_hi = +result.gum.expanded_uncertainty;

    bool lo_agrees = std::abs(result.mc.coverage_lo - gum_lo) < delta;
    bool hi_agrees = std::abs(result.mc.coverage_hi - gum_hi) < delta;

    if (lo_agrees && hi_agrees) {
        result.authoritative_method = "GUM (validated by MC)";
        result.expanded_uncertainty = result.gum.expanded_uncertainty;
    } else {
        result.methods_disagree = true;
        result.disagreement_detail =
            "GUM U=" + std::to_string(result.gum.expanded_uncertainty) +
            " vs MC interval [" + std::to_string(result.mc.coverage_lo) +
            ", " + std::to_string(result.mc.coverage_hi) + "]";

        // Fail-safe: take the LARGER U.
        double mc_U = result.mc.expanded_uncertainty;
        if (mc_U > result.gum.expanded_uncertainty) {
            result.expanded_uncertainty = mc_U;
            result.authoritative_method = "MC (larger — GUM-MC disagreement)";
        } else {
            result.expanded_uncertainty = result.gum.expanded_uncertainty;
            result.authoritative_method = "GUM (larger — GUM-MC disagreement)";
        }
        result.warnings.push_back(
            "GUM and MC disagree beyond 5% tolerance: " +
            result.disagreement_detail);
    }

    if (result.gum.low_dof_warning) {
        result.warnings.push_back(
            "Low effective DOF (v_eff=" +
            std::to_string(result.gum.effective_dof) +
            ") — coverage factor elevated by Student's t");
    }

    return result;
}

} // namespace alignmesh::analysis
