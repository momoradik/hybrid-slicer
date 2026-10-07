#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <memory>
#include <string>

namespace alignmesh::geometry {

// ============================================================================
// ReferenceGeometry — abstraction over reference surfaces
// ============================================================================
//
// Consumed by the deviation engine and RPS projection. Decouples the
// conformance path from the specific reference representation (triangle
// mesh vs. CAD B-rep).
//
// Implementations:
//   MeshReference  — BVH closest-triangle + facet normal (existing mesh tier)
//   CadReference   — BVH-localized candidate face + analytic refine to the
//                    true trimmed B-rep face (CAD/STEP tier)
//
// The conformance path uses closest_point_on_surface() for deviation
// and normal_at() for RPS nominal normals. Display tessellation is
// separate (never feeds analysis).
// ============================================================================

/// Status of a surface query.
enum class SurfaceQueryStatus {
    OK,                 ///< Query succeeded.
    DEGENERATE_NORMAL,  ///< Closest point found but normal is unreliable.
    NO_SURFACE,         ///< No valid surface at this location.
    NOT_CONVERGED,      ///< Iterative refinement did not converge (CAD tier).
    OUT_OF_RANGE,       ///< Query point too far from any surface.
};

/// Result of a closest-point-on-surface query.
struct SurfacePointResult {
    Vec3 point = Vec3::Zero();          ///< Closest point on the surface.
    Vec3 unit_normal = Vec3::Zero();    ///< Outward surface normal at closest point.
    double signed_distance = 0;         ///< Positive = outside, negative = inside.
    double unsigned_distance = 0;       ///< Absolute distance.
    SurfaceQueryStatus status = SurfaceQueryStatus::OK;
    int face_index = -1;                ///< Face/triangle index (implementation-specific).
};

/// Result of a normal-at query.
struct NormalResult {
    Vec3 unit_normal = Vec3::Zero();    ///< Surface normal at the query point.
    SurfaceQueryStatus status = SurfaceQueryStatus::OK;
};

/// Abstract reference geometry consumed by the conformance path.
class ReferenceGeometry {
public:
    virtual ~ReferenceGeometry() = default;

    /// Find the closest point on the reference surface to query point p.
    /// Returns the closest point, outward normal, signed/unsigned distance,
    /// and query status.
    virtual SurfacePointResult closest_point_on_surface(const Vec3& p) const = 0;

    /// Compute the true surface normal at (or nearest to) a nominal point.
    /// For mesh: facet normal of the closest triangle.
    /// For CAD: analytic normal from the B-rep face.
    virtual NormalResult normal_at(const Vec3& nominal_point) const = 0;

    /// Human-readable type name (e.g., "mesh", "cad-brep").
    virtual std::string type_name() const = 0;
};

// Forward declaration — TriangleMesh defined in triangle_mesh.h.
class TriangleMesh;

/// Create a MeshReference wrapping a TriangleMesh + AABBTree.
/// The mesh must outlive the returned object.
std::unique_ptr<ReferenceGeometry> make_mesh_reference(const TriangleMesh& mesh);

} // namespace alignmesh::geometry
