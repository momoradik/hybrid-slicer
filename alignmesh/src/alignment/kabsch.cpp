#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <limits>

namespace alignmesh::alignment {

using namespace geometry;

// Thresholds for conditioning / degeneracy detection.
static constexpr double kCollinearThreshold   = 1e-8;  // sv_min/sv_max
static constexpr double kCoplanarThreshold    = 1e-4;  // sv_min/sv_max
static constexpr double kDuplicateDistSq      = 1e-24; // distance^2
static constexpr double kWeightImbalanceWarn  = 10.0;

// ---------------------------------------------------------------------------
// Deterministic weighted centroid using Neumaier compensated summation.
// ---------------------------------------------------------------------------

static Vec3 weighted_centroid(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        const Eigen::VectorXd& w,
        double w_sum) {
    auto n = pts.cols();
    // Sum w_i * x_i for each coordinate using compensated summation.
    std::vector<double> wx(static_cast<std::size_t>(n));
    std::vector<double> wy(static_cast<std::size_t>(n));
    std::vector<double> wz(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        auto ui = static_cast<std::size_t>(i);
        wx[ui] = w(i) * pts(0, i);
        wy[ui] = w(i) * pts(1, i);
        wz[ui] = w(i) * pts(2, i);
    }
    return Vec3(
        numerics::neumaier_sum(wx.begin(), wx.end()) / w_sum,
        numerics::neumaier_sum(wy.begin(), wy.end()) / w_sum,
        numerics::neumaier_sum(wz.begin(), wz.end()) / w_sum);
}

// ---------------------------------------------------------------------------
// Deterministic weighted cross-covariance H = sum w_i * (s_i - cs) * (t_i - ct)^T
// ---------------------------------------------------------------------------

static Mat3 weighted_cross_covariance(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const Eigen::VectorXd& w,
        const Vec3& cs, const Vec3& ct) {
    auto n = source.cols();

    // Compute each of the 9 elements of H using compensated summation
    // to ensure deterministic reduction order.
    std::vector<double> buf(static_cast<std::size_t>(n));
    Mat3 H = Mat3::Zero();

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            for (Eigen::Index i = 0; i < n; ++i) {
                buf[static_cast<std::size_t>(i)] =
                    w(i) * (source(r, i) - cs(r)) * (target(c, i) - ct(c));
            }
            H(r, c) = numerics::neumaier_sum(buf.begin(), buf.end());
        }
    }
    return H;
}

// ---------------------------------------------------------------------------

AlignmentResult align_landmarks(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target,
        const Eigen::VectorXd& weights_in) {
    AlignmentResult result;
    auto n = source.cols();

    // ---- input validation --------------------------------------------------

    if (source.cols() != target.cols()) {
        result.errors.push_back("Source/target size mismatch (" +
            std::to_string(source.cols()) + " vs " + std::to_string(target.cols()) + ")");
        return result;
    }
    if (n < 3) {
        result.errors.push_back("Fewer than 3 point pairs (" +
            std::to_string(n) + ")");
        return result;
    }

    // Weights: default to uniform if not provided.
    Eigen::VectorXd w;
    if (weights_in.size() == 0) {
        w = Eigen::VectorXd::Ones(n);
    } else if (weights_in.size() != n) {
        result.errors.push_back("Weight vector size mismatch (" +
            std::to_string(weights_in.size()) + " vs " + std::to_string(n) + ")");
        return result;
    } else {
        w = weights_in;
    }

    // Check for zero/negative weights.
    for (Eigen::Index i = 0; i < n; ++i) {
        if (w(i) <= 0.0) {
            result.errors.push_back("Weight " + std::to_string(i) +
                " is non-positive (" + std::to_string(w(i)) + ")");
            return result;
        }
    }

    // Check for NaN/Inf in inputs.
    for (Eigen::Index i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(source(c, i)) || !std::isfinite(target(c, i))) {
                result.errors.push_back("NaN or Inf in input at point " +
                    std::to_string(i));
                return result;
            }
        }
    }

    // Check for duplicate source points.
    for (Eigen::Index i = 0; i < n; ++i) {
        for (Eigen::Index j = i + 1; j < n; ++j) {
            if ((source.col(i) - source.col(j)).squaredNorm() < kDuplicateDistSq) {
                result.errors.push_back("Duplicate source points at indices " +
                    std::to_string(i) + " and " + std::to_string(j));
                return result;
            }
        }
    }

    // ---- weight sum + imbalance -------------------------------------------

    std::vector<double> w_vec(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i)
        w_vec[static_cast<std::size_t>(i)] = w(i);
    double w_sum = numerics::neumaier_sum(w_vec.begin(), w_vec.end());

    double w_max = w.maxCoeff();
    double w_min = w.minCoeff();
    result.diagnostics.weight_imbalance = w_max / w_min;

    if (result.diagnostics.weight_imbalance > kWeightImbalanceWarn) {
        result.warnings.push_back(
            "Weight imbalance = " + std::to_string(result.diagnostics.weight_imbalance) +
            " (one point dominates the fit)");
    }

    // ---- weighted centroids -----------------------------------------------

    Vec3 cs = weighted_centroid(source, w, w_sum);
    Vec3 ct = weighted_centroid(target, w, w_sum);

    // ---- weighted cross-covariance H = sum w_i * (s_i-cs) * (t_i-ct)^T ---

    Mat3 H = weighted_cross_covariance(source, target, w, cs, ct);

    // ---- SVD of H ---------------------------------------------------------

    Eigen::JacobiSVD<Mat3> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Vec3 sv = svd.singularValues();

    result.diagnostics.sv_max = sv(0);
    result.diagnostics.sv_mid = sv(1);
    result.diagnostics.sv_min = sv(2);
    result.diagnostics.condition_score =
        (sv(0) > 0) ? sv(2) / sv(0) : 0.0;

    // ---- degeneracy / conditioning checks ---------------------------------

    if (sv(0) < std::numeric_limits<double>::epsilon()) {
        result.errors.push_back("Cross-covariance matrix is zero — "
            "all centred points are at the origin");
        return result;
    }

    // Collinear: sv_mid is also near-zero → rank 1 → cannot determine rotation.
    double mid_ratio = sv(1) / sv(0);
    if (mid_ratio < kCollinearThreshold) {
        result.errors.push_back(
            "Configuration is collinear or near-collinear (sv_mid/sv_max = " +
            std::to_string(mid_ratio) +
            ") — alignment is rank-deficient");
        return result;
    }

    // Coplanar: sv_min near-zero but sv_mid is fine → rank 2.
    // The in-plane rotation is well-determined but the out-of-plane
    // component is unconstrained. Warn but allow.
    if (result.diagnostics.condition_score < kCoplanarThreshold) {
        result.warnings.push_back(
            "Near-coplanar configuration (condition = " +
            std::to_string(result.diagnostics.condition_score) +
            ") — out-of-plane component is poorly constrained");
    }

    // ---- rotation: R = V * diag(1, 1, det(V*U^T)) * U^T ------------------
    //
    // The determinant correction prevents reflections (det(R) = +1).

    Mat3 U = svd.matrixU();
    Mat3 V = svd.matrixV();

    double d = (V * U.transpose()).determinant();
    Mat3 S = Mat3::Identity();
    if (d < 0.0) {
        S(2, 2) = -1.0;
    }

    Mat3 R = V * S * U.transpose();

    // ---- translation: t = ct - R * cs -------------------------------------

    Vec3 t = ct - R * cs;

    result.transform = RigidTransform::from_rotation_translation(R, t);

    // ---- residuals --------------------------------------------------------

    result.diagnostics.residuals.resize(static_cast<std::size_t>(n));
    std::vector<double> w_resid_sq(static_cast<std::size_t>(n));
    std::vector<double> resid_sq(static_cast<std::size_t>(n));

    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 mapped = result.transform.apply(source.col(i));
        double r = (mapped - target.col(i)).norm();
        auto ui = static_cast<std::size_t>(i);
        result.diagnostics.residuals[ui] = r;
        w_resid_sq[ui] = w(i) * r * r;
        resid_sq[ui] = r * r;
    }

    double sum_w_resid_sq = numerics::neumaier_sum(w_resid_sq.begin(), w_resid_sq.end());
    double sum_resid_sq = numerics::neumaier_sum(resid_sq.begin(), resid_sq.end());

    result.diagnostics.weighted_rms = std::sqrt(sum_w_resid_sq / w_sum);
    result.diagnostics.unweighted_rms = std::sqrt(sum_resid_sq / static_cast<double>(n));

    result.success = true;
    return result;
}

} // namespace alignmesh::alignment
