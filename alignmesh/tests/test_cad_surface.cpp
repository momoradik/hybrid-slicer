#include <gtest/gtest.h>
#include "alignmesh/cad/analytic_surface.h"
#include "alignmesh/analysis/deviation.h"

#include <cmath>
#include <filesystem>
#include <numbers>

using namespace alignmesh::cad;
using namespace alignmesh::geometry;

static constexpr double kPi = std::numbers::pi;

// ============================================================================
// Plane
// ============================================================================

TEST(PlaneS, PointAbove) {
    PlaneS plane(Vec3(0, 0, 0), Vec3(0, 0, 1));
    auto r = plane.closest_point(Vec3(3, 4, 5));
    EXPECT_NEAR(r.distance, 5.0, 1e-12);
    EXPECT_NEAR(r.signed_distance, 5.0, 1e-12);
    EXPECT_NEAR(r.closest_point(0), 3.0, 1e-12);
    EXPECT_NEAR(r.closest_point(1), 4.0, 1e-12);
    EXPECT_NEAR(r.closest_point(2), 0.0, 1e-12);
}

TEST(PlaneS, PointBelow) {
    PlaneS plane(Vec3(0, 0, 0), Vec3(0, 0, 1));
    auto r = plane.closest_point(Vec3(1, 2, -3));
    EXPECT_NEAR(r.distance, 3.0, 1e-12);
    EXPECT_NEAR(r.signed_distance, -3.0, 1e-12);
}

TEST(PlaneS, PointOnPlane) {
    PlaneS plane(Vec3(0, 0, 0), Vec3(0, 0, 1));
    auto r = plane.closest_point(Vec3(5, 5, 0));
    EXPECT_NEAR(r.distance, 0.0, 1e-12);
    EXPECT_NEAR(r.signed_distance, 0.0, 1e-12);
}

TEST(PlaneS, OffsetPlane) {
    PlaneS plane(Vec3(0, 0, 10), Vec3(0, 0, 1));
    auto r = plane.closest_point(Vec3(0, 0, 15));
    EXPECT_NEAR(r.signed_distance, 5.0, 1e-12);
}

// ============================================================================
// Cylinder
// ============================================================================

TEST(CylinderS, PointOutside) {
    // Cylinder along Z, radius 5, at origin.
    CylinderS cyl(Vec3(0, 0, 0), Vec3(0, 0, 1), 5.0);
    auto r = cyl.closest_point(Vec3(10, 0, 3));
    EXPECT_NEAR(r.distance, 5.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, 5.0, 1e-10);
    EXPECT_NEAR(r.closest_point(0), 5.0, 1e-10);
    EXPECT_NEAR(r.closest_point(1), 0.0, 1e-10);
    EXPECT_NEAR(r.closest_point(2), 3.0, 1e-10);
}

TEST(CylinderS, PointInside) {
    CylinderS cyl(Vec3(0, 0, 0), Vec3(0, 0, 1), 5.0);
    auto r = cyl.closest_point(Vec3(2, 0, 7));
    EXPECT_NEAR(r.distance, 3.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, -3.0, 1e-10);
}

TEST(CylinderS, PointOnSurface) {
    CylinderS cyl(Vec3(0, 0, 0), Vec3(0, 0, 1), 5.0);
    auto r = cyl.closest_point(Vec3(5, 0, 0));
    EXPECT_NEAR(r.distance, 0.0, 1e-10);
}

// ============================================================================
// Sphere
// ============================================================================

TEST(SphereS, PointOutside) {
    SphereS sphere(Vec3(0, 0, 0), 10.0);
    auto r = sphere.closest_point(Vec3(15, 0, 0));
    EXPECT_NEAR(r.distance, 5.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, 5.0, 1e-10);
    EXPECT_NEAR(r.closest_point(0), 10.0, 1e-10);
}

TEST(SphereS, PointInside) {
    SphereS sphere(Vec3(0, 0, 0), 10.0);
    auto r = sphere.closest_point(Vec3(3, 0, 0));
    EXPECT_NEAR(r.distance, 7.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, -7.0, 1e-10);
}

TEST(SphereS, PointOnSurface) {
    SphereS sphere(Vec3(0, 0, 0), 10.0);
    auto r = sphere.closest_point(Vec3(0, 10, 0));
    EXPECT_NEAR(r.distance, 0.0, 1e-10);
}

TEST(SphereS, PointAtCenter) {
    SphereS sphere(Vec3(0, 0, 0), 10.0);
    auto r = sphere.closest_point(Vec3(0, 0, 0));
    EXPECT_NEAR(r.distance, 10.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, -10.0, 1e-10);
}

// ============================================================================
// True-surface deviation: known offsets
// ============================================================================

TEST(TrueSurfaceDev, UniformOffsetFromPlane) {
    PlaneS plane(Vec3(0, 0, 0), Vec3(0, 0, 1));
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 100);
    for (int i = 0; i < 100; ++i)
        pts.col(i) = Vec3(i * 0.1, 0, 0.5);  // all at z = 0.5

    auto dev = compute_true_surface_deviation(pts, plane);
    EXPECT_NEAR(dev.rms, 0.5, 1e-10);
    EXPECT_NEAR(dev.max_abs, 0.5, 1e-10);
    EXPECT_NEAR(dev.mean_signed, 0.5, 1e-10);
}

TEST(TrueSurfaceDev, SphericalOffset) {
    SphereS sphere(Vec3(0, 0, 0), 50.0);
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 100);
    // Points at radius 50.1 → offset = 0.1.
    for (int i = 0; i < 100; ++i) {
        double theta = kPi * (i + 0.5) / 100;
        pts.col(i) = Vec3(50.1 * std::sin(theta), 0, 50.1 * std::cos(theta));
    }

    auto dev = compute_true_surface_deviation(pts, sphere);
    EXPECT_NEAR(dev.rms, 0.1, 0.001);
    EXPECT_NEAR(dev.mean_signed, 0.1, 0.001);
}

// ============================================================================
// True-surface vs tessellation bias
// ============================================================================

TEST(TrueSurfaceDev, TessellationBias) {
    // On a curved surface (sphere R=50), tessellation introduces chord error.
    // True-surface deviation should be MORE ACCURATE than mesh deviation.
    SphereS sphere(Vec3(0, 0, 0), 50.0);

    // Points on the sphere surface (should have zero true deviation).
    int n = 200;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    for (int i = 0; i < n; ++i) {
        double theta = kPi * (i + 0.5) / n;
        double phi = 2.0 * kPi * i / n;
        pts.col(i) = Vec3(50.0 * std::sin(theta) * std::cos(phi),
                          50.0 * std::sin(theta) * std::sin(phi),
                          50.0 * std::cos(theta));
    }

    auto true_dev = compute_true_surface_deviation(pts, sphere);

    // True deviation should be essentially zero (points are ON the surface).
    EXPECT_LT(true_dev.rms, 1e-8);

    // A tessellated sphere would have nonzero deviation due to chord error.
    // (We don't build the tessellated sphere here, but the test proves that
    //  the true-surface path achieves machine-precision deviation.)
}

// ============================================================================
// STEP file header reading
// ============================================================================

TEST(StepFile, ReadHeader) {
    std::string path = "C:/Users/Sina/OneDrive/Desktop/sinatest.step";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "sinatest.step not found";
    }

    auto info = read_step_header(path);
    EXPECT_FALSE(info.filename.empty());
    EXPECT_FALSE(info.schema.empty());
    EXPECT_GT(info.file_size, 0u);
    EXPECT_GT(info.entity_count, 0);

    std::cout << "\n=== sinatest.step header ===" << std::endl;
    std::cout << "Description: " << info.description << std::endl;
    std::cout << "Schema: " << info.schema << std::endl;
    std::cout << "System: " << info.originating_system << std::endl;
    std::cout << "Timestamp: " << info.timestamp << std::endl;
    std::cout << "Size: " << info.file_size << " bytes" << std::endl;
    std::cout << "Entities: " << info.entity_count << std::endl;
    std::cout << "Has PMI: " << (info.has_pmi ? "yes" : "no") << std::endl;
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(CadAdversarial, CylinderOnAxis) {
    // Point exactly on the cylinder axis → distance = radius.
    CylinderS cyl(Vec3(0, 0, 0), Vec3(0, 0, 1), 5.0);
    auto r = cyl.closest_point(Vec3(0, 0, 10));
    EXPECT_NEAR(r.distance, 5.0, 1e-10);
    EXPECT_NEAR(r.signed_distance, -5.0, 1e-10);
}

TEST(CadAdversarial, EmptyPoints) {
    PlaneS plane(Vec3(0, 0, 0), Vec3(0, 0, 1));
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 0);
    auto dev = compute_true_surface_deviation(pts, plane);
    EXPECT_EQ(dev.per_point.size(), 0u);
}
