#pragma once

#include "alignmesh/geometry/triangle_mesh.h"

#include <Eigen/Core>
#include <memory>
#include <vector>

namespace alignmesh::spatial {

/// Result of a closest-point-on-surface query.
struct SurfaceNNResult {
    Eigen::Vector3d closest_point = Eigen::Vector3d::Zero();
    double distance_sq = 0;         ///< Squared Euclidean distance.
    int triangle_index = -1;        ///< Index of the closest triangle.
};

// ============================================================================
// AABBTree — axis-aligned bounding box tree over triangle mesh
// ============================================================================
//
// Precomputed spatial index for exact point-to-surface (closest triangle)
// queries.  Builds once in O(N log N), queries in O(log N).
//
// Properties:
//   - EXACT: the prune uses a correct point-to-box lower bound, so the true
//     closest triangle is never discarded.
//   - DETERMINISTIC: fixed build order (median split on longest axis) and
//     fixed traversal order (always visit nearer child first) produce
//     identical results across runs.
//   - DOUBLE PRECISION throughout.
//
// Reference: Ericson, "Real-Time Collision Detection", Ch. 6.
// ============================================================================

class AABBTree {
public:
    /// Build from a triangle mesh. The mesh must outlive this tree.
    explicit AABBTree(const geometry::TriangleMesh& mesh);
    ~AABBTree();

    AABBTree(const AABBTree&) = delete;
    AABBTree& operator=(const AABBTree&) = delete;
    AABBTree(AABBTree&&) noexcept;
    AABBTree& operator=(AABBTree&&) noexcept;

    /// Find the closest point on the mesh surface to the query point.
    /// Returns the closest point, squared distance, and triangle index.
    SurfaceNNResult closest_point(const Eigen::Vector3d& query) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace alignmesh::spatial
