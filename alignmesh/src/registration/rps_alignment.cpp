#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace alignmesh::registration {

// ============================================================================
// Simultaneous Weighted Directional RPS Solver
//
// Gauss-Newton on SE(3), parameterized as (rotation via so(3), translation).
//
// Pose vector ξ = [δθ_x, δθ_y, δθ_z, δt_x, δt_y, δt_z]
//
// For each constraint (point i, direction k):
//   residual r_{i,k} = d_{i,k}ᵀ · (R·m_i + t − p_i)
//
// Jacobian of r_{i,k} w.r.t. ξ:
//   ∂r/∂δθ = d_{i,k}ᵀ · (-[R·m_i]×)  = -d_{i,k}ᵀ · skew(R·m_i)
//   ∂r/∂δt = d_{i,k}ᵀ
//
// Normal equations:  (JᵀWJ) δξ = -JᵀW r
// Update: R ← exp([δθ]×) · R,  t ← t + δt
// ============================================================================

// Skew-symmetric matrix [v]×.
static Eigen::Matrix3d skew(const Eigen::Vector3d& v) {
    Eigen::Matrix3d S;
    S << 0, -v.z(), v.y(),
         v.z(), 0, -v.x(),
         -v.y(), v.x(), 0;
    return S;
}

// Exponential map so(3) → SO(3) via Rodrigues.
static Eigen::Matrix3d exp_so3(const Eigen::Vector3d& omega) {
    double theta = omega.norm();
    if (theta < 1e-14) return Eigen::Matrix3d::Identity();
    Eigen::Vector3d axis = omega / theta;
    Eigen::Matrix3d K = skew(axis);
    return Eigen::Matrix3d::Identity()
         + std::sin(theta) * K
         + (1.0 - std::cos(theta)) * K * K;
}

// Analyze DOF status from the constraint Jacobian.
static RPSDOFStatus analyze_dofs(
        const Eigen::MatrixXd& J,
        double rank_tol) {
    RPSDOFStatus status;
    status.num_constraints = static_cast<int>(J.rows());

    // SVD of J (M×6 matrix).
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(J, Eigen::ComputeFullV);
    auto sv = svd.singularValues();

    double sv_max = (sv.size() > 0) ? sv(0) : 0;
    double threshold = sv_max * rank_tol;

    status.rank = 0;
    for (int i = 0; i < sv.size() && i < 6; ++i) {
        if (sv(i) > threshold) status.rank++;
    }

    status.fully_constrained = (status.rank == 6);
    status.over_determined = (status.num_constraints > 6 && status.rank == 6);

    // Identify free DOFs from null space when under-constrained.
    if (status.rank < 6) {
        auto V = svd.matrixV(); // 6×6
        static const char* dof_names[6] = {"tx", "ty", "tz", "rx", "ry", "rz"};
        // DOFs in the null space: columns of V corresponding to near-zero SVs.
        // Convention: ξ = [δθ_x, δθ_y, δθ_z, δt_x, δt_y, δt_z]
        // Map: col 0-2 = rotation, col 3-5 = translation.
        // Reorder for output: tx(3),ty(4),tz(5),rx(0),ry(1),rz(2)
        int map[6] = {3, 4, 5, 0, 1, 2}; // free_dofs[i] maps to ξ[map[i]]

        for (int d = 0; d < 6; ++d) {
            // Check if DOF d participates in the null space.
            double null_component = 0;
            for (int k = status.rank; k < 6 && k < sv.size(); ++k) {
                null_component += V(map[d], k) * V(map[d], k);
            }
            status.free_dofs[d] = (null_component > 0.1);
            if (status.free_dofs[d]) {
                status.free_dof_names.push_back(dof_names[d]);
            }
        }
    }

    return status;
}

RPSAlignmentResult rps_align(
        const std::vector<RPSConstraintPoint>& points,
        const geometry::RigidTransform& initial,
        const RPSAlignmentSettings& settings) {

    RPSAlignmentResult result;

    // ── Validate inputs ──────────────────────────────────────────────

    if (points.empty()) {
        result.errors.push_back("No constraint points provided");
        return result;
    }

    // Count total directional constraints.
    int total_constraints = 0;
    for (auto& pt : points) {
        if (pt.locks.empty()) {
            result.errors.push_back("Point has no locked directions");
            return result;
        }
        for (auto& lock : pt.locks) {
            if (lock.weight <= 0) {
                result.errors.push_back("Lock weight must be positive");
                return result;
            }
            double norm = lock.direction.norm();
            if (norm < 1e-12) {
                result.errors.push_back("Lock direction has zero length");
                return result;
            }
        }
        total_constraints += static_cast<int>(pt.locks.size());
    }

    // Check for NaN/Inf (fail-safe).
    for (auto& pt : points) {
        if (!pt.nominal.allFinite() || !pt.measured.allFinite()) {
            result.errors.push_back("NaN/Inf in constraint point coordinates");
            return result;
        }
        for (auto& lock : pt.locks) {
            if (!lock.direction.allFinite()) {
                result.errors.push_back("NaN/Inf in lock direction");
                return result;
            }
        }
    }

    // ── Build initial Jacobian for DOF analysis ──────────────────────

    // Current pose.
    Eigen::Matrix3d R = initial.rotation();
    Eigen::Vector3d t = initial.translation();

    // Build Jacobian and check rank BEFORE solving.
    auto build_jacobian_and_residuals = [&](
            const Eigen::Matrix3d& R_cur, const Eigen::Vector3d& t_cur,
            Eigen::MatrixXd& J, Eigen::VectorXd& r, Eigen::VectorXd& w_diag) {

        J.resize(total_constraints, 6);
        r.resize(total_constraints);
        w_diag.resize(total_constraints);

        int row = 0;
        for (auto& pt : points) {
            Eigen::Vector3d Rm = R_cur * pt.measured;
            Eigen::Vector3d transformed = Rm + t_cur;
            Eigen::Vector3d error = transformed - pt.nominal;
            Eigen::Matrix3d neg_skew_Rm = -skew(Rm);

            for (auto& lock : pt.locks) {
                Eigen::Vector3d d = lock.direction.normalized();

                // Residual: dᵀ · (R·m + t − p)
                r(row) = d.dot(error);

                // Jacobian row: [dᵀ · (-[R·m]×),  dᵀ]
                // ξ convention: [δθ_x, δθ_y, δθ_z, δt_x, δt_y, δt_z]
                Eigen::RowVector3d dT = d.transpose();
                J.block<1, 3>(row, 0) = dT * neg_skew_Rm; // ∂r/∂δθ
                J.block<1, 3>(row, 3) = dT;                // ∂r/∂δt

                w_diag(row) = lock.weight;
                row++;
            }
        }
    };

    Eigen::MatrixXd J;
    Eigen::VectorXd r, w_diag;
    build_jacobian_and_residuals(R, t, J, r, w_diag);

    // DOF analysis via rank of J.
    result.dof_status = analyze_dofs(J, settings.rank_tolerance);

    if (!result.dof_status.fully_constrained) {
        result.errors.push_back("Under-constrained: Jacobian rank " +
            std::to_string(result.dof_status.rank) + "/6. Free DOFs: ");
        std::string free_list;
        for (auto& name : result.dof_status.free_dof_names) {
            if (!free_list.empty()) free_list += ", ";
            free_list += name;
        }
        result.errors.back() += free_list;
        return result;
    }

    if (result.dof_status.over_determined) {
        result.redundant_constraints =
            result.dof_status.num_constraints - 6;
        result.warnings.push_back("Over-determined: " +
            std::to_string(result.redundant_constraints) +
            " redundant constraint(s). Priorities govern the trade-off.");
    }

    // ── Gauss-Newton iterations ──────────────────────────────────────

    for (int iter = 0; iter < settings.max_iterations; ++iter) {
        build_jacobian_and_residuals(R, t, J, r, w_diag);

        // Weighted normal equations: (JᵀWJ) δξ = -JᵀW r
        // W = diag(w_diag)
        Eigen::MatrixXd JtW = J.transpose() * w_diag.asDiagonal();
        Eigen::Matrix<double, 6, 6> H = JtW * J;
        Eigen::Matrix<double, 6, 1> g = -JtW * r;

        // Solve for pose increment.
        Eigen::Matrix<double, 6, 1> delta = H.ldlt().solve(g);

        if (!delta.allFinite()) {
            result.errors.push_back("Gauss-Newton produced NaN/Inf at iteration " +
                std::to_string(iter));
            return result;
        }

        // Update pose: R ← exp([δθ]×) · R,  t ← t + δt
        Eigen::Vector3d delta_theta = delta.head<3>();
        Eigen::Vector3d delta_t = delta.tail<3>();

        R = exp_so3(delta_theta) * R;
        t = t + delta_t;

        result.iterations = iter + 1;

        // Convergence check.
        if (delta.norm() < settings.convergence_threshold) break;
    }

    // Verify R is still a valid rotation.
    double det = R.determinant();
    if (std::abs(det - 1.0) > 1e-6) {
        result.errors.push_back("Rotation matrix degenerated (det=" +
            std::to_string(det) + ")");
        return result;
    }
    // Re-orthogonalize R via polar decomposition.
    {
        Eigen::JacobiSVD<Eigen::Matrix3d> svd(R, Eigen::ComputeFullU | Eigen::ComputeFullV);
        R = svd.matrixU() * svd.matrixV().transpose();
        if (R.determinant() < 0) {
            Eigen::Matrix3d D = Eigen::Matrix3d::Identity();
            D(2, 2) = -1;
            R = svd.matrixU() * D * svd.matrixV().transpose();
        }
    }

    result.transform = geometry::RigidTransform::from_rotation_translation(R, t);

    // ── Compute final residuals ──────────────────────────────────────

    result.residuals.resize(points.size());
    std::vector<double> weighted_sq_vals;

    for (std::size_t i = 0; i < points.size(); ++i) {
        auto& pt = points[i];
        auto& res = result.residuals[i];
        res.nominal = pt.nominal;
        Eigen::Vector3d aligned = R * pt.measured + t;
        res.aligned_measured = aligned;
        Eigen::Vector3d error = aligned - pt.nominal;
        res.total_residual = error.norm();

        for (auto& lock : pt.locks) {
            Eigen::Vector3d d = lock.direction.normalized();
            double dir_res = d.dot(error);
            res.directional_residuals.push_back(dir_res);
            weighted_sq_vals.push_back(lock.weight * dir_res * dir_res);
        }

        if (res.total_residual > result.max_residual)
            result.max_residual = res.total_residual;
    }

    // Weighted RMS.
    double sum_wsq = numerics::neumaier_sum(weighted_sq_vals.begin(),
                                             weighted_sq_vals.end());
    std::vector<double> all_weights;
    for (auto& pt : points)
        for (auto& lock : pt.locks)
            all_weights.push_back(lock.weight);
    double sum_w = numerics::neumaier_sum(all_weights.begin(), all_weights.end());
    result.weighted_rms = (sum_w > 1e-30) ? std::sqrt(sum_wsq / sum_w) : 0;

    if (result.weighted_rms > settings.max_rms_warning) {
        result.warnings.push_back("Weighted RMS " +
            std::to_string(result.weighted_rms) + "mm exceeds warning threshold " +
            std::to_string(settings.max_rms_warning) + "mm");
    }

    result.success = true;
    return result;
}

} // namespace alignmesh::registration
