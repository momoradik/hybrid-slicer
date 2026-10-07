#pragma once

#include "alignmesh/geometry/triangle_mesh.h"

#include <string>
#include <utility>
#include <vector>

namespace alignmesh::io {

/// Validation report — flags issues without repairing.
/// Repair that alters measured geometry is forbidden per CLAUDE.md invariant 3.
struct ValidationReport {
    bool is_valid = true;

    bool is_empty = false;
    bool has_nan_or_inf = false;

    /// Indices of triangles with near-zero area.
    std::vector<int> degenerate_triangles;

    /// Pairs of vertex indices that are coincident (within tolerance).
    std::vector<std::pair<int, int>> duplicate_vertices;

    /// Edges shared by 3+ triangles (non-manifold). Each pair is sorted (v_lo, v_hi).
    std::vector<std::pair<int, int>> non_manifold_edges;

    /// Edges shared by exactly 1 triangle (holes/boundary).
    std::vector<std::pair<int, int>> boundary_edges;

    /// Triangle indices where winding is inconsistent with a neighbor.
    std::vector<int> inconsistent_winding;

    /// Human-readable summary lines.
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Validate a mesh. Detects issues but never repairs.
/// degenerate_area_tol: triangles with area below this are flagged.
/// duplicate_vertex_tol: vertices closer than this are flagged as duplicates.
ValidationReport validate_mesh(const geometry::TriangleMesh& mesh,
                                double degenerate_area_tol = 1e-15,
                                double duplicate_vertex_tol = 1e-12);

} // namespace alignmesh::io
