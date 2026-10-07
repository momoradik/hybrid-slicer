#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/spatial/kdtree.h"

#include <Eigen/Core>
#include <vector>

namespace alignmesh::spatial {

/// Per-point normal estimation result.
struct NormalEstimation {
    /// 3xN estimated normals (unit vectors, consistently oriented).
    Eigen::Matrix<double, 3, Eigen::Dynamic> normals;

    /// Per-point curvature measure: lambda_min / (lambda_0 + lambda_1 + lambda_2).
    /// Low = flat, high = curved/edge.
    Eigen::VectorXd curvatures;

    /// Per-point reliability score (0 = unreliable, 1 = high confidence).
    /// Based on neighbour count and eigenvalue spread.
    Eigen::VectorXd reliabilities;

    /// Points whose normals could not be reliably estimated.
    std::vector<Eigen::Index> unreliable_indices;
};

/// Estimate normals via PCA over a k-nearest-neighbour neighbourhood.
/// Consistent orientation is propagated via a minimum-spanning-tree flip
/// resolution across the point cloud.
///
/// @param points   3xN point cloud.
/// @param k        number of neighbours for PCA (typically 10-30).
/// @param tree     optional pre-built KD-tree (if null, one is built internally).
NormalEstimation estimate_normals_knn(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    int k,
    const KdTree* tree = nullptr);

/// Estimate normals via PCA over a radius neighbourhood.
NormalEstimation estimate_normals_radius(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    double radius,
    const KdTree* tree = nullptr);

} // namespace alignmesh::spatial
