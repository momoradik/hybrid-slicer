#include "alignmesh/io/mesh_validation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

namespace alignmesh::io {

ValidationReport validate_mesh(const geometry::TriangleMesh& mesh,
                                double degenerate_area_tol,
                                double duplicate_vertex_tol) {
    ValidationReport report;
    const auto& verts = mesh.vertices();
    const auto& tris  = mesh.triangles();
    auto nv = mesh.num_vertices();
    auto nf = mesh.num_triangles();

    // ---- empty mesh ----
    if (nf == 0) {
        report.is_empty = true;
        report.is_valid = false;
        report.errors.push_back("Mesh is empty (0 triangles)");
        return report;
    }

    // ---- NaN / Inf check ----
    for (Eigen::Index i = 0; i < nv; ++i) {
        for (int c = 0; c < 3; ++c) {
            double val = verts(c, i);
            if (std::isnan(val) || std::isinf(val)) {
                report.has_nan_or_inf = true;
                report.is_valid = false;
                report.errors.push_back("Vertex " + std::to_string(i) +
                    " has NaN or Inf coordinate");
                break;
            }
        }
    }

    // ---- degenerate triangles ----
    for (Eigen::Index f = 0; f < nf; ++f) {
        geometry::Vec3 v0 = verts.col(tris(0, f));
        geometry::Vec3 v1 = verts.col(tris(1, f));
        geometry::Vec3 v2 = verts.col(tris(2, f));
        double area = 0.5 * (v1 - v0).cross(v2 - v0).norm();
        if (area < degenerate_area_tol) {
            report.degenerate_triangles.push_back(static_cast<int>(f));
        }
    }
    if (!report.degenerate_triangles.empty()) {
        report.is_valid = false;
        report.warnings.push_back(std::to_string(report.degenerate_triangles.size()) +
            " degenerate triangle(s) with near-zero area");
    }

    // ---- duplicate vertices ----
    {
        std::vector<Eigen::Index> idx(static_cast<std::size_t>(nv));
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](Eigen::Index a, Eigen::Index b) {
            if (verts(0, a) != verts(0, b)) return verts(0, a) < verts(0, b);
            if (verts(1, a) != verts(1, b)) return verts(1, a) < verts(1, b);
            return verts(2, a) < verts(2, b);
        });
        for (std::size_t i = 1; i < idx.size(); ++i) {
            if ((verts.col(idx[i]) - verts.col(idx[i - 1])).norm() < duplicate_vertex_tol) {
                report.duplicate_vertices.emplace_back(
                    static_cast<int>(std::min(idx[i], idx[i - 1])),
                    static_cast<int>(std::max(idx[i], idx[i - 1])));
            }
        }
        if (!report.duplicate_vertices.empty()) {
            report.warnings.push_back(std::to_string(report.duplicate_vertices.size()) +
                " pair(s) of duplicate/near-coincident vertices");
        }
    }

    // ---- edge analysis (non-manifold, boundary, winding) ----
    using Edge = std::pair<int, int>;

    // For each directed half-edge, record which triangle it belongs to.
    // A properly oriented manifold mesh has each undirected edge appearing
    // exactly twice with opposite orientations.
    struct HalfEdgeInfo {
        int triangle;
        int v0, v1;  // directed
    };

    // Map undirected edge -> list of directed half-edges
    std::map<Edge, std::vector<HalfEdgeInfo>> edge_map;

    for (Eigen::Index f = 0; f < nf; ++f) {
        for (int j = 0; j < 3; ++j) {
            int va = tris(j, f);
            int vb = tris((j + 1) % 3, f);
            Edge ue{std::min(va, vb), std::max(va, vb)};
            edge_map[ue].push_back({static_cast<int>(f), va, vb});
        }
    }

    std::set<int> inconsistent_set;

    for (auto& [edge, halfedges] : edge_map) {
        if (halfedges.size() == 1) {
            report.boundary_edges.push_back(edge);
        } else if (halfedges.size() > 2) {
            report.non_manifold_edges.push_back(edge);
        }
        // Winding check: two half-edges sharing an undirected edge should
        // traverse it in opposite directions. If both go (a,b) or both go
        // (b,a), winding is inconsistent.
        if (halfedges.size() == 2) {
            bool same_dir = (halfedges[0].v0 == halfedges[1].v0 &&
                             halfedges[0].v1 == halfedges[1].v1);
            if (same_dir) {
                inconsistent_set.insert(halfedges[0].triangle);
                inconsistent_set.insert(halfedges[1].triangle);
            }
        }
    }

    report.inconsistent_winding.assign(inconsistent_set.begin(), inconsistent_set.end());

    if (!report.non_manifold_edges.empty()) {
        report.is_valid = false;
        report.errors.push_back(std::to_string(report.non_manifold_edges.size()) +
            " non-manifold edge(s)");
    }
    if (!report.boundary_edges.empty()) {
        report.warnings.push_back(std::to_string(report.boundary_edges.size()) +
            " boundary edge(s) (holes)");
    }
    if (!report.inconsistent_winding.empty()) {
        report.is_valid = false;
        report.errors.push_back(std::to_string(report.inconsistent_winding.size()) +
            " triangle(s) with inconsistent winding");
    }

    return report;
}

} // namespace alignmesh::io
