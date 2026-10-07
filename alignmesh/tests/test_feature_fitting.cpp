#include <gtest/gtest.h>
#include "alignmesh/features/feature_fitting.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace alignmesh::features;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

// ============================================================================
// Plane fitting
// ============================================================================

TEST(FitPlane, PerfectPlane) {
    // Points exactly on z=0 plane.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 100);
    SeededRng rng(42);
    std::uniform_real_distribution<double> d(-10, 10);
    for (int i = 0; i < 100; ++i)
        pts.col(i) = Vec3(d(rng), d(rng), 0);

    auto r = fit_plane(pts, FitCriterion::LEAST_SQUARES);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(std::abs(r.direction.dot(Vec3::UnitZ())), 1.0, 1e-10);
    EXPECT_NEAR(r.rms_residual, 0, 1e-10);
}

TEST(FitPlane, NoisyPlane) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 500);
    SeededRng rng(55);
    std::uniform_real_distribution<double> xy(-10, 10);
    std::normal_distribution<double> noise(0, 0.01);
    for (int i = 0; i < 500; ++i)
        pts.col(i) = Vec3(xy(rng), xy(rng), 5.0 + noise(rng));

    auto r = fit_plane(pts, FitCriterion::LEAST_SQUARES);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(std::abs(r.direction.dot(Vec3::UnitZ())), 1.0, 0.01);
    EXPECT_LT(r.rms_residual, 0.02);
}

TEST(FitPlane, MinZoneTighterThanLS) {
    // A known case where minimum-zone gives a tighter form error than LS.
    // Add outlier-like high/low points that pull LS but not min-zone.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 102);
    for (int i = 0; i < 100; ++i)
        pts.col(i) = Vec3(i * 0.1, 0, 0);  // 100 points on z=0
    pts.col(100) = Vec3(5, 0, 0.5);   // high outlier
    pts.col(101) = Vec3(5, 0, -0.5);  // low outlier

    auto ls = fit_plane(pts, FitCriterion::LEAST_SQUARES);
    auto mz = fit_plane(pts, FitCriterion::MINIMUM_ZONE);
    ASSERT_TRUE(ls.success && mz.success);

    // Min-zone should achieve tighter or equal form error (max-min).
    EXPECT_LE(mz.form_error, ls.form_error + 1e-6);
}

// ============================================================================
// Sphere fitting
// ============================================================================

TEST(FitSphere, PerfectSphere) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 200);
    for (int i = 0; i < 200; ++i) {
        double theta = kPi * (i + 0.5) / 200;
        double phi = 2.0 * kPi * i / 200;
        pts.col(i) = Vec3(10 * std::sin(theta) * std::cos(phi),
                          10 * std::sin(theta) * std::sin(phi),
                          10 * std::cos(theta));
    }

    auto r = fit_sphere(pts, FitCriterion::LEAST_SQUARES);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.radius, 10.0, 0.01);
    EXPECT_NEAR(r.origin.norm(), 0.0, 0.1);
    EXPECT_LT(r.rms_residual, 0.01);
}

TEST(FitSphere, OffCentreSphere) {
    Vec3 centre(5, -3, 7);
    double radius = 20.0;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 300);
    SeededRng rng(77);
    std::normal_distribution<double> noise(0, 0.005);
    for (int i = 0; i < 300; ++i) {
        double theta = kPi * (i + 0.5) / 300;
        double phi = 2.0 * kPi * i / 300;
        Vec3 p = centre + radius * Vec3(
            std::sin(theta) * std::cos(phi),
            std::sin(theta) * std::sin(phi),
            std::cos(theta));
        p += Vec3(noise(rng), noise(rng), noise(rng));
        pts.col(i) = p;
    }

    auto r = fit_sphere(pts);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.radius, radius, 0.1);
    EXPECT_NEAR((r.origin - centre).norm(), 0, 0.2);
}

// ============================================================================
// Cylinder fitting
// ============================================================================

TEST(FitCylinder, PerfectCylinder) {
    Vec3 axis_origin(0, 0, 0);
    Vec3 axis_dir = Vec3::UnitZ();
    double radius = 5.0;
    // Cylinder MUCH taller than wide so PCA axis is unambiguous.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 600);
    for (int i = 0; i < 600; ++i) {
        double theta = 2.0 * kPi * (i % 20) / 20;
        double z = (i / 20) * 2.0;  // 30 rings × 2mm = 60mm tall, radius = 5mm
        pts.col(i) = Vec3(radius * std::cos(theta), radius * std::sin(theta), z);
    }

    auto r = fit_cylinder(pts);
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(r.radius, 5.0, 0.1);
    EXPECT_NEAR(std::abs(r.direction.dot(Vec3::UnitZ())), 1.0, 0.01);
    EXPECT_LT(r.rms_residual, 0.01);
}

// ============================================================================
// Datum plane (one-sided / tangent fit)
// ============================================================================

TEST(DatumPlane, TangentOnHighPoints) {
    // Points with a flat base at z=0 and some peaks.
    // The datum plane should sit on the peaks (highest points).
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 50);
    SeededRng rng(42);
    std::uniform_real_distribution<double> xy(-5, 5);
    for (int i = 0; i < 50; ++i) {
        // Most points at z ∈ [-0.1, 0], a few peaks near z=0.
        double z = -0.1 * (i % 10) / 10.0;
        if (i < 3) z = 0.0;  // 3 high points exactly at z=0
        pts.col(i) = Vec3(xy(rng), xy(rng), z);
    }

    auto r = fit_datum_plane(pts, +1);  // material below (external datum)
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.criterion_used, FitCriterion::TANGENT_PLANE);

    // All residuals should be ≤ 0 (plane sits ON or ABOVE all points).
    for (auto res : r.residuals) {
        EXPECT_LE(res, 1e-10)
            << "Datum plane must sit on top — no point above the plane";
    }

    // The plane should be very close to z=0 (the highest points).
    // The datum plane should be close to the highest points.
    // The max residual should be ~0 (plane sits on the peaks).
    EXPECT_NEAR(r.max_residual, 0.0, 1e-10);
}

TEST(DatumPlane, InternalDatum) {
    // Internal datum: material above, plane sits below.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 50);
    SeededRng rng(99);
    std::uniform_real_distribution<double> xy(-5, 5);
    for (int i = 0; i < 50; ++i) {
        double z = 0.1 * (i % 10) / 10.0;  // peaks at z=0.1
        if (i < 3) z = 0.0;  // 3 low points at z=0
        pts.col(i) = Vec3(xy(rng), xy(rng), z);
    }

    auto r = fit_datum_plane(pts, -1);  // material above (internal datum)
    ASSERT_TRUE(r.success);

    // All residuals should be ≥ 0 (plane sits BELOW all points).
    for (auto res : r.residuals) {
        EXPECT_GE(res, -1e-10)
            << "Internal datum plane must sit below — no point below the plane";
    }
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(FeatureFitting, TooFewPoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts.col(0) = Vec3(0, 0, 0);
    pts.col(1) = Vec3(1, 0, 0);
    auto r = fit_plane(pts);
    EXPECT_FALSE(r.success);
}

TEST(FeatureFitting, CollinearPointsPlane) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 10);
    for (int i = 0; i < 10; ++i)
        pts.col(i) = Vec3(i * 1.0, 0, 0);
    auto r = fit_plane(pts);
    if (r.success) {
        EXPECT_FALSE(r.warnings.empty());
    }
}

// ============================================================================
// Q3: Cone datum unsupported
// ============================================================================

TEST(FeatureFitting, ConeDatumRejected) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 50);
    for (int i = 0; i < 50; ++i)
        pts.col(i) = Vec3(i * 0.1, 0, i * 0.2);
    auto r = fit_datum_cone(pts);
    EXPECT_FALSE(r.success);
    bool found = false;
    for (auto& w : r.warnings)
        if (w.find("UNSUPPORTED") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

// ============================================================================
// Q3: UAME datum cylinder
// ============================================================================

TEST(FeatureFitting, DatumCylinderUAME) {
    double radius = 5.0;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 600);
    for (int i = 0; i < 600; ++i) {
        double theta = 2.0 * kPi * (i % 20) / 20;
        double z = (i / 20) * 2.0;
        pts.col(i) = Vec3(radius * std::cos(theta), radius * std::sin(theta), z);
    }

    auto r_hole = fit_datum_cylinder(pts, true);   // internal → max-inscribed
    auto r_shaft = fit_datum_cylinder(pts, false);  // external → min-circumscribed

    ASSERT_TRUE(r_hole.success && r_shaft.success);
    EXPECT_TRUE(r_hole.is_uame);
    EXPECT_TRUE(r_shaft.is_uame);

    // For perfect data, inscribed = circumscribed = actual radius.
    EXPECT_NEAR(r_hole.radius, radius, 0.01);
    EXPECT_NEAR(r_shaft.radius, radius, 0.01);

    // UAME warning about not reusing as size.
    bool found_uame_warn = false;
    for (auto& w : r_hole.warnings)
        if (w.find("SIZE") != std::string::npos) found_uame_warn = true;
    EXPECT_TRUE(found_uame_warn);
}

// ============================================================================
// Q5: Min-zone conservative validation
// ============================================================================

TEST(FeatureFitting, MinZoneConservativeValidation) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 100);
    SeededRng rng(42);
    std::uniform_real_distribution<double> xy(-10, 10);
    std::normal_distribution<double> noise(0, 0.01);
    for (int i = 0; i < 100; ++i)
        pts.col(i) = Vec3(xy(rng), xy(rng), noise(rng));

    auto r = fit_plane(pts, FitCriterion::MINIMUM_ZONE);
    ASSERT_TRUE(r.success);
    // The result should be validated conservative.
    EXPECT_TRUE(r.minzone_validated_conservative)
        << "Min-zone should be validated against convex-hull bound";
}

// ============================================================================
// Constrained cylinder fit
// ============================================================================

TEST(FeatureFitting, ConstrainedCylinder) {
    double radius = 5.0;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 600);
    for (int i = 0; i < 600; ++i) {
        double theta = 2.0 * kPi * (i % 20) / 20;
        double z = (i / 20) * 2.0;
        pts.col(i) = Vec3(radius * std::cos(theta), radius * std::sin(theta), z);
    }

    // Constrain axis to Z.
    auto r = fit_cylinder_constrained(pts, Vec3::UnitZ());
    ASSERT_TRUE(r.success);
    EXPECT_NEAR(std::abs(r.direction.dot(Vec3::UnitZ())), 1.0, 1e-10);
    EXPECT_NEAR(r.radius, radius, 0.1);
}
