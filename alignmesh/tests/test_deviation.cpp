#include <gtest/gtest.h>
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>

using namespace alignmesh::analysis;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Build a flat XY quad mesh: 2 triangles forming a square [0,size]×[0,size].
TriangleMesh make_flat_quad(double size = 10.0) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(size, 0, 0);
    v.col(2) = Vec3(size, size, 0);
    v.col(3) = Vec3(0, size, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 2);
    t.col(0) << 0, 1, 2;
    t.col(1) << 0, 2, 3;
    return TriangleMesh(std::move(v), std::move(t));
}

// Build a denser flat mesh for testing.
TriangleMesh make_flat_grid(int nx, int ny, double spacing = 1.0) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, nx * ny);
    int idx = 0;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < ny; ++j)
            v.col(idx++) = Vec3(i * spacing, j * spacing, 0);

    std::vector<Eigen::Vector3i> tris;
    for (int i = 0; i < nx - 1; ++i) {
        for (int j = 0; j < ny - 1; ++j) {
            int a = i * ny + j;
            int b = (i + 1) * ny + j;
            int c = (i + 1) * ny + (j + 1);
            int d = i * ny + (j + 1);
            tris.push_back(Eigen::Vector3i(a, b, c));
            tris.push_back(Eigen::Vector3i(a, c, d));
        }
    }
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, static_cast<Eigen::Index>(tris.size()));
    for (std::size_t i = 0; i < tris.size(); ++i)
        t.col(static_cast<Eigen::Index>(i)) = tris[i];

    return TriangleMesh(std::move(v), std::move(t));
}

} // namespace

// ============================================================================
// Closest point on triangle
// ============================================================================

TEST(ClosestPointOnTriangle, PointAboveFace) {
    Vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
    Vec3 query(0.2, 0.2, 1.0);  // directly above interior
    double dsq;
    Vec3 cp = closest_point_on_triangle(query, v0, v1, v2, dsq);
    EXPECT_NEAR(cp(0), 0.2, 1e-12);
    EXPECT_NEAR(cp(1), 0.2, 1e-12);
    EXPECT_NEAR(cp(2), 0.0, 1e-12);
    EXPECT_NEAR(dsq, 1.0, 1e-12);
}

TEST(ClosestPointOnTriangle, PointNearEdge) {
    Vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
    Vec3 query(0.5, -0.1, 0);  // below edge v0-v1
    double dsq;
    Vec3 cp = closest_point_on_triangle(query, v0, v1, v2, dsq);
    EXPECT_NEAR(cp(1), 0.0, 1e-12);
    EXPECT_NEAR(cp(0), 0.5, 1e-12);
    EXPECT_NEAR(dsq, 0.01, 1e-12);
}

TEST(ClosestPointOnTriangle, PointNearVertex) {
    Vec3 v0(0, 0, 0), v1(1, 0, 0), v2(0, 1, 0);
    Vec3 query(-0.1, -0.1, 0);  // near vertex v0
    double dsq;
    Vec3 cp = closest_point_on_triangle(query, v0, v1, v2, dsq);
    EXPECT_NEAR(cp(0), 0.0, 1e-12);
    EXPECT_NEAR(cp(1), 0.0, 1e-12);
    EXPECT_NEAR(dsq, 0.02, 1e-12);
}

// ============================================================================
// Known deviation field
// ============================================================================

TEST(Deviation, KnownUniformOffset) {
    auto mesh = make_flat_quad(10.0);

    // Points uniformly offset by 0.5 above the plane.
    int n = 100;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    SeededRng rng(42);
    std::uniform_real_distribution<double> xy(0.1, 9.9);
    for (int i = 0; i < n; ++i) {
        pts.col(i) = Vec3(xy(rng), xy(rng), 0.5);
    }

    DeviationSettings settings;
    settings.tolerance = 1.0;

    auto result = compute_deviation(pts, mesh, settings);
    ASSERT_TRUE(result.success);

    // All distances should be exactly 0.5.
    EXPECT_NEAR(result.unsigned_stats.min, 0.5, 0.01);
    EXPECT_NEAR(result.unsigned_stats.max, 0.5, 0.01);
    EXPECT_NEAR(result.unsigned_stats.mean, 0.5, 0.01);
    EXPECT_NEAR(result.unsigned_stats.rms, 0.5, 0.01);
    EXPECT_NEAR(result.unsigned_stats.std_dev, 0.0, 0.01);
}

TEST(Deviation, KnownBump) {
    // Inject a bump of known height 2.0 at the center of a flat mesh.
    auto mesh = make_flat_grid(20, 20, 0.5);  // 10x10 flat grid

    int n = 200;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    SeededRng rng(55);
    std::uniform_real_distribution<double> xy(0.5, 9.0);

    // 90% of points at z=0 (on the mesh), 10% at z=2.0 (bump).
    for (int i = 0; i < n; ++i) {
        double z = (i < 20) ? 2.0 : 0.0;  // first 20 points are the bump
        pts.col(i) = Vec3(xy(rng), xy(rng), z);
    }

    DeviationSettings settings;
    settings.tolerance = 0.5;

    auto result = compute_deviation(pts, mesh, settings);
    ASSERT_TRUE(result.success);

    // Mean should be ~0.2 (20/200 * 2.0 + 180/200 * 0.0).
    EXPECT_NEAR(result.unsigned_stats.mean, 0.2, 0.05);

    // Max should be ~2.0 (the bump).
    EXPECT_NEAR(result.unsigned_stats.max, 2.0, 0.1);

    // 90% within tolerance, 10% outside.
    EXPECT_GT(result.unsigned_stats.percent_within_tolerance, 80.0);
    EXPECT_LT(result.unsigned_stats.percent_within_tolerance, 95.0);
}

TEST(Deviation, LocalBumpHiddenByGlobalRMS) {
    // A part with a localized out-of-tolerance bump but low global RMS.
    // Percentile stats should reveal it; RMS alone would hide it.
    auto mesh = make_flat_grid(20, 20, 0.5);

    int n = 500;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    SeededRng rng(77);
    std::uniform_real_distribution<double> xy(0.5, 9.0);

    // 485 points at z=0 (perfect), 15 points at z=3.0 (spike, 3%).
    for (int i = 0; i < n; ++i) {
        double z = (i < 15) ? 3.0 : 0.0;
        pts.col(i) = Vec3(xy(rng), xy(rng), z);
    }

    DeviationSettings settings;
    settings.tolerance = 0.5;

    auto result = compute_deviation(pts, mesh, settings);
    ASSERT_TRUE(result.success);

    // RMS is relatively low (spike is 3% of points).
    EXPECT_LT(result.unsigned_stats.rms, 1.0);

    // But max and p99 reveal the spike.
    EXPECT_GT(result.unsigned_stats.max, 2.0);
    EXPECT_GT(result.unsigned_stats.p99, 1.0);
}

// ============================================================================
// Sampling adequacy
// ============================================================================

TEST(Deviation, SamplingAdequacy) {
    auto mesh = make_flat_grid(20, 20, 0.5);

    // Very sparse sampling: only 5 points over a 10x10 area.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 5);
    pts.col(0) = Vec3(1, 1, 0);
    pts.col(1) = Vec3(5, 5, 0);
    pts.col(2) = Vec3(9, 1, 0);
    pts.col(3) = Vec3(1, 9, 0);
    pts.col(4) = Vec3(9, 9, 0);

    DeviationSettings settings;
    settings.feature_size = 0.5;  // need to resolve 0.5mm features
    settings.min_density = 4.0;   // need at least 4 pts/unit²

    auto result = compute_deviation(pts, mesh, settings);
    ASSERT_TRUE(result.success);

    // Should flag sampling as inadequate.
    EXPECT_FALSE(result.unsigned_stats.sampling_adequate);
    EXPECT_FALSE(result.unsigned_stats.sampling_warning.empty());
}

// ============================================================================
// Signed deviation
// ============================================================================

TEST(Deviation, SignedDeviation) {
    auto mesh = make_flat_quad(10.0);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts.col(0) = Vec3(5, 5, 1.0);   // above → positive
    pts.col(1) = Vec3(5, 5, -1.0);  // below → negative

    auto result = compute_deviation(pts, mesh);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.deviations.size(), 2u);

    // Both should have distance = 1.0.
    EXPECT_NEAR(result.deviations[0].distance, 1.0, 1e-10);
    EXPECT_NEAR(result.deviations[1].distance, 1.0, 1e-10);

    // Signs should differ (if normals are available on the mesh).
    if (result.deviations[0].sign_reliable && result.deviations[1].sign_reliable) {
        EXPECT_GT(result.deviations[0].signed_distance, 0);
        EXPECT_LT(result.deviations[1].signed_distance, 0);
    }
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(DeviationAdversarial, EmptyPoints) {
    auto mesh = make_flat_quad();
    Eigen::Matrix<double, 3, Eigen::Dynamic> empty(3, 0);
    auto result = compute_deviation(empty, mesh);
    EXPECT_FALSE(result.success);
}

TEST(DeviationAdversarial, EmptyMesh) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 0);
    TriangleMesh empty_mesh(std::move(v), std::move(t));
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Vec3(1, 2, 3);
    auto result = compute_deviation(pts, empty_mesh);
    EXPECT_FALSE(result.success);
}
