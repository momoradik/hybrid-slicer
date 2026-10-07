#include "alignmesh/registration/observability.h"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>

namespace alignmesh::registration {

using namespace geometry;

// DOF labels: [rot_x, rot_y, rot_z, trans_x, trans_y, trans_z]
static const char* kDOFLabels[6] = {
    "rot_x", "rot_y", "rot_z", "trans_x", "trans_y", "trans_z"
};

// ============================================================================
// Point-to-plane Hessian computation
// ============================================================================
//
// For point-to-plane ICP, the cost for each correspondence is:
//   e_i = n_i^T * (T * s_i - t_i)
//
// The Jacobian of e_i w.r.t. the 6-DOF pose [omega, v] is:
//   J_i = [ n_i^T * [-(T*s_i)]_x,  n_i^T ]     (1×6 row vector)
//       = [ -(T*s_i × n_i)^T,       n_i^T ]
//
// The Hessian (information matrix) is:
//   H = sum_i J_i^T * J_i     (6×6 positive semi-definite)
//
// The covariance of the optimal pose estimate (Censi 2007):
//   Σ = σ² * H⁻¹
// where σ² is the residual variance at the optimum.

ObservabilityResult analyze_observability(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_transformed,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_normals,
        double eigenvalue_threshold) {
    ObservabilityResult result;
    auto n = source_transformed.cols();

    if (n < 3 || target.cols() != n || target_normals.cols() != n) {
        result.warnings.push_back("Insufficient points for observability analysis");
        return result;
    }

    // ---- Build the Hessian H = sum J_i^T * J_i ----------------------------
    Eigen::Matrix<double, 6, 6> H = Eigen::Matrix<double, 6, 6>::Zero();
    double sum_residual_sq = 0;

    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 p = source_transformed.col(i);   // T * s_i
        Vec3 ni = target_normals.col(i);

        // Jacobian row: J_i = [-(p × n)^T, n^T]
        Vec3 cross = p.cross(ni);  // p × n
        Eigen::Matrix<double, 6, 1> Ji;
        Ji.head<3>() = -cross;  // rotation part
        Ji.tail<3>() = ni;      // translation part

        H += Ji * Ji.transpose();

        // Residual for covariance scaling.
        double e = ni.dot(p - target.col(i));
        sum_residual_sq += e * e;
    }

    result.hessian = H;

    // ---- Eigendecomposition of H -------------------------------------------
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix<double, 6, 6>> eig(H);
    result.eigenvalues = eig.eigenvalues();     // ascending order
    result.eigenvectors = eig.eigenvectors();

    double ev_max = result.eigenvalues(5);
    double ev_min = result.eigenvalues(0);
    result.condition_number = (ev_min > 1e-20) ? ev_max / ev_min : 1e20;

    // ---- Per-DOF constraint quality ----------------------------------------
    // Map eigenvectors to physical DOFs. The eigenvalue for each eigenvector
    // indicates how well that direction is constrained.
    //
    // We also project back to the canonical DOF axes to give per-axis quality.
    result.dof_constraints.resize(6);

    // Compute per-canonical-DOF quality by summing eigenvalue contributions
    // weighted by the projection of each eigenvector onto each canonical axis.
    for (int d = 0; d < 6; ++d) {
        result.dof_constraints[static_cast<std::size_t>(d)].label = kDOFLabels[d];
        double quality_sum = 0;
        for (int k = 0; k < 6; ++k) {
            double projection = result.eigenvectors(d, k);
            quality_sum += projection * projection * result.eigenvalues(k);
        }
        // quality_sum = H(d,d) = diagonal element of the Hessian.
        // This represents the constraint strength along canonical DOF d.
        result.dof_constraints[static_cast<std::size_t>(d)].eigenvalue = quality_sum;
    }

    // Normalize quality to [0, 1] relative to the max diagonal.
    double max_diag = 0;
    for (int d = 0; d < 6; ++d) {
        max_diag = std::max(max_diag,
            result.dof_constraints[static_cast<std::size_t>(d)].eigenvalue);
    }

    result.num_under_constrained = 0;
    for (int d = 0; d < 6; ++d) {
        auto& dof = result.dof_constraints[static_cast<std::size_t>(d)];
        dof.quality = (max_diag > 0) ? dof.eigenvalue / max_diag : 0;
        dof.under_constrained = (dof.quality < eigenvalue_threshold);
        if (dof.under_constrained) {
            ++result.num_under_constrained;
            result.warnings.push_back(
                "DOF '" + dof.label + "' is under-constrained (quality=" +
                std::to_string(dof.quality) + ")");
        }
    }

    result.fully_constrained = (result.num_under_constrained == 0);

    // ---- Covariance: Σ = σ² * H⁻¹ (Censi 2007) ----------------------------
    // Regularize H to ensure invertibility.
    double regularization = ev_max * 1e-12;
    Eigen::Matrix<double, 6, 6> H_reg = H +
        regularization * Eigen::Matrix<double, 6, 6>::Identity();

    result.covariance = H_reg.inverse();

    // Scale by residual variance (σ² = sum(e²) / (n - 6)).
    double dof_count = static_cast<double>(n) - 6.0;
    if (dof_count > 0 && sum_residual_sq > 0) {
        double sigma_sq = sum_residual_sq / dof_count;
        result.covariance *= sigma_sq;
    }

    return result;
}

// Convenience overload with transform.
ObservabilityResult analyze_observability(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_normals,
        const RigidTransform& T,
        double eigenvalue_threshold) {
    auto source_transformed = T.apply_cloud(source);
    return analyze_observability(source_transformed, target, target_normals,
                                 eigenvalue_threshold);
}

} // namespace alignmesh::registration
