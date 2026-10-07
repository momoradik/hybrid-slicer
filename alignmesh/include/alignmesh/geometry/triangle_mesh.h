#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::geometry {

// ============================================================================
// TriangleMesh -- double-precision triangle mesh with adjacency
// ============================================================================
//
// Immutable-source-aware: same semantics as PointCloud.
//
// Storage:
//   vertices  : 3xV matrix (each column is a vertex position)
//   triangles : 3xF matrix (each column is 3 vertex indices)
//
// Adjacency (computed at construction, topology-only):
//   vertex_adjacency[v]     : triangle indices containing vertex v
//   triangle_adjacency(j,f) : triangle sharing edge j of triangle f,
//                              or -1 for boundary.
//     Edge j connects vertices triangles(j,f) and triangles((j+1)%3, f).
// ============================================================================

class TriangleMesh {
public:
    using TriMatrix = Eigen::Matrix<int, 3, Eigen::Dynamic>;

    /// Construct from vertices and triangles. Adjacency is built immediately.
    TriangleMesh(Eigen::Matrix<double, 3, Eigen::Dynamic> vertices,
                 TriMatrix triangles,
                 std::string source_hash = {});

    Eigen::Index num_vertices() const { return vertices_.cols(); }
    Eigen::Index num_triangles() const { return triangles_.cols(); }

    const Eigen::Matrix<double, 3, Eigen::Dynamic>& vertices() const { return vertices_; }
    const TriMatrix& triangles() const { return triangles_; }

    const std::vector<std::vector<int>>& vertex_adjacency() const { return vertex_adj_; }
    const TriMatrix& triangle_adjacency() const { return triangle_adj_; }

    bool is_source() const { return !source_hash_.empty(); }
    const std::string& source_hash() const { return source_hash_; }

    /// Apply rigid transform to vertices. Returns a NEW mesh (topology
    /// preserved, adjacency reused). Not a source.
    TriangleMesh transformed(const RigidTransform& T) const;

private:
    Eigen::Matrix<double, 3, Eigen::Dynamic> vertices_;
    TriMatrix triangles_;
    std::string source_hash_;

    std::vector<std::vector<int>> vertex_adj_;
    TriMatrix triangle_adj_;

    void build_adjacency();

    /// Private: construct with pre-built adjacency (avoids recomputation
    /// in transformed()).
    TriangleMesh(Eigen::Matrix<double, 3, Eigen::Dynamic> vertices,
                 TriMatrix triangles,
                 std::vector<std::vector<int>> vertex_adj,
                 TriMatrix triangle_adj);
};

} // namespace alignmesh::geometry
