#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <memory>
#include <string>
#include <vector>

namespace alignmesh::cad {

// ============================================================================
// Analytic surface types for true-surface deviation (CAD tier)
// ============================================================================
//
// Replaces tessellated-mesh closest-point for tight-tolerance tiers.
// Each surface provides exact signed closest-point computation,
// removing tessellation discretization bias.
//
// NOTE: Full STEP/BREP ingestion requires OpenCASCADE (OCCT, LGPL-2.1).
// OCCT is recorded in THIRD_PARTY.md as a planned dependency awaiting
// installation. These analytic surface types are the integration point.
// ============================================================================

/// Result of a closest-point-on-surface query.
struct SurfaceQueryResult {
    geometry::Vec3 closest_point = geometry::Vec3::Zero();
    double distance = 0;           // unsigned
    double signed_distance = 0;    // positive = outside, negative = inside
    geometry::Vec3 surface_normal = geometry::Vec3::Zero();
    double u = 0, v = 0;          // parametric coordinates
    bool converged = true;         // for iterative methods
};

/// Base class for analytic surfaces.
class AnalyticSurface {
public:
    virtual ~AnalyticSurface() = default;
    virtual std::string type_name() const = 0;
    virtual SurfaceQueryResult closest_point(const geometry::Vec3& query) const = 0;
    virtual geometry::Vec3 evaluate(double u, double v) const = 0;
    virtual geometry::Vec3 normal_at(double u, double v) const = 0;
};

/// Infinite plane: n·(p - origin) = 0.
class PlaneS : public AnalyticSurface {
public:
    PlaneS(const geometry::Vec3& origin, const geometry::Vec3& normal);
    std::string type_name() const override { return "plane"; }
    SurfaceQueryResult closest_point(const geometry::Vec3& query) const override;
    geometry::Vec3 evaluate(double u, double v) const override;
    geometry::Vec3 normal_at(double u, double v) const override;

private:
    geometry::Vec3 origin_, normal_, u_axis_, v_axis_;
};

/// Cylinder: axis + radius. Infinite along axis.
class CylinderS : public AnalyticSurface {
public:
    CylinderS(const geometry::Vec3& axis_origin,
              const geometry::Vec3& axis_dir, double radius);
    std::string type_name() const override { return "cylinder"; }
    SurfaceQueryResult closest_point(const geometry::Vec3& query) const override;
    geometry::Vec3 evaluate(double u, double v) const override;
    geometry::Vec3 normal_at(double u, double v) const override;

private:
    geometry::Vec3 origin_, axis_, u_axis_, v_axis_;
    double radius_;
};

/// Sphere: centre + radius.
class SphereS : public AnalyticSurface {
public:
    SphereS(const geometry::Vec3& centre, double radius);
    std::string type_name() const override { return "sphere"; }
    SurfaceQueryResult closest_point(const geometry::Vec3& query) const override;
    geometry::Vec3 evaluate(double u, double v) const override;
    geometry::Vec3 normal_at(double u, double v) const override;

private:
    geometry::Vec3 centre_;
    double radius_;
};

/// Compute true-surface deviation for a set of query points against
/// an analytic surface. Returns per-point signed distances.
struct TrueSurfaceDeviation {
    std::vector<SurfaceQueryResult> per_point;
    double rms = 0;
    double max_abs = 0;
    double mean_signed = 0;
};

TrueSurfaceDeviation compute_true_surface_deviation(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const AnalyticSurface& surface);

/// Read basic STEP file header information (without full BREP parsing).
struct StepFileInfo {
    std::string filename;
    std::string description;
    std::string schema;
    std::string originating_system;
    std::string timestamp;
    std::size_t file_size = 0;
    int entity_count = 0;
    bool has_pmi = false;
    std::vector<std::string> warnings;
};

StepFileInfo read_step_header(const std::string& path);

} // namespace alignmesh::cad
