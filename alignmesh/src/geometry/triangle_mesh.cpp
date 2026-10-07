#include "alignmesh/geometry/triangle_mesh.h"

#include <algorithm>
#include <map>
#include <utility>

namespace alignmesh::geometry {

TriangleMesh::TriangleMesh(
        Eigen::Matrix<double, 3, Eigen::Dynamic> vertices,
        TriMatrix triangles,
        std::string source_hash)
    : vertices_(std::move(vertices)),
      triangles_(std::move(triangles)),
      source_hash_(std::move(source_hash)) {
    build_adjacency();
}

TriangleMesh::TriangleMesh(
        Eigen::Matrix<double, 3, Eigen::Dynamic> vertices,
        TriMatrix triangles,
        std::vector<std::vector<int>> vertex_adj,
        TriMatrix triangle_adj)
    : vertices_(std::move(vertices)),
      triangles_(std::move(triangles)),
      vertex_adj_(std::move(vertex_adj)),
      triangle_adj_(std::move(triangle_adj)) {}

void TriangleMesh::build_adjacency() {
    const auto nv = vertices_.cols();
    const auto nf = triangles_.cols();

    // ---- vertex -> triangle adjacency ----
    vertex_adj_.clear();
    vertex_adj_.resize(static_cast<std::size_t>(nv));
    for (int f = 0; f < static_cast<int>(nf); ++f) {
        for (int j = 0; j < 3; ++j) {
            auto v = static_cast<std::size_t>(triangles_(j, f));
            vertex_adj_[v].push_back(f);
        }
    }

    // ---- triangle -> triangle adjacency ----
    // Map each directed edge (sorted vertex pair) to the faces containing it.
    using Edge = std::pair<int, int>;
    std::map<Edge, std::vector<int>> edge_faces;

    for (int f = 0; f < static_cast<int>(nf); ++f) {
        for (int j = 0; j < 3; ++j) {
            int v0 = triangles_(j, f);
            int v1 = triangles_((j + 1) % 3, f);
            Edge e{std::min(v0, v1), std::max(v0, v1)};
            edge_faces[e].push_back(f);
        }
    }

    triangle_adj_.resize(3, nf);
    triangle_adj_.setConstant(-1);

    for (int f = 0; f < static_cast<int>(nf); ++f) {
        for (int j = 0; j < 3; ++j) {
            int v0 = triangles_(j, f);
            int v1 = triangles_((j + 1) % 3, f);
            Edge e{std::min(v0, v1), std::max(v0, v1)};

            for (int neighbor : edge_faces[e]) {
                if (neighbor != f) {
                    triangle_adj_(j, f) = neighbor;
                    break;
                }
            }
        }
    }
}

TriangleMesh TriangleMesh::transformed(const RigidTransform& T) const {
    auto new_verts = T.apply_cloud(vertices_);
    // Topology is unchanged; reuse adjacency.  Not a source (empty hash).
    return TriangleMesh(std::move(new_verts), triangles_,
                        vertex_adj_, triangle_adj_);
}

} // namespace alignmesh::geometry
