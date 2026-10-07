#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::alignment {

// ============================================================================
// Weighted Kabsch / Umeyama rigid-alignment solver
// ============================================================================
//
// AUTHORITATIVE result path (landmark mode) — must be exact and deterministic.
//
// Algorithm: SVD-based closed-form weighted Procrustes
//   (Arun–Huang–Blostein 1987 / Umeyama 1991).
//
//   - Scale = 1 (fixed, never estimated).
//   - Reflection prevented: det(R) = +1 enforced via the determinant
//     correction on the right singular vectors.
//   - All centroids and covariance sums use the deterministic reduction
//     utilities (neumaier_sum / pairwise_sum), never raw accumulate.
//
// Solves:  target ≈ R * source + t   (column-vector convention)
//   i.e. returns T such that  T.apply(source_i) ≈ target_i
//
// Fail-safe: degenerate, under-constrained, or ill-conditioned
// configurations are FLAGGED, never silently solved.
// ============================================================================

/// Diagnostics from the alignment.
struct AlignmentDiagnostics {
    double weighted_rms = 0.0;      // sqrt(sum(w_i * ||r_i||^2) / sum(w_i))
    double unweighted_rms = 0.0;    // sqrt(sum(||r_i||^2) / N)
    std::vector<double> residuals;  // per-landmark ||T*source_i - target_i||

    /// Singular values of the weighted, centred cross-covariance (descending).
    /// Used to assess configuration conditioning.
    double sv_max = 0.0;
    double sv_mid = 0.0;
    double sv_min = 0.0;

    /// Condition score = sv_min / sv_max.
    /// Near 0 → ill-conditioned (collinear/coplanar); 1 → ideal.
    double condition_score = 0.0;

    /// Weight imbalance = max(w) / min(w). High → dominated by one point.
    double weight_imbalance = 1.0;
};

/// Result of the alignment.
struct AlignmentResult {
    bool success = false;

    /// The rigid transform: T.apply(source_i) ≈ target_i.
    geometry::RigidTransform transform;

    AlignmentDiagnostics diagnostics;

    /// Non-empty if alignment failed or has quality warnings.
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Solve the weighted Kabsch / Umeyama alignment.
///
/// @param source   3×N source points (each column is a point).
/// @param target   3×N target points (each column is a point).
/// @param weights  N weights (positive). If empty, uniform weights are used.
///
/// Fail-safe conditions (returns success=false):
///   - Fewer than 3 non-degenerate point pairs.
///   - Any weight ≤ 0.
///   - Duplicate source or target points.
///   - Collinear configuration (sv_min / sv_max < threshold).
///   - NaN or Inf in input.
///
/// Warnings (returns success=true with warnings):
///   - Near-coplanar (one singular value much smaller than the others).
///   - Weight imbalance > 10.
AlignmentResult align_landmarks(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const Eigen::VectorXd& weights = Eigen::VectorXd());

} // namespace alignmesh::alignment
