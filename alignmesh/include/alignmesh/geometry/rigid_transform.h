#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace alignmesh::geometry {

// ============================================================================
// Type aliases for the geometry module (IEEE-754 binary64 throughout)
// ============================================================================

/// 4x4 homogeneous transformation matrix, row-major storage.
using Mat4 = Eigen::Matrix<double, 4, 4, Eigen::RowMajor>;
/// 3x3 matrix (Eigen default column-major, internal computation only).
using Mat3 = Eigen::Matrix3d;
/// 3-vector.
using Vec3 = Eigen::Vector3d;
/// 4-vector (homogeneous coordinates).
using Vec4 = Eigen::Vector4d;
/// Quaternion (Hamilton convention, Eigen default).
using Quat = Eigen::Quaterniond;
/// 6-vector for SE(3) Lie algebra (twist).
using Vec6 = Eigen::Matrix<double, 6, 1>;

// ============================================================================
// RigidTransform -- SE(3) rigid body transformation
// ============================================================================
//
// CONVENTION (enforced by ConventionXToY test -- the classic silent-killer
// detector for transpose/handedness bugs):
//
//   Storage:     4x4 matrix, ROW-MAJOR (Eigen::RowMajor)
//   Application: COLUMN-VECTOR convention:  point' = T * point
//   Frame:       world frame explicit
//   Handedness:  right-handed coordinates
//
// Matrix layout:
//
//   | R00  R01  R02  tx |     p' = R*p + t
//   | R10  R11  R12  ty |
//   | R20  R21  R22  tz |
//   |  0    0    0    1 |
//
// Composition: (A * B) applies B first, then A.
// Inverse:     T^-1 = [R^T, -R^T*t; 0, 1]
//
// Twist (SE(3) Lie algebra) convention:
//   twist[0:3] = omega (rotation axis * angle, so(3) element)
//   twist[3:6] = v     (translational component)
//
//   exp(twist^) = [exp([omega]x), V*v; 0, 1]
//
// ============================================================================

class RigidTransform {
public:
    /// Construct the identity transform.
    RigidTransform();

    /// Construct from a 4x4 matrix. Caller ensures valid SE(3).
    static RigidTransform from_matrix(const Mat4& m);

    /// Construct from rotation matrix + translation.
    static RigidTransform from_rotation_translation(const Mat3& R, const Vec3& t);

    /// Construct from unit quaternion + translation.
    static RigidTransform from_quaternion_translation(const Quat& q, const Vec3& t);

    /// SE(3) exponential map: twist -> rigid transform.
    static RigidTransform exp(const Vec6& twist);

    /// SE(3) logarithm: rigid transform -> twist.
    Vec6 log() const;

    /// The underlying 4x4 matrix (row-major).
    const Mat4& matrix() const { return m_; }

    /// Extract the 3x3 rotation block.
    Mat3 rotation() const;

    /// Extract the translation vector.
    Vec3 translation() const;

    /// Convert rotation to unit quaternion.
    Quat quaternion() const;

    /// Compose: result = *this * other (apply other first, then *this).
    RigidTransform operator*(const RigidTransform& other) const;

    /// Inverse: T^-1 such that T * T^-1 = identity.
    RigidTransform inverse() const;

    /// Apply to a single 3D point: p' = R*p + t.
    Vec3 apply(const Vec3& point) const;

    /// Apply to a point cloud (3xN, each column is a point).
    /// Named differently from apply() to avoid overload ambiguity with
    /// Eigen's implicit matrix-type conversions.
    Eigen::Matrix<double, 3, Eigen::Dynamic> apply_cloud(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points) const;

    /// Approximate equality (Frobenius norm of matrix difference).
    bool is_approx(const RigidTransform& other, double tol = 1e-12) const;

    /// Check if approximately identity.
    bool is_identity(double tol = 1e-12) const;

private:
    Mat4 m_;
};

// Free functions for so(3) operations (useful for testing / advanced use).

/// Skew-symmetric matrix from 3-vector: [omega]x.
Mat3 hat(const Vec3& omega);

/// Extract 3-vector from skew-symmetric matrix.
Vec3 vee(const Mat3& omega_hat);

} // namespace alignmesh::geometry
