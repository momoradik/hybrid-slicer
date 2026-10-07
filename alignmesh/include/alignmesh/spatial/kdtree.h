#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <memory>
#include <vector>

namespace alignmesh::spatial {

/// Result of a nearest-neighbour query.
struct NNResult {
    int index = -1;          // index of the nearest point
    double distance_sq = 0;  // squared Euclidean distance
};

// ============================================================================
// KdTree — double-precision KD-tree for point nearest-neighbour queries
// ============================================================================
//
// Wraps nanoflann. This is point NN, NOT a ray-casting BVH.
// All coordinates are IEEE-754 binary64 (double).
// ============================================================================

class KdTree {
public:
    /// Build from a 3xN point matrix (each column is a point).
    /// Points are copied internally.
    explicit KdTree(const Eigen::Matrix<double, 3, Eigen::Dynamic>& points);
    ~KdTree();

    KdTree(const KdTree&) = delete;
    KdTree& operator=(const KdTree&) = delete;
    KdTree(KdTree&&) noexcept;
    KdTree& operator=(KdTree&&) noexcept;

    /// Number of points in the tree.
    Eigen::Index size() const;

    /// Find the single nearest neighbour to query.
    NNResult nearest(const geometry::Vec3& query) const;

    /// Find k nearest neighbours. Returns indices and squared distances,
    /// sorted by distance (ascending).
    void knn(const geometry::Vec3& query, int k,
             std::vector<int>& indices,
             std::vector<double>& distances_sq) const;

    /// Find all neighbours within radius. Returns count.
    int radius_search(const geometry::Vec3& query, double radius,
                      std::vector<int>& indices,
                      std::vector<double>& distances_sq) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace alignmesh::spatial
