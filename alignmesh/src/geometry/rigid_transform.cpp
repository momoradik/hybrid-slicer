#include "alignmesh/geometry/rigid_transform.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace alignmesh::geometry {

static constexpr double kPi = std::numbers::pi;

// Below this angle (radians) we use first-order Taylor expansions.
static constexpr double kSmallAngle = 1e-10;

// When |theta - pi| is below this, use the special near-pi branch for so3 log.
// The general formula uses theta/(2*sin(theta)) which amplifies roundoff when
// sin(theta) is small. At |theta-pi| < 1e-4, the amplification factor exceeds
// ~15000x, making the symmetric-part extraction more stable.
static constexpr double kNearPi = 1e-4;

// ---- hat / vee -------------------------------------------------------------

Mat3 hat(const Vec3& w) {
    Mat3 m;
    m <<     0, -w(2),  w(1),
         w(2),      0, -w(0),
        -w(1),  w(0),      0;
    return m;
}

Vec3 vee(const Mat3& W) {
    return Vec3(W(2, 1), W(0, 2), W(1, 0));
}

// ---- so(3) exp (Rodrigues) -------------------------------------------------

static Mat3 so3_exp(const Vec3& omega) {
    const double theta_sq = omega.squaredNorm();
    const double theta    = std::sqrt(theta_sq);

    if (theta < kSmallAngle) {
        // R ~ I + [omega]x   (first-order Taylor)
        return Mat3::Identity() + hat(omega);
    }

    const Mat3 W  = hat(omega);
    const Mat3 W2 = W * W;

    return Mat3::Identity()
           + (std::sin(theta) / theta) * W
           + ((1.0 - std::cos(theta)) / theta_sq) * W2;
}

// ---- so(3) log -------------------------------------------------------------

static Vec3 so3_log(const Mat3& R) {
    const double cos_theta = std::clamp((R.trace() - 1.0) * 0.5, -1.0, 1.0);
    const double theta     = std::acos(cos_theta);

    // --- Near identity (theta ~ 0) -----------------------------------------
    if (theta < kSmallAngle) {
        return vee((R - R.transpose()) * 0.5);
    }

    // --- Near 180 deg (theta ~ pi) ------------------------------------------
    if (std::abs(theta - kPi) < kNearPi) {
        // Extract axis from the SYMMETRIC part of R (eliminates the
        // antisymmetric sin(theta)*[n]x term that causes large errors
        // as sin(theta)->0):
        //
        //   R_sym = (R + R^T)/2 = cos(theta)*I + (1-cos(theta))*n*n^T
        //   n*n^T = (R_sym - cos(theta)*I) / (1 - cos(theta))
        //
        // This is exact for orthogonal R, unlike (R+I)/2 which only
        // equals n*n^T at exactly theta = pi.
        const double one_minus_cos = 1.0 - cos_theta;  // ~2 near pi
        const Mat3 nnT = ((R + R.transpose()) * 0.5
                          - cos_theta * Mat3::Identity()) / one_minus_cos;

        Eigen::Index max_idx;
        nnT.diagonal().maxCoeff(&max_idx);

        Vec3 col  = nnT.col(max_idx);
        double nk = std::sqrt(std::abs(col(max_idx)));  // |n_k|
        Vec3 n    = col / nk;                            // +/- unit axis

        // Resolve sign ambiguity using the antisymmetric part of R,
        // which equals 2*sin(theta)*[n]x.  For theta = pi exactly
        // sin(theta) = 0 so either sign is valid.
        Vec3 skew = vee(R - R.transpose());
        if (skew.dot(n) < 0.0) {
            n = -n;
        }

        return theta * n;
    }

    // --- General case -------------------------------------------------------
    return vee((theta / (2.0 * std::sin(theta))) * (R - R.transpose()));
}

// ---- SE(3) V matrix (maps v -> t in the exponential) -----------------------

static Mat3 se3_V(const Vec3& omega) {
    const double theta_sq = omega.squaredNorm();
    const double theta    = std::sqrt(theta_sq);

    if (theta < kSmallAngle) {
        // V ~ I  (first-order)
        return Mat3::Identity();
    }

    const Mat3 W  = hat(omega);
    const Mat3 W2 = W * W;

    return Mat3::Identity()
           + ((1.0 - std::cos(theta)) / theta_sq) * W
           + ((theta - std::sin(theta)) / (theta_sq * theta)) * W2;
}

// ---- RigidTransform --------------------------------------------------------

RigidTransform::RigidTransform() : m_(Mat4::Identity()) {}

RigidTransform RigidTransform::from_matrix(const Mat4& m) {
    RigidTransform rt;
    rt.m_ = m;
    return rt;
}

RigidTransform RigidTransform::from_rotation_translation(
        const Mat3& R, const Vec3& t) {
    RigidTransform rt;
    rt.m_.block<3, 3>(0, 0) = R;
    rt.m_.block<3, 1>(0, 3) = t;
    rt.m_.row(3) << 0.0, 0.0, 0.0, 1.0;
    return rt;
}

RigidTransform RigidTransform::from_quaternion_translation(
        const Quat& q, const Vec3& t) {
    return from_rotation_translation(q.normalized().toRotationMatrix(), t);
}

RigidTransform RigidTransform::exp(const Vec6& twist) {
    const Vec3 omega = twist.head<3>();
    const Vec3 v     = twist.tail<3>();

    const Mat3 R = so3_exp(omega);
    const Mat3 V = se3_V(omega);
    const Vec3 t = V * v;

    return from_rotation_translation(R, t);
}

Vec6 RigidTransform::log() const {
    const Mat3 R = rotation();
    const Vec3 t = translation();
    const Vec3 omega = so3_log(R);

    Vec3 v;
    if (omega.norm() < kSmallAngle) {
        v = t;
    } else {
        v = se3_V(omega).inverse() * t;
    }

    Vec6 twist;
    twist.head<3>() = omega;
    twist.tail<3>() = v;
    return twist;
}

Mat3 RigidTransform::rotation() const {
    return m_.block<3, 3>(0, 0);
}

Vec3 RigidTransform::translation() const {
    return m_.block<3, 1>(0, 3);
}

Quat RigidTransform::quaternion() const {
    return Quat(rotation());
}

RigidTransform RigidTransform::operator*(const RigidTransform& other) const {
    RigidTransform result;
    result.m_.noalias() = m_ * other.m_;
    return result;
}

RigidTransform RigidTransform::inverse() const {
    const Mat3 R  = rotation();
    const Vec3 t  = translation();
    const Mat3 Rt = R.transpose();
    return from_rotation_translation(Rt, -(Rt * t));
}

Vec3 RigidTransform::apply(const Vec3& point) const {
    return rotation() * point + translation();
}

Eigen::Matrix<double, 3, Eigen::Dynamic> RigidTransform::apply_cloud(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points) const {
    return (rotation() * points).colwise() + translation();
}

bool RigidTransform::is_approx(const RigidTransform& other, double tol) const {
    return (m_ - other.m_).norm() < tol;
}

bool RigidTransform::is_identity(double tol) const {
    return (m_ - Mat4::Identity()).norm() < tol;
}

} // namespace alignmesh::geometry
