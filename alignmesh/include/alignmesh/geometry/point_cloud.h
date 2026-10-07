#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <optional>
#include <string>
#include <vector>

namespace alignmesh::geometry {

// ============================================================================
// PointCloud -- double-precision 3D point set with optional attributes
// ============================================================================
//
// Immutable-source-aware: if constructed with a source_hash (imported data),
// the instance represents original geometry that must never be modified in
// place.  transformed() always returns a NEW instance that is NOT a source.
//
// Attributes:
//   points      (required): 3xN matrix, each column is a point
//   normals     (optional): 3xN matrix, unit normals per point
//   covariances (optional): per-point 3x3 covariance matrices
// ============================================================================

class PointCloud {
public:
    /// Construct from points only.
    explicit PointCloud(Eigen::Matrix<double, 3, Eigen::Dynamic> points);

    /// Construct with all optional attributes.
    PointCloud(Eigen::Matrix<double, 3, Eigen::Dynamic> points,
               std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> normals,
               std::optional<std::vector<Mat3>> covariances,
               std::string source_hash = {});

    Eigen::Index size() const { return points_.cols(); }

    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points() const { return points_; }

    bool has_normals() const { return normals_.has_value(); }
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals() const;

    bool has_covariances() const { return covariances_.has_value(); }
    const std::vector<Mat3>& covariances() const;

    bool is_source() const { return !source_hash_.empty(); }
    const std::string& source_hash() const { return source_hash_; }

    /// Apply rigid transform. Returns a NEW cloud (never modifies *this).
    /// The returned cloud is NOT a source (derived copy).
    ///
    /// Transform rules:
    ///   points:      p' = R*p + t
    ///   normals:     n' = R*n     (rotation only)
    ///   covariances: S' = R*S*R^T
    PointCloud transformed(const RigidTransform& T) const;

private:
    Eigen::Matrix<double, 3, Eigen::Dynamic> points_;
    std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> normals_;
    std::optional<std::vector<Mat3>> covariances_;
    std::string source_hash_;
};

} // namespace alignmesh::geometry
