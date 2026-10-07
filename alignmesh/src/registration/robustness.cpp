#include "alignmesh/registration/robustness.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace alignmesh::registration {

using namespace geometry;
using namespace alignment;

// ============================================================================
// Robust kernel weights
// ============================================================================

double robust_weight(RobustKernel kernel, double r_sq, double c) {
    double r = std::sqrt(r_sq);
    double c_sq = c * c;

    switch (kernel) {
    case RobustKernel::NONE:
        return 1.0;

    case RobustKernel::HUBER:
        // w(r) = 1 if |r| <= c, else c/|r|
        return (r <= c) ? 1.0 : c / r;

    case RobustKernel::TUKEY: {
        // w(r) = (1 - (r/c)²)² if |r| <= c, else 0
        if (r >= c) return 0.0;
        double u = 1.0 - r_sq / c_sq;
        return u * u;
    }

    case RobustKernel::GEMAN_MCCLURE:
        // w(r) = c⁴ / (r² + c²)²
        // At r=0: w=1. As r→∞: w→0.
        {
            double denom = r_sq + c_sq;
            return (c_sq * c_sq) / (denom * denom);
        }
    }
    return 1.0;
}

// ============================================================================
// Helpers
// ============================================================================

static void compute_residuals(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const RigidTransform& T,
        std::vector<double>& residuals) {
    auto n = source.cols();
    residuals.resize(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        residuals[static_cast<std::size_t>(i)] =
            (T.apply(source.col(i)) - target.col(i)).norm();
    }
}

static RobustAlignmentResult make_result(
        const RigidTransform& T,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const std::vector<double>& weights,
        double inlier_threshold) {
    RobustAlignmentResult result;
    result.transform = T;
    result.success = true;

    auto n = source.cols();
    auto un = static_cast<std::size_t>(n);
    result.num_total = static_cast<int>(n);
    result.weights = weights;

    // Compute residuals.
    compute_residuals(source, target, T, result.residuals);

    // Classify inliers/outliers.
    std::vector<double> inlier_sq, all_sq;
    for (std::size_t i = 0; i < un; ++i) {
        double r = result.residuals[i];
        all_sq.push_back(r * r);
        if (weights[i] > inlier_threshold) {
            inlier_sq.push_back(r * r);
            ++result.num_inliers;
        } else {
            ++result.num_outliers;
        }
    }

    result.inlier_rms = inlier_sq.empty() ? 0.0 :
        std::sqrt(numerics::neumaier_sum(inlier_sq.begin(), inlier_sq.end()) /
                  static_cast<double>(inlier_sq.size()));
    result.all_rms =
        std::sqrt(numerics::neumaier_sum(all_sq.begin(), all_sq.end()) /
                  static_cast<double>(n));

    result.overlap_percent = 100.0 * result.num_inliers / result.num_total;
    result.outlier_percent = 100.0 * result.num_outliers / result.num_total;
    result.rejected_percent = result.outlier_percent;

    if (result.overlap_percent < 30.0) {
        result.warnings.push_back("Low overlap (" +
            std::to_string(result.overlap_percent) + "%)");
    }

    return result;
}

// ============================================================================
// Trimmed ICP
// ============================================================================

RobustAlignmentResult trimmed_icp(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const TrimmedICPSettings& settings) {
    RobustAlignmentResult result;
    auto n = source.cols();

    if (n != target.cols() || n < 3) {
        result.errors.push_back("Invalid input: need >= 3 matched pairs");
        return result;
    }

    int keep = std::max(3, static_cast<int>(settings.trim_fraction * n));
    double prev_rms = std::numeric_limits<double>::max();

    // Start with identity and iterate.
    RigidTransform T;

    for (int iter = 0; iter < settings.max_iterations; ++iter) {
        // Compute residuals with current T.
        std::vector<double> residuals;
        compute_residuals(source, target, T, residuals);

        // Sort by residual to find the best `keep` correspondences.
        std::vector<std::size_t> order(static_cast<std::size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
            [&](std::size_t a, std::size_t b) { return residuals[a] < residuals[b]; });

        // Build trimmed point sets.
        Eigen::Matrix<double, 3, Eigen::Dynamic> src_trim(3, keep);
        Eigen::Matrix<double, 3, Eigen::Dynamic> tgt_trim(3, keep);
        for (int i = 0; i < keep; ++i) {
            auto idx = static_cast<Eigen::Index>(order[static_cast<std::size_t>(i)]);
            src_trim.col(i) = source.col(idx);
            tgt_trim.col(i) = target.col(idx);
        }

        // Solve weighted Kabsch on trimmed set (uniform weights).
        auto kabsch_result = align_landmarks(src_trim, tgt_trim);
        if (!kabsch_result.success) {
            result.errors.push_back("Kabsch failed in trimmed ICP iteration " +
                std::to_string(iter));
            return result;
        }

        T = kabsch_result.transform;
        double cur_rms = kabsch_result.diagnostics.unweighted_rms;

        // Convergence check.
        double rel_change = (prev_rms > 0) ?
            std::abs(cur_rms - prev_rms) / prev_rms : 1.0;
        if (rel_change < settings.convergence_tol && iter > 0) break;
        prev_rms = cur_rms;
    }

    // Build weights: 1.0 for kept, 0.0 for trimmed.
    std::vector<double> residuals;
    compute_residuals(source, target, T, residuals);
    std::vector<std::size_t> order(static_cast<std::size_t>(n));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) { return residuals[a] < residuals[b]; });

    std::vector<double> weights(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < keep; ++i) {
        weights[order[static_cast<std::size_t>(i)]] = 1.0;
    }

    return make_result(T, source, target, weights, 0.5);
}

// ============================================================================
// M-estimator (IRLS)
// ============================================================================

RobustAlignmentResult m_estimator_align(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const MEstimatorSettings& settings) {
    RobustAlignmentResult result;
    auto n = source.cols();

    if (n != target.cols() || n < 3) {
        result.errors.push_back("Invalid input: need >= 3 matched pairs");
        return result;
    }

    RigidTransform T;
    Eigen::VectorXd w = Eigen::VectorXd::Ones(n);  // start uniform
    double prev_rms = std::numeric_limits<double>::max();

    for (int iter = 0; iter < settings.max_iterations; ++iter) {
        // Solve weighted Kabsch.
        auto kabsch_result = align_landmarks(source, target, w);
        if (!kabsch_result.success) {
            result.errors.push_back("Kabsch failed in IRLS iteration " +
                std::to_string(iter));
            return result;
        }

        T = kabsch_result.transform;

        // Compute residuals and update weights.
        std::vector<double> residuals;
        compute_residuals(source, target, T, residuals);

        for (Eigen::Index i = 0; i < n; ++i) {
            double r_sq = residuals[static_cast<std::size_t>(i)];
            r_sq *= r_sq;
            w(i) = robust_weight(settings.kernel, r_sq, settings.kernel_param);
            // Clamp minimum weight to prevent singular Kabsch.
            w(i) = std::max(w(i), 1e-6);
        }

        double cur_rms = kabsch_result.diagnostics.weighted_rms;
        double rel_change = (prev_rms > 0) ?
            std::abs(cur_rms - prev_rms) / prev_rms : 1.0;
        if (rel_change < settings.convergence_tol && iter > 0) break;
        prev_rms = cur_rms;
    }

    // Final weights for classification.
    std::vector<double> final_weights(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i)
        final_weights[static_cast<std::size_t>(i)] = w(i);

    return make_result(T, source, target, final_weights, 0.1);
}

// ============================================================================
// Graduated Non-Convexity (GNC)
// ============================================================================

RobustAlignmentResult gnc_align(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const GNCSettings& settings) {
    RobustAlignmentResult result;
    auto n = source.cols();

    if (n != target.cols() || n < 3) {
        result.errors.push_back("Invalid input: need >= 3 matched pairs");
        return result;
    }

    double mu = settings.initial_mu;
    double c = settings.kernel_param;
    double c_sq = c * c;
    RigidTransform T;
    Eigen::VectorXd w = Eigen::VectorXd::Ones(n);

    // Graduated annealing: decrease µ from large (convex) to small (non-convex).
    while (mu >= settings.final_mu) {
        // Inner loop: IRLS at fixed µ.
        for (int inner = 0; inner < settings.max_inner_iterations; ++inner) {
            auto kabsch_result = align_landmarks(source, target, w);
            if (!kabsch_result.success) {
                result.errors.push_back("Kabsch failed in GNC at mu=" +
                    std::to_string(mu));
                return result;
            }
            T = kabsch_result.transform;

            // Update weights using GNC surrogate:
            // w_i(µ) = (µ * c²)² / (r_i² + µ * c²)²
            // At large µ: w → 1 for all (convex).
            // At µ = 1: recovers Geman–McClure.
            std::vector<double> residuals;
            compute_residuals(source, target, T, residuals);

            bool converged = true;
            for (Eigen::Index i = 0; i < n; ++i) {
                double r_sq = residuals[static_cast<std::size_t>(i)];
                r_sq *= r_sq;
                double mu_c_sq = mu * c_sq;
                double denom = r_sq + mu_c_sq;
                double new_w = (mu_c_sq * mu_c_sq) / (denom * denom);
                new_w = std::max(new_w, 1e-6);

                if (std::abs(new_w - w(i)) > settings.convergence_tol)
                    converged = false;
                w(i) = new_w;
            }
            if (converged) break;
        }

        mu /= settings.mu_factor;
    }

    // Final weights for classification.
    std::vector<double> final_weights(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i)
        final_weights[static_cast<std::size_t>(i)] = w(i);

    return make_result(T, source, target, final_weights, 0.1);
}

} // namespace alignmesh::registration
