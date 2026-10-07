#pragma once

#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/alignment/kabsch.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Robust M-estimator kernels
// ============================================================================
//
// Each kernel maps a squared residual r² to a weight w(r²) ∈ [0,1] that
// down-weights outliers. The weight is used in the weighted Kabsch solver.

enum class RobustKernel { NONE, HUBER, TUKEY, GEMAN_MCCLURE };

/// Compute the robust weight for a squared residual.
/// @param kernel   The kernel type.
/// @param r_sq     Squared residual (distance²).
/// @param c        Kernel threshold parameter (units of distance, not squared).
double robust_weight(RobustKernel kernel, double r_sq, double c);

// ============================================================================
// Trimmed ICP result
// ============================================================================

struct RobustAlignmentResult {
    bool success = false;
    geometry::RigidTransform transform;

    double inlier_rms = 0;          // RMS over inlier set only
    double all_rms = 0;             // RMS over all correspondences
    int num_inliers = 0;
    int num_outliers = 0;
    int num_total = 0;
    double overlap_percent = 0;     // inliers / total * 100
    double outlier_percent = 0;     // outliers / total * 100
    double rejected_percent = 0;    // trimmed / total * 100

    std::vector<double> residuals;  // per-correspondence residual
    std::vector<double> weights;    // per-correspondence weight (0 = outlier)

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

// ============================================================================
// Trimmed ICP
// ============================================================================
//
// Given N correspondence pairs (source_i, target_i), solve a weighted
// Kabsch alignment keeping only the best `trim_fraction` of correspondences
// (by residual). Iterates: solve → re-trim → solve until stable.

struct TrimmedICPSettings {
    double trim_fraction = 0.8;     // keep this fraction of correspondences
    int max_iterations = 20;
    double convergence_tol = 1e-6;  // relative change in inlier RMS
};

RobustAlignmentResult trimmed_icp(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const TrimmedICPSettings& settings = {});

// ============================================================================
// M-estimator weighted alignment
// ============================================================================
//
// Iteratively Reweighted Least Squares (IRLS): solve weighted Kabsch,
// recompute weights from residuals using the robust kernel, repeat.

struct MEstimatorSettings {
    RobustKernel kernel = RobustKernel::HUBER;
    double kernel_param = 1.0;      // c parameter for the kernel
    int max_iterations = 20;
    double convergence_tol = 1e-6;
};

RobustAlignmentResult m_estimator_align(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const MEstimatorSettings& settings = {});

// ============================================================================
// Graduated Non-Convexity (GNC) — Yang et al., RA-L 2020
// ============================================================================
//
// Starts with a convex surrogate (large µ → all weights ≈ 1) and gradually
// decreases µ toward the non-convex Geman–McClure kernel, annealing the
// outlier rejection. Avoids local minima that plague direct robust estimation.

struct GNCSettings {
    double initial_mu = 100.0;      // large = convex start
    double mu_factor = 1.4;         // division factor per step
    double final_mu = 1.0;          // stop when µ ≤ this
    double kernel_param = 1.0;      // c for the underlying kernel
    int max_inner_iterations = 10;  // Kabsch iterations per µ level
    double convergence_tol = 1e-6;
};

RobustAlignmentResult gnc_align(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const GNCSettings& settings = {});

} // namespace alignmesh::registration
