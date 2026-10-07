#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// RPS Point Projection — Robust Local Quadric Surface Fit
// ============================================================================
//
// After pre-alignment (coarse-to-fine), reference datum points must be
// projected onto the measured surface to find their correspondences.
//
// Method (Khameneifar & Feng, CAD 2017 — balanced neighborhood for local
// quadric surface fitting):
//
//   1. Collect measured points within search_radius of p_nom.
//   2. Validate balanced neighborhood: query must lie within the convex
//      hull projection of the neighborhood on the local tangent plane.
//      Extrapolation from an edge-of-patch position → INSUFFICIENT_DATA.
//   3. Fit a robust local quadric surface to the neighborhood:
//      - Primary:  second-order polynomial (z = a₀+a₁x+a₂y+a₃x²+a₄xy+a₅y²)
//        in a PCA-aligned local frame. This is the inspection-domain
//        standard for accurate corresponding-point computation on scan data.
//      - Fallback: PCA / total-least-squares tangent plane (first-order).
//      - Robust IRLS weighting (Huber) to reject outliers/flyers.
//   4. Project p_nom along the nominal normal onto the fitted surface.
//   5. Deviation = signed component of (measured − nominal) along the normal.
//
// NOT used: RIMLS / APSS / jet-fitting (graphics reconstruction operators,
// fragile on noisy scan data — Öztireli 2009, Cazals–Pouget, Lachaud 2023).
// NOT used: nearest-neighbor / closest-point-on-mesh (tangential drift).
//
// MANDATORY FAILURE MODES (surfaced explicitly, never silently defaulted):
//   INSUFFICIENT_DATA      — too few neighbors, hole, or unbalanced patch
//   AMBIGUOUS_PATCH        — local fit residual exceeds threshold
//   GRAZING                — nominal normal near-tangent to fitted surface
//   MULTIPLE_INTERSECTION  — normal line meets local surface at >1 point
//   NO_NORMAL              — nominal normal undefined or degenerate
// ============================================================================

/// Projection status per §4 mandatory failure modes.
/// Any non-OK status excludes the point from alignment and propagates
/// to the DOF report. Never silently default.
enum class ProjectionStatus {
    OK,                     ///< Projection succeeded.
    INSUFFICIENT_DATA,      ///< Patch has too few points or a hole at p_nom.
    AMBIGUOUS_PATCH,        ///< Local fit residual exceeds threshold (edge/crease).
    GRAZING,                ///< Nominal normal nearly tangent to local surface.
    MULTIPLE_INTERSECTION,  ///< Multiple ray-surface intersections.
    NO_NORMAL,              ///< Nominal normal undefined (point not on a surface).
};

/// Quality of a single projected point.
struct ProjectionQuality {
    ProjectionStatus status = ProjectionStatus::OK;
    double distance = 0;           ///< Distance from reference point to projected point (mm).
    double normal_angle_deg = 0;   ///< Angle between ref normal and fitted surface normal (deg).
    double patch_roughness = 0;    ///< Local surface fit residual / RMS (mm).
    double search_radius = 0;      ///< Radius used for neighborhood search (mm).
    int neighborhood_size = 0;     ///< Number of scan points in the search radius.
    std::string warning;           ///< Explanation if status != OK.
};

/// Result of projecting a single reference point onto the measured surface.
struct ProjectedPoint {
    Eigen::Vector3d ref_point;       ///< Original reference point.
    Eigen::Vector3d projected_point; ///< Point on measured surface (from local surface fit).
    Eigen::Vector3d ref_normal;      ///< Surface normal at reference point (from reference geometry).
    ProjectionQuality quality;
};

/// Result of projecting all datum points.
struct RPSProjectionResult {
    bool success = false;
    std::vector<ProjectedPoint> projections;
    int num_ok = 0;           ///< Points with status == OK.
    int num_failed = 0;       ///< Points with status != OK.
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Settings for RPS projection.
struct RPSProjectionSettings {
    /// Search radius for neighborhood collection (mm).
    /// 0 = auto-compute from median nearest-neighbor distance in measured
    /// data, multiplied by search_radius_factor.
    double search_radius = 0;

    /// Factor applied to median NN distance for auto search_radius.
    double search_radius_factor = 5.0;

    /// Minimum neighbor count for a valid local surface fit.
    /// Below this → INSUFFICIENT_DATA.
    int min_neighbors = 3;

    /// Minimum neighbor count for second-order (quadric) fit (6 parameters).
    /// Below this (but >= min_neighbors) → first-order tangent-plane fallback.
    int min_neighbors_quadric = 10;

    /// Maximum projection distance along nominal normal (mm).
    /// If the projected point is farther than this from p_nom → AMBIGUOUS_PATCH.
    /// Also: if the nearest measured point is farther than this, the query
    /// is over a hole → INSUFFICIENT_DATA.
    double max_projection_distance = 5.0;

    /// Maximum local fit residual / roughness (mm).
    /// Above this → AMBIGUOUS_PATCH. Indicates the patch straddles an
    /// edge, crease, or high-curvature region that a smooth fit cannot capture.
    double max_roughness = 0.5;

    /// Minimum |cos(angle)| between nominal normal and fitted surface normal.
    /// Below this → GRAZING. cos(80°) ≈ 0.174.
    double min_cos_grazing = 0.174;

    /// Huber threshold for robust IRLS weighting (multiples of fit sigma).
    /// Points with residuals beyond huber_k * sigma are down-weighted.
    double huber_k = 1.345;

    /// IRLS iterations for robust surface fit.
    int robust_iterations = 3;
};

/// Project reference datum points onto the measured surface (mesh input).
///
/// @param ref_points     Reference datum point positions (3xN).
/// @param ref_mesh       Reference mesh (for computing nominal surface normals).
/// @param meas_mesh      Measured mesh (target surface for projection).
/// @param pre_alignment  Pre-alignment transform (measured→reference frame).
/// @param settings       Projection parameters.
///
/// The pre_alignment transform brings measured into reference frame.
/// Projection finds where each reference point corresponds on the
/// (pre-aligned) measured surface via local surface fitting.
RPSProjectionResult project_rps_points(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
    const geometry::TriangleMesh& ref_mesh,
    const geometry::TriangleMesh& meas_mesh,
    const geometry::RigidTransform& pre_alignment,
    const RPSProjectionSettings& settings = {});

/// Project reference datum points onto the measured surface (point cloud input).
///
/// Same algorithm as mesh overload: MLS/PCA local surface fitting on
/// the measured points. No closest-point fallback.
RPSProjectionResult project_rps_points(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
    const geometry::TriangleMesh& ref_mesh,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& meas_points,
    const geometry::RigidTransform& pre_alignment,
    const RPSProjectionSettings& settings = {});

/// Project reference datum points using ReferenceGeometry for nominal normals.
///
/// This overload replaces the brute-force closest-triangle normal search
/// with the ReferenceGeometry abstraction. For MeshReference this is
/// equivalent to the TriangleMesh overload. For CadReference, the nominal
/// normal is the true analytic surface normal from the B-rep face.
///
/// @param ref_points     Reference datum point positions (3xN).
/// @param ref_geom       Reference geometry (mesh or CAD B-rep) for normals.
/// @param meas_mesh      Measured mesh (target surface for projection).
/// @param pre_alignment  Pre-alignment transform (measured->reference frame).
/// @param settings       Projection parameters.
RPSProjectionResult project_rps_points(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
    const geometry::ReferenceGeometry& ref_geom,
    const geometry::TriangleMesh& meas_mesh,
    const geometry::RigidTransform& pre_alignment,
    const RPSProjectionSettings& settings = {});

/// Same as above but with measured point cloud input.
RPSProjectionResult project_rps_points(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
    const geometry::ReferenceGeometry& ref_geom,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& meas_points,
    const geometry::RigidTransform& pre_alignment,
    const RPSProjectionSettings& settings = {});

} // namespace alignmesh::registration
