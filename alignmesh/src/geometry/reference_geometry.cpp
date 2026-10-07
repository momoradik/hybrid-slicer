#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/spatial/aabb_tree.h"
#include "alignmesh/analysis/deviation.h"

#include <cmath>

namespace alignmesh::geometry {

// ============================================================================
// MeshReference — ReferenceGeometry backed by a TriangleMesh + AABBTree
// ============================================================================
//
// The existing mesh tier. BVH closest-triangle for distance, facet normal
// for sign and RPS nominal direction. This is what compute_deviation()
// and project_rps_points() were hardcoded to do; now it's behind the
// abstraction so CadReference can drop in.
// ============================================================================

class MeshReference : public ReferenceGeometry {
public:
    /// Construct from a TriangleMesh. The mesh must outlive this object.
    explicit MeshReference(const TriangleMesh& mesh)
        : mesh_(mesh), tree_(mesh) {}

    SurfacePointResult closest_point_on_surface(const Vec3& p) const override {
        SurfacePointResult r;

        if (mesh_.num_triangles() == 0) {
            r.status = SurfaceQueryStatus::NO_SURFACE;
            return r;
        }

        auto hit = tree_.closest_point(p);
        r.point = hit.closest_point;
        r.unsigned_distance = std::sqrt(hit.distance_sq);
        r.face_index = hit.triangle_index;

        // Signed distance from triangle facet normal.
        if (hit.triangle_index >= 0) {
            const auto& V = mesh_.vertices();
            const auto& F = mesh_.triangles();
            Vec3 v0 = V.col(F(0, hit.triangle_index));
            Vec3 v1 = V.col(F(1, hit.triangle_index));
            Vec3 v2 = V.col(F(2, hit.triangle_index));
            Vec3 tri_normal = (v1 - v0).cross(v2 - v0);
            double nlen = tri_normal.norm();
            if (nlen > 1e-15) {
                tri_normal /= nlen;
                r.unit_normal = tri_normal;
                double dot = tri_normal.dot(p - hit.closest_point);
                r.signed_distance = dot;
                r.status = SurfaceQueryStatus::OK;
            } else {
                r.signed_distance = r.unsigned_distance;
                r.status = SurfaceQueryStatus::DEGENERATE_NORMAL;
            }
        } else {
            r.status = SurfaceQueryStatus::NO_SURFACE;
        }

        return r;
    }

    NormalResult normal_at(const Vec3& nominal_point) const override {
        NormalResult r;

        if (mesh_.num_triangles() == 0) {
            r.status = SurfaceQueryStatus::NO_SURFACE;
            return r;
        }

        auto hit = tree_.closest_point(nominal_point);
        if (hit.triangle_index >= 0) {
            const auto& V = mesh_.vertices();
            const auto& F = mesh_.triangles();
            Vec3 v0 = V.col(F(0, hit.triangle_index));
            Vec3 v1 = V.col(F(1, hit.triangle_index));
            Vec3 v2 = V.col(F(2, hit.triangle_index));
            Vec3 n = (v1 - v0).cross(v2 - v0);
            double nlen = n.norm();
            if (nlen > 1e-15) {
                r.unit_normal = n / nlen;
                r.status = SurfaceQueryStatus::OK;
            } else {
                r.status = SurfaceQueryStatus::DEGENERATE_NORMAL;
            }
        } else {
            r.status = SurfaceQueryStatus::NO_SURFACE;
        }

        return r;
    }

    std::string type_name() const override { return "mesh"; }

private:
    const TriangleMesh& mesh_;
    spatial::AABBTree tree_;
};

// ============================================================================
// Factory function
// ============================================================================

std::unique_ptr<ReferenceGeometry> make_mesh_reference(const TriangleMesh& mesh) {
    return std::make_unique<MeshReference>(mesh);
}

} // namespace alignmesh::geometry
