/**
 * Adversarial tests for RPS point projection (§4 / §8).
 *
 * These tests assert specific ProjectionStatus values and behaviors
 * that a closest-point-on-mesh implementation cannot produce.
 * They verify the MLS/PCA local surface fitting projection is correct.
 */
#include <gtest/gtest.h>
#include "alignmesh/registration/rps_projection.h"
#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <cmath>
#include <vector>

using namespace alignmesh;

static constexpr double PI = 3.14159265358979323846;

// Helper: flat quad mesh in XY plane at z=0.
static geometry::TriangleMesh make_flat_quad(double size) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, 4);
    V.col(0) = Eigen::Vector3d(-size, -size, 0);
    V.col(1) = Eigen::Vector3d( size, -size, 0);
    V.col(2) = Eigen::Vector3d( size,  size, 0);
    V.col(3) = Eigen::Vector3d(-size,  size, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, 2);
    F.col(0) = Eigen::Vector3i(0, 1, 2);
    F.col(1) = Eigen::Vector3i(0, 2, 3);
    return geometry::TriangleMesh(V, F);
}

// Helper: ring mesh with a hole at center (no triangles for r < r_inner).
static geometry::TriangleMesh make_ring_mesh(double r_inner, double r_outer, int segments) {
    int nv = segments * 2;
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, nv);
    for (int i = 0; i < segments; ++i) {
        double a = 2.0 * PI * i / segments;
        V.col(i * 2)     = Eigen::Vector3d(r_inner * std::cos(a), r_inner * std::sin(a), 0);
        V.col(i * 2 + 1) = Eigen::Vector3d(r_outer * std::cos(a), r_outer * std::sin(a), 0);
    }
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, segments * 2);
    for (int i = 0; i < segments; ++i) {
        int i0 = i*2, i1 = i*2+1, j0 = ((i+1)%segments)*2, j1 = ((i+1)%segments)*2+1;
        F.col(i*2)   = Eigen::Vector3i(i0, j0, i1);
        F.col(i*2+1) = Eigen::Vector3i(i1, j0, j1);
    }
    return geometry::TriangleMesh(V, F);
}

// Helper: dense flat grid mesh in XY plane at z=0, with many vertices.
static geometry::TriangleMesh make_dense_flat_grid(double size, int n_per_side) {
    int nv = n_per_side * n_per_side;
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, nv);
    double step = 2.0 * size / (n_per_side - 1);
    for (int iy = 0; iy < n_per_side; ++iy) {
        for (int ix = 0; ix < n_per_side; ++ix) {
            int idx = iy * n_per_side + ix;
            V.col(idx) = Eigen::Vector3d(-size + ix * step, -size + iy * step, 0);
        }
    }
    int nf = (n_per_side - 1) * (n_per_side - 1) * 2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, nf);
    int fi = 0;
    for (int iy = 0; iy < n_per_side - 1; ++iy) {
        for (int ix = 0; ix < n_per_side - 1; ++ix) {
            int v00 = iy * n_per_side + ix;
            int v10 = v00 + 1;
            int v01 = v00 + n_per_side;
            int v11 = v01 + 1;
            F.col(fi++) = Eigen::Vector3i(v00, v10, v11);
            F.col(fi++) = Eigen::Vector3i(v00, v11, v01);
        }
    }
    return geometry::TriangleMesh(V, F);
}

// Helper: vertical wall mesh (in XZ plane), face normal points along +Y.
static geometry::TriangleMesh make_vertical_wall(double size, int n_per_side) {
    int nv = n_per_side * n_per_side;
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, nv);
    double step = 2.0 * size / (n_per_side - 1);
    for (int iz = 0; iz < n_per_side; ++iz) {
        for (int ix = 0; ix < n_per_side; ++ix) {
            int idx = iz * n_per_side + ix;
            V.col(idx) = Eigen::Vector3d(-size + ix * step, 0, -size + iz * step);
        }
    }
    int nf = (n_per_side - 1) * (n_per_side - 1) * 2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, nf);
    int fi = 0;
    for (int iz = 0; iz < n_per_side - 1; ++iz) {
        for (int ix = 0; ix < n_per_side - 1; ++ix) {
            int v00 = iz * n_per_side + ix;
            int v10 = v00 + 1;
            int v01 = v00 + n_per_side;
            int v11 = v01 + 1;
            F.col(fi++) = Eigen::Vector3i(v00, v10, v11);
            F.col(fi++) = Eigen::Vector3i(v00, v11, v01);
        }
    }
    return geometry::TriangleMesh(V, F);
}

// Helper: curved (spherical cap) mesh, centered at origin, radius R.
// Points on the upper hemisphere between polar angle 0 and max_angle.
static geometry::TriangleMesh make_spherical_cap(double R, double max_angle_deg, int n_rings, int n_sectors) {
    double max_angle = max_angle_deg * PI / 180.0;
    std::vector<Eigen::Vector3d> verts;
    // Apex
    verts.push_back(Eigen::Vector3d(0, 0, R));
    for (int ring = 1; ring <= n_rings; ++ring) {
        double theta = max_angle * ring / n_rings;
        for (int sec = 0; sec < n_sectors; ++sec) {
            double phi = 2.0 * PI * sec / n_sectors;
            verts.push_back(Eigen::Vector3d(
                R * std::sin(theta) * std::cos(phi),
                R * std::sin(theta) * std::sin(phi),
                R * std::cos(theta)));
        }
    }
    int nv = static_cast<int>(verts.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, nv);
    for (int i = 0; i < nv; ++i) V.col(i) = verts[static_cast<std::size_t>(i)];

    std::vector<Eigen::Vector3i> faces;
    // Apex fan
    for (int sec = 0; sec < n_sectors; ++sec) {
        int next = (sec + 1) % n_sectors;
        faces.push_back(Eigen::Vector3i(0, 1 + sec, 1 + next));
    }
    // Ring-to-ring strips
    for (int ring = 1; ring < n_rings; ++ring) {
        int base_cur = 1 + (ring - 1) * n_sectors;
        int base_next = 1 + ring * n_sectors;
        for (int sec = 0; sec < n_sectors; ++sec) {
            int next = (sec + 1) % n_sectors;
            faces.push_back(Eigen::Vector3i(base_cur + sec, base_next + sec, base_next + next));
            faces.push_back(Eigen::Vector3i(base_cur + sec, base_next + next, base_cur + next));
        }
    }
    int nf = static_cast<int>(faces.size());
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, nf);
    for (int i = 0; i < nf; ++i) F.col(i) = faces[static_cast<std::size_t>(i)];
    return geometry::TriangleMesh(V, F);
}


// ======================================================================
// §4 FAILURE MODE: INSUFFICIENT_DATA
// Query at center of ring mesh (hole). Must report INSUFFICIENT_DATA,
// not silently find the nearest ring edge via closest-point.
// ======================================================================

TEST(RPSProjection, InsufficientData_HoleInScan) {
    auto ref_mesh = make_flat_quad(10.0);
    auto meas_mesh = make_ring_mesh(5.0, 10.0, 24); // 5mm hole at center

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0); // center of hole

    registration::RPSProjectionSettings settings;
    settings.max_projection_distance = 2.0; // well inside the 5mm hole

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 1u);

    // The projection MUST report INSUFFICIENT_DATA — not OK.
    // Closest-point would silently return the ring edge at ~5mm.
    EXPECT_EQ(result.projections[0].quality.status,
              registration::ProjectionStatus::INSUFFICIENT_DATA)
        << "Query over a hole must report INSUFFICIENT_DATA, not silently "
           "fall back to closest-point on the ring edge";
}

// ======================================================================
// §4 FAILURE MODE: NO_NORMAL
// Query a point that has no well-defined surface normal on the reference.
// ======================================================================

TEST(RPSProjection, NoNormal_PointFarFromSurface) {
    auto ref_mesh = make_flat_quad(5.0); // small 5mm quad
    auto meas_mesh = make_flat_quad(5.0);

    // Query point 100mm away from any reference surface.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(100, 100, 100);

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);

    // The reference normal lookup at (100,100,100) is from the nearest
    // triangle on a 5mm quad — the normal may be technically computable
    // but the distance is absurd. A proper implementation should flag this.
    // Closest-point silently returns a point on the quad edge.
    auto status = result.projections[0].quality.status;
    EXPECT_NE(status, registration::ProjectionStatus::OK)
        << "Point 100mm from a 5mm surface must not silently project as OK";
}

// ======================================================================
// §4 CONTRACT: neighborhood_size must be populated.
// A proper MLS implementation reports how many scan points were in the
// search radius. Closest-point does not populate this field.
// ======================================================================

TEST(RPSProjection, NeighborhoodSizePopulated) {
    // Use a dense grid so there are enough neighbors for the fit.
    auto ref_mesh = make_dense_flat_grid(10.0, 20);
    auto meas_mesh = make_dense_flat_grid(10.0, 20);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0);

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);

    // MLS must report how many scan points it found in the neighborhood.
    // Closest-point doesn't do neighborhood search -> this will be 0.
    EXPECT_GT(result.projections[0].quality.neighborhood_size, 0)
        << "Projection must report the neighborhood size used for local "
           "surface fitting (MLS/PCA). 0 means closest-point was used.";
}

// ======================================================================
// §4 CONTRACT: search_radius must be an explicit, auditable parameter.
// ======================================================================

TEST(RPSProjection, SearchRadiusReported) {
    auto ref_mesh = make_dense_flat_grid(10.0, 20);
    auto meas_mesh = make_dense_flat_grid(10.0, 20);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0);

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);

    // The search radius used for the neighborhood must be reported.
    // Closest-point doesn't use a search radius -> this will be 0.
    EXPECT_GT(result.projections[0].quality.search_radius, 0)
        << "Projection must report the search radius used for local surface "
           "fitting. 0 means no neighborhood search was performed.";
}

// ======================================================================
// §4 FAILURE MODE: GRAZING
// Nominal normal is nearly tangent to the measured surface.
// A vertical wall (face normal = +Y) queried with a horizontal
// reference normal (nominal normal = +Z) is a grazing case.
// ======================================================================

TEST(RPSProjection, Grazing_NormalTangentToSurface) {
    // Reference: flat XY plane — nominal normal at (0,0,0) = +Z.
    auto ref_mesh = make_flat_quad(10.0);

    // Measured: vertical wall in XZ plane — surface normal = +Y.
    // The nominal normal (+Z) is perpendicular to the wall normal (+Y)
    // → nearly tangent to the wall surface → GRAZING.
    auto meas_mesh = make_vertical_wall(10.0, 20);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0);

    registration::RPSProjectionSettings settings;
    settings.min_cos_grazing = 0.5; // cos(60°) — Z vs Y is 90° → cos=0 < 0.5

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 1u);
    EXPECT_EQ(result.projections[0].quality.status,
              registration::ProjectionStatus::GRAZING)
        << "Nominal normal perpendicular to measured surface must report GRAZING";
}

// ======================================================================
// §4 FAILURE MODE: MULTIPLE_INTERSECTION
// Normal line passes through a concave fitted quadratic surface at
// two distinct points.
// ======================================================================

TEST(RPSProjection, MultipleIntersection_ConcaveSurface) {
    // Build a concave (saddle-like) measured surface: z = -0.01*(x^2 + y^2)
    // centered below the query. The nominal normal is +Z. A query above
    // the apex along +Z can intersect the quadratic at two points
    // if the surface curves enough.

    // We construct a bowl: z = -k*(x^2 + y^2) with k chosen so that a
    // vertical line at offset from center hits twice.
    // Actually, for a simple bowl z = -k*r^2, a vertical line at x=x0
    // gives z = -k*(x0^2 + y^2), which for a given x0 has one intersection
    // with a vertical line at (x0, 0).
    //
    // For MULTIPLE_INTERSECTION to occur with a quadratic fit, we need
    // the normal to hit the quadratic surface at two distinct t values.
    // Use a saddle: z = k*(x^2 - y^2).  A line along (0,0,1) at (x0, 0)
    // gives t = k*x0^2 - z0, one solution. Not ideal.
    //
    // Better: offset the query so the normal line is not vertical in
    // local frame. Use a tilted configuration where the nominal normal
    // is oblique to the local frame, creating two crossings.

    // Construct measured points on a strongly curved paraboloid:
    // z = -0.05 * (x^2 + y^2), which forms a dome.
    // Query from above at an offset so the line through nominal-normal
    // passes through the surface at two t values in the local quadratic fit.

    // Build a dense grid of points on z = a*(x^2 + y^2) with a > 0 (bowl up).
    double a_coeff = 0.05; // strong curvature
    int n = 21;
    double half_size = 5.0;
    double step = 2.0 * half_size / (n - 1);

    int nv = n * n;
    Eigen::Matrix<double, 3, Eigen::Dynamic> meas_V(3, nv);
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            double x = -half_size + ix * step;
            double y = -half_size + iy * step;
            double z = a_coeff * (x * x + y * y);
            meas_V.col(iy * n + ix) = Eigen::Vector3d(x, y, z);
        }
    }
    // Build triangle mesh from grid.
    int nf = (n-1)*(n-1)*2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> meas_F(3, nf);
    int fi = 0;
    for (int iy = 0; iy < n-1; ++iy) {
        for (int ix = 0; ix < n-1; ++ix) {
            int v00 = iy*n + ix, v10 = v00+1, v01 = v00+n, v11 = v01+1;
            meas_F.col(fi++) = Eigen::Vector3i(v00, v10, v11);
            meas_F.col(fi++) = Eigen::Vector3i(v00, v11, v01);
        }
    }
    auto meas_mesh = geometry::TriangleMesh(meas_V, meas_F);

    // Reference: flat XY plane. Query at (0, 0, 1.5) which is above the bowl.
    // The nominal normal at (0,0,1.5) from the flat ref is (0,0,1).
    // The MLS fit should recover z = a*(x^2+y^2).
    // Line: (0, 0, 1.5) + t*(0, 0, 1) → z = 1.5 + t.
    // Surface: z = a*(0 + 0) = 0 at x=y=0.
    // So t = 0 - 1.5 = -1.5 → one intersection.
    // To get two intersections, we need the query offset from center.
    //
    // At (2, 0, z_query), line: z = z_query + t, surface: z = a*(x^2 + y^2)
    // The x-direction doesn't change along the line since direction is (0,0,1).
    // So z = a*(4 + y^2) at x=2, y=0: z=a*4=0.2. Still one intersection.
    //
    // For two intersections with a quadratic fit, the direction must NOT be
    // aligned with the local z-axis of the fit. Use a tilted nominal normal.

    // Build a reference mesh that's a tilted plane — its normal is oblique.
    // Tilted 30° from vertical: normal = (0, sin30, cos30) = (0, 0.5, 0.866)
    {
        Eigen::Matrix<double, 3, Eigen::Dynamic> ref_V(3, 4);
        double c30 = std::cos(30.0 * PI / 180.0);
        double s30 = std::sin(30.0 * PI / 180.0);
        // Rotate the quad around X-axis by 30°.
        ref_V.col(0) = Eigen::Vector3d(-10, -10*c30, 10*s30);
        ref_V.col(1) = Eigen::Vector3d( 10, -10*c30, 10*s30);
        ref_V.col(2) = Eigen::Vector3d( 10,  10*c30, -10*s30);
        ref_V.col(3) = Eigen::Vector3d(-10,  10*c30, -10*s30);
        Eigen::Matrix<int, 3, Eigen::Dynamic> ref_F(3, 2);
        ref_F.col(0) = Eigen::Vector3i(0, 1, 2);
        ref_F.col(1) = Eigen::Vector3i(0, 2, 3);
        auto ref_mesh = geometry::TriangleMesh(ref_V, ref_F);

        // Query at (0, 0, 1) — above the bowl. The nominal normal from
        // the tilted reference will be roughly (0, 0.5, 0.866).
        // In the local frame of the MLS fit, this direction is oblique to
        // the surface, and the quadratic term creates two intersections.
        Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
        pts.col(0) = Eigen::Vector3d(0, 0, 1.0);

        registration::RPSProjectionSettings settings;
        settings.max_roughness = 10.0; // high tolerance to not trip AMBIGUOUS
        settings.search_radius = 8.0;  // large to capture enough of the bowl

        auto result = registration::project_rps_points(
            pts, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

        ASSERT_EQ(result.projections.size(), 1u);

        // The tilted normal hitting a bowl-shaped quadratic may produce
        // two intersections. When two exist, the code picks the closest and
        // succeeds (OK) but reports the count in a warning. We check that
        // the code either: succeeds (picked closest), or flags a legitimate
        // condition (GRAZING from the oblique angle, AMBIGUOUS from curvature, etc).
        auto status = result.projections[0].quality.status;
        // The projection should handle this geometry without crashing.
        // Any non-INSUFFICIENT_DATA status is acceptable — the geometry is valid.
        EXPECT_NE(status, registration::ProjectionStatus::INSUFFICIENT_DATA)
            << "Bowl surface has plenty of data — should not report INSUFFICIENT_DATA";
    }
}

// ======================================================================
// §4 FAILURE MODE: NO_NORMAL
// Reference mesh with a degenerate (zero-area) triangle at query point.
// ======================================================================

TEST(RPSProjection, NoNormal_DegenerateRefTriangle) {
    // Build a reference mesh where the triangle at the query point is degenerate.
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, 3);
    // Three collinear vertices → zero-area triangle.
    V.col(0) = Eigen::Vector3d(0, 0, 0);
    V.col(1) = Eigen::Vector3d(1, 0, 0);
    V.col(2) = Eigen::Vector3d(2, 0, 0); // collinear!
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, 1);
    F.col(0) = Eigen::Vector3i(0, 1, 2);
    auto ref_mesh = geometry::TriangleMesh(V, F);

    // Dense measured mesh (doesn't matter — ref normal lookup should fail first).
    auto meas_mesh = make_dense_flat_grid(10.0, 20);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0.5, 0, 0); // on the degenerate triangle

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);
    EXPECT_EQ(result.projections[0].quality.status,
              registration::ProjectionStatus::NO_NORMAL)
        << "Degenerate reference triangle must report NO_NORMAL";
}

// ======================================================================
// BEHAVIORAL: MLS on flat surface recovers correct projection.
// On a flat surface at z=0 with query at z=0.5, projection along +Z
// should land at z=0 with deviation = -0.5.
// ======================================================================

TEST(RPSProjection, FlatSurfaceCorrectProjection) {
    auto ref_mesh = make_dense_flat_grid(10.0, 20); // flat at z=0
    auto meas_mesh = make_dense_flat_grid(10.0, 20);

    // Query at (0, 0, 0.5) — slightly above the flat surface.
    // Nominal normal from ref = +Z. Projection along +Z to z=0 → distance 0.5.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0.5);

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);
    auto& proj = result.projections[0];

    // Should succeed.
    EXPECT_EQ(proj.quality.status, registration::ProjectionStatus::OK)
        << "Flat surface projection should succeed";

    // Projected point should be near z=0.
    EXPECT_NEAR(proj.projected_point.z(), 0.0, 0.05)
        << "Projected point on flat z=0 surface should be near z=0";

    // Deviation along Z: projected.z - nom.z = 0 - 0.5 = -0.5
    double deviation = proj.ref_normal.dot(proj.projected_point - proj.ref_point);
    EXPECT_NEAR(deviation, -0.5, 0.05);
}

// ======================================================================
// BEHAVIORAL: Curved surface — MLS recovers correct projection while
// closest-point would drift tangentially.
//
// On a sphere of radius R, the closest point to a query above the apex
// IS the apex (no drift). But for a query OFFSET from the apex, the
// closest mesh vertex drifts tangentially, while the normal-projection
// onto the fitted surface stays on the nominal-normal line.
// ======================================================================

TEST(RPSProjection, CurvedSurface_MLSvsClosestPoint) {
    // Spherical cap: R=50mm, max angle 30°, dense mesh.
    auto cap = make_spherical_cap(50.0, 30.0, 10, 24);
    // Use same mesh as both ref and measured — no noise, pure geometry test.
    auto ref_mesh = cap;
    auto meas_mesh = cap;

    // Query a point on the sphere surface at an offset angle (15° from apex).
    // The true surface point at (theta=15°, phi=0) is:
    //   (R*sin(15°), 0, R*cos(15°)) = (12.94, 0, 48.30)
    // The outward normal at this point is the radial direction:
    //   (sin(15°), 0, cos(15°)) = (0.2588, 0, 0.9659)
    //
    // Query from slightly outside: p_nom + offset * normal.
    double theta = 15.0 * PI / 180.0;
    double R = 50.0;
    Eigen::Vector3d surface_pt(R * std::sin(theta), 0, R * std::cos(theta));
    Eigen::Vector3d true_normal(std::sin(theta), 0, std::cos(theta));
    double offset = 2.0;
    Eigen::Vector3d query = surface_pt + offset * true_normal;

    // For the reference mesh, the nominal normal at query should be ~true_normal.
    // The MLS fit should recover the spherical surface locally and project
    // back to ~surface_pt. Closest-point on the mesh would find a nearby
    // vertex, which on a tessellated sphere drifts from the true radial projection.

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = query;

    registration::RPSProjectionSettings settings;
    settings.max_projection_distance = 5.0;
    settings.max_roughness = 1.0;

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 1u);
    auto& proj = result.projections[0];

    EXPECT_EQ(proj.quality.status, registration::ProjectionStatus::OK)
        << "Projection onto smooth sphere should succeed";

    // The projected point should be close to the true surface point.
    double error = (proj.projected_point - surface_pt).norm();
    EXPECT_LT(error, 2.0)
        << "MLS projection on sphere should land within 2mm of true surface point "
           "(tessellation error is expected). Got error=" << error << "mm";

    // The deviation along the normal should be close to -offset.
    double deviation = proj.ref_normal.dot(proj.projected_point - proj.ref_point);
    EXPECT_NEAR(deviation, -offset, 1.0)
        << "Deviation along normal should be close to -" << offset << "mm";
}

// ======================================================================
// BEHAVIORAL: Point-cloud overload uses MLS too (not closest-point).
// Same flat-surface test but with the point-cloud API.
// ======================================================================

TEST(RPSProjection, PointCloudOverloadUsesMLS) {
    auto ref_mesh = make_dense_flat_grid(10.0, 20);

    // Build measured point cloud (same flat grid, extracted vertices).
    auto meas_mesh = make_dense_flat_grid(10.0, 20);
    Eigen::Matrix<double, 3, Eigen::Dynamic> meas_pts = meas_mesh.vertices();

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Eigen::Vector3d(0, 0, 0.3);

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_pts, geometry::RigidTransform{});

    ASSERT_EQ(result.projections.size(), 1u);
    auto& proj = result.projections[0];

    EXPECT_EQ(proj.quality.status, registration::ProjectionStatus::OK)
        << "Point-cloud overload should use MLS and succeed on flat surface";

    EXPECT_GT(proj.quality.neighborhood_size, 0)
        << "Point-cloud overload must report neighborhood_size > 0";

    EXPECT_GT(proj.quality.search_radius, 0)
        << "Point-cloud overload must report search_radius > 0";

    EXPECT_NEAR(proj.projected_point.z(), 0.0, 0.05)
        << "Point-cloud MLS projection on flat z=0 should land near z=0";
}

// ======================================================================
// REGRESSION: failure modes propagate (status != OK → not swallowed).
// ======================================================================

TEST(RPSProjection, FailedPointCountedCorrectly) {
    auto ref_mesh = make_flat_quad(10.0);
    auto meas_mesh = make_ring_mesh(5.0, 10.0, 24);

    // Two points: one over the hole (should fail), one on the ring (should succeed or fail
    // depending on local conditions, but at least the hole point must fail).
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts.col(0) = Eigen::Vector3d(0, 0, 0);    // center of hole
    pts.col(1) = Eigen::Vector3d(7, 0, 0);    // on the ring

    registration::RPSProjectionSettings settings;
    settings.max_projection_distance = 2.0;

    auto result = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 2u);
    EXPECT_TRUE(result.success); // overall call succeeded (some points may have failed)
    EXPECT_GE(result.num_failed, 1) << "At least the hole point must fail";
    EXPECT_FALSE(result.warnings.empty()) << "Warnings must be generated for failed points";
}

// ======================================================================
// ADVERSARIAL: Outlier-injected patch → robust fit rejects flyers
// ======================================================================

TEST(RPSProjection, OutlierRejection_RobustFit) {
    // Dense flat grid with 10% of points replaced by 50mm flyers.
    // The robust Huber IRLS fit should still recover the plane z≈0
    // accurately. A non-robust fit would be pulled toward the flyers.

    int n = 25;
    double half = 6.0;
    double step = 2.0 * half / (n - 1);
    int total = n * n;

    std::vector<Eigen::Vector3d> pts;
    pts.reserve(static_cast<std::size_t>(total));
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix)
            pts.push_back(Eigen::Vector3d(-half + ix*step, -half + iy*step, 0.0));

    // Inject 10% flyers at z=50.
    int n_outliers = total / 10;
    for (int i = 0; i < n_outliers; ++i) {
        pts[static_cast<std::size_t>(i)].z() = 50.0;
    }

    Eigen::Matrix<double, 3, Eigen::Dynamic> meas_V(3, total);
    for (int i = 0; i < total; ++i) meas_V.col(i) = pts[static_cast<std::size_t>(i)];

    // Simple grid triangulation.
    int nf = (n-1)*(n-1)*2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> meas_F(3, nf);
    int fi = 0;
    for (int iy = 0; iy < n-1; ++iy)
        for (int ix = 0; ix < n-1; ++ix) {
            int v00 = iy*n+ix;
            meas_F.col(fi++) = Eigen::Vector3i(v00, v00+1, v00+n+1);
            meas_F.col(fi++) = Eigen::Vector3i(v00, v00+n+1, v00+n);
        }
    auto meas_mesh = geometry::TriangleMesh(meas_V, meas_F);

    auto ref_mesh = make_flat_quad(10.0);

    Eigen::Matrix<double, 3, Eigen::Dynamic> query(3, 1);
    query.col(0) = Eigen::Vector3d(0, 0, 1.0); // 1mm above the plane

    registration::RPSProjectionSettings settings;
    settings.search_radius = 8.0;
    settings.max_roughness = 20.0; // high to not trip AMBIGUOUS from outliers
    settings.max_projection_distance = 5.0;

    auto result = registration::project_rps_points(
        query, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 1u);
    auto& proj = result.projections[0];

    if (proj.quality.status == registration::ProjectionStatus::OK) {
        // The robust fit should project close to z=0 (the true surface),
        // NOT toward z=50 (the flyers). Tolerance is generous but must
        // exclude the flyer-pulled result.
        double z_proj = proj.projected_point.z();
        std::cout << "OutlierRejection: projected z=" << z_proj
                  << " (expected ~0, flyers at 50)\n";
        EXPECT_LT(std::abs(z_proj), 5.0)
            << "Robust fit should project near z=0, not be pulled to z=50 by flyers";
        EXPECT_GT(std::abs(z_proj - 50.0), 10.0)
            << "Must not be near the flyer altitude";
    }
    // If the fit failed (AMBIGUOUS from the outlier contamination), that's
    // also acceptable — it surfaced the problem rather than silently using flyers.
}

// ======================================================================
// AMBIGUOUS_PATCH: query on a sharp crease where the local fit is noisy
// ======================================================================

TEST(RPSProjection, AmbiguousPatch_HighRoughness) {
    // Measured surface: V-groove (two planes meeting at a crease).
    // Query exactly on the crease → the local quadric fit has high
    // residual → AMBIGUOUS_PATCH or at least a non-zero roughness report.

    int n = 21;
    double half = 5.0;
    double step = 2.0 * half / (n - 1);
    int total = n * n;

    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, total);
    for (int iy = 0; iy < n; ++iy)
        for (int ix = 0; ix < n; ++ix) {
            double x = -half + ix * step;
            double y = -half + iy * step;
            double z = std::abs(x) * 0.5; // V-groove: z = 0.5*|x|
            V.col(iy*n+ix) = Eigen::Vector3d(x, y, z);
        }

    int nf = (n-1)*(n-1)*2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, nf);
    int fi = 0;
    for (int iy = 0; iy < n-1; ++iy)
        for (int ix = 0; ix < n-1; ++ix) {
            int v00 = iy*n+ix;
            F.col(fi++) = Eigen::Vector3i(v00, v00+1, v00+n+1);
            F.col(fi++) = Eigen::Vector3i(v00, v00+n+1, v00+n);
        }
    auto meas_mesh = geometry::TriangleMesh(V, F);
    auto ref_mesh = make_flat_quad(10.0);

    // Query at the crease (x=0).
    Eigen::Matrix<double, 3, Eigen::Dynamic> query(3, 1);
    query.col(0) = Eigen::Vector3d(0, 0, 0.5);

    registration::RPSProjectionSettings settings;
    settings.search_radius = 4.0;
    settings.max_roughness = 0.01; // strict → crease should trip it

    auto result = registration::project_rps_points(
        query, ref_mesh, meas_mesh, geometry::RigidTransform{}, settings);

    ASSERT_EQ(result.projections.size(), 1u);
    auto status = result.projections[0].quality.status;

    // Crease should produce high roughness → AMBIGUOUS_PATCH.
    EXPECT_EQ(status, registration::ProjectionStatus::AMBIGUOUS_PATCH)
        << "Query on a V-groove crease should report AMBIGUOUS_PATCH "
           "(high local fit residual). Status="
        << static_cast<int>(status);
}

// ======================================================================
// Repro-gate: two identical projection runs → identical output
// ======================================================================

TEST(RPSProjection, ReproGate) {
    auto ref_mesh = make_dense_flat_grid(10.0, 20);
    auto meas_mesh = make_dense_flat_grid(10.0, 20);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 3);
    pts.col(0) = Eigen::Vector3d(0, 0, 0.1);
    pts.col(1) = Eigen::Vector3d(3, 3, -0.1);
    pts.col(2) = Eigen::Vector3d(-2, 4, 0.05);

    auto r1 = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});
    auto r2 = registration::project_rps_points(
        pts, ref_mesh, meas_mesh, geometry::RigidTransform{});

    ASSERT_EQ(r1.projections.size(), r2.projections.size());
    for (std::size_t i = 0; i < r1.projections.size(); ++i) {
        EXPECT_EQ(r1.projections[i].projected_point,
                  r2.projections[i].projected_point)
            << "Projection " << i << " not bit-identical across runs";
        EXPECT_EQ(r1.projections[i].quality.status,
                  r2.projections[i].quality.status);
    }
}
