#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Observability / degeneracy diagnostic + transform covariance
// ============================================================================
//
// SAFETY-CRITICAL: catches "low RMS but meaningless pose."
//
// Computes the registration information (Hessian) matrix from the
// point-to-plane cost and analyzes its eigenvalues to flag
// under-constrained DOFs. Outputs a per-DOF constraint-quality
// vector and a 6×6 pose covariance.
//
// DOF ordering: [rot_x, rot_y, rot_z, trans_x, trans_y, trans_z]
//
// FAIL-SAFE RULE: if any DOF is under-constrained beyond threshold,
// the result is flagged — it CANNOT pass regardless of RMS.
// ============================================================================

/// Per-DOF constraint quality and labels.
struct DOFConstraint {
    /// Eigenvalue of the Hessian for this DOF (higher = better constrained).
    double eigenvalue = 0;

    /// Normalized constraint quality in [0, 1] (0 = unconstrained).
    double quality = 0;

    /// True if this DOF is under-constrained.
    bool under_constrained = false;

    /// Human-readable DOF label (e.g., "rot_z", "trans_x").
    std::string label;
};

struct ObservabilityResult {
    /// True if ALL 6 DOFs are adequately constrained.
    bool fully_constrained = false;

    /// 6×6 information (Hessian) matrix of the point-to-plane cost.
    Eigen::Matrix<double, 6, 6> hessian = Eigen::Matrix<double, 6, 6>::Zero();

    /// Eigenvalues of the Hessian (ascending order).
    Eigen::Matrix<double, 6, 1> eigenvalues = Eigen::Matrix<double, 6, 1>::Zero();

    /// Eigenvectors (columns correspond to eigenvalues).
    Eigen::Matrix<double, 6, 6> eigenvectors = Eigen::Matrix<double, 6, 6>::Zero();

    /// Condition number = max eigenvalue / min eigenvalue.
    double condition_number = 0;

    /// Per-DOF constraint quality (6 entries, mapped to physical DOFs
    /// via the eigenvectors).
    std::vector<DOFConstraint> dof_constraints;

    /// 6×6 pose covariance (inverse of the Hessian, regularized).
    /// Censi 2007 closed-form for point-to-plane.
    Eigen::Matrix<double, 6, 6> covariance = Eigen::Matrix<double, 6, 6>::Zero();

    /// Number of under-constrained DOFs.
    int num_under_constrained = 0;

    /// Warnings about under-constrained directions.
    std::vector<std::string> warnings;
};

/// Compute the point-to-plane Hessian and observability diagnostic.
///
/// @param source_transformed  3×N source points AFTER applying the transform.
/// @param target              3×N target points (correspondences).
/// @param target_normals      3×N target normals.
/// @param residuals           N residuals (point-to-plane distances).
/// @param eigenvalue_threshold  Eigenvalues below this fraction of the max
///                              are flagged as under-constrained.
ObservabilityResult analyze_observability(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_transformed,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_normals,
    double eigenvalue_threshold = 1e-3);

/// Convenience: compute observability from source, target, normals, and transform.
ObservabilityResult analyze_observability(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_normals,
    const geometry::RigidTransform& T,
    double eigenvalue_threshold = 1e-3);

} // namespace alignmesh::registration
