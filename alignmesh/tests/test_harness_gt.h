#pragma once
// ============================================================================
// Synthetic ground-truth test harness
//
// Reusable infrastructure for every later self-test. Provides:
//   - apply_known_transform:  SE(3)-transform geometry, deterministic.
//   - make_synthetic_scan:    independently resampled, noisy, different-topology
//                             point cloud from a reference mesh.
//   - Curved test geometries: L-bracket-with-fillet, cylinder, sphere,
//                             NURBS-like patch, chamfered block.
//   - full_rank6_rps_config:  6-DOF spanning RPS constraint set.
//   - under_constrained_rps_config: rank-5 (negative tests).
//   - Fixed-seed RNG for bit-reproducibility.
//
// INVARIANTS:
//   Double precision throughout. Deterministic (SeededRng, fixed-order ops).
//   Synthetic scan has INDEPENDENT topology from reference (different vertex
//   count, triangulation, density). Never reuses reference vertices.
// ============================================================================

#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <Eigen/Core>
#include <cstdint>
#include <vector>

namespace alignmesh::test_harness {

// ── Transform helpers ────────────────────────────────────────────────────

/// Apply a known SE(3) transform to a mesh. Returns a new mesh.
geometry::TriangleMesh apply_known_transform(
    const geometry::TriangleMesh& mesh,
    const geometry::RigidTransform& T);

/// Apply a known SE(3) transform to a point cloud (3xN matrix).
Eigen::Matrix<double, 3, Eigen::Dynamic> apply_known_transform(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const geometry::RigidTransform& T);

// ── Synthetic scan generation ────────────────────────────────────────────

struct ScanParams {
    double noise_sigma       = 0.01;   // mm, Gaussian noise sigma per axis
    double outlier_fraction  = 0.0;    // fraction of points replaced with flyers
    double outlier_range     = 50.0;   // mm, range of random flyer displacement
    double resample_density  = 2.0;    // points per triangle (approximate)
    std::uint64_t seed       = 42;     // RNG seed
};

struct SyntheticScan {
    Eigen::Matrix<double, 3, Eigen::Dynamic> points;  // in the raw measured frame
    int n_inliers  = 0;
    int n_outliers = 0;
};

/// Generate an independently resampled point set from a reference mesh,
/// expressed in the raw measured frame (i.e., T_gt^{-1} applied).
///
/// The scan has DIFFERENT topology/density from the reference:
///   - Points are sampled uniformly on triangle surfaces (barycentric),
///     NOT at reference vertices.
///   - Correlated Gaussian noise is added per-point.
///   - A fraction of points are replaced with random flyers.
///   - The result is in the raw measured frame (pre-alignment not applied).
///
/// T_gt: the ground-truth transform from measured frame to reference frame.
///        i.e., ref_point = T_gt * meas_point.
SyntheticScan make_synthetic_scan(
    const geometry::TriangleMesh& reference,
    const geometry::RigidTransform& T_gt,
    const ScanParams& params = {});

// ── Test geometries ──────────────────────────────────────────────────────

/// L-bracket with fillet: two orthogonal planar walls + a fillet at the
/// junction. Provides planar + cylindrical surfaces, <20 faces.
/// Origin at the inner corner, extends along +X and +Y, thickness along +Z.
geometry::TriangleMesh make_l_bracket_with_fillet(
    double arm_length = 60.0, double arm_width = 20.0,
    double thickness = 10.0, double fillet_radius = 5.0,
    int angular_segments = 12, int linear_segments = 4);

/// Closed cylinder along Z. Planar caps + cylindrical barrel.
geometry::TriangleMesh make_cylinder(
    double radius = 20.0, double height = 40.0,
    int radial_segments = 32, int height_segments = 8);

/// UV-sphere with planar-cap poles (avoids degenerate triangles at poles).
geometry::TriangleMesh make_sphere(
    double radius = 25.0, int u_segments = 24, int v_segments = 16);

/// NURBS-like curved patch: a bilinear-interpolated surface with gentle
/// curvature (sinusoidal displacement). Single-face, trimmed rectangle.
geometry::TriangleMesh make_curved_patch(
    double width = 40.0, double depth = 30.0,
    double amplitude = 5.0, int u_segments = 20, int v_segments = 15);

/// Axis-aligned box (6 planar faces, 12 triangles).
geometry::TriangleMesh make_box(
    double sx = 50.0, double sy = 30.0, double sz = 20.0);

// ── RPS configurations ───────────────────────────────────────────────────

struct RPSConfig {
    std::vector<registration::RPSConstraintPoint> constraints;
    double bilateral_tolerance = 0.1;  // mm
};

/// Generate an RPS configuration whose locked directions span all 6 rigid
/// DOF (rank-6 constraint Jacobian). Points are placed on the geometry's
/// bounding box faces with surface-normal locks.
RPSConfig full_rank6_rps_config(const geometry::TriangleMesh& geometry);

/// Generate a rank-5 configuration (one DOF unconstrained) for negative tests.
RPSConfig under_constrained_rps_config(const geometry::TriangleMesh& geometry);

// ── Ground-truth transform factory ───────────────────────────────────────

/// A "realistic" ground-truth rigid transform: small rotation + translation,
/// typical of a fixtured part that's slightly misaligned.
geometry::RigidTransform make_known_transform(
    double rot_x_deg = 0.3, double rot_y_deg = -0.5, double rot_z_deg = 0.2,
    double tx = 0.15, double ty = -0.08, double tz = 0.04);

} // namespace alignmesh::test_harness
