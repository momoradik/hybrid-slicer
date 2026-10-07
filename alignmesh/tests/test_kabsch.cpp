#include <gtest/gtest.h>
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace alignmesh::alignment;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Helper: generate N random 3D points in [-range, range]^3.
Eigen::Matrix<double, 3, Eigen::Dynamic>
random_points(SeededRng& rng, int n, double range = 10.0) {
    std::uniform_real_distribution<double> dist(-range, range);
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    for (int i = 0; i < n; ++i) {
        pts(0, i) = dist(rng);
        pts(1, i) = dist(rng);
        pts(2, i) = dist(rng);
    }
    return pts;
}

// Helper: generate a random rigid transform.
RigidTransform random_transform(SeededRng& rng) {
    std::uniform_real_distribution<double> angle(-kPi, kPi);
    std::uniform_real_distribution<double> trans(-10.0, 10.0);
    std::normal_distribution<double> gauss(0.0, 1.0);
    Vec3 axis(gauss(rng), gauss(rng), gauss(rng));
    double n = axis.norm();
    if (n < 1e-12) axis = Vec3::UnitX();
    else axis /= n;
    Mat3 R = Eigen::AngleAxisd(angle(rng), axis).toRotationMatrix();
    Vec3 t(trans(rng), trans(rng), trans(rng));
    return RigidTransform::from_rotation_translation(R, t);
}

} // namespace

// ============================================================================
// Ground truth: known transform recovery
// ============================================================================

TEST(Kabsch, RecoverKnownTransform_MachineEpsilon) {
    // THE keystone exactness test.
    // Generate synthetic pairs with a KNOWN transform; recover it to
    // machine precision.
    SeededRng rng(42);
    for (int trial = 0; trial < 50; ++trial) {
        int n = 10 + trial;
        auto source = random_points(rng, n);
        auto T_true = random_transform(rng);
        auto target = T_true.apply_cloud(source);

        auto result = align_landmarks(source, target);
        ASSERT_TRUE(result.success) << "Trial " << trial << " failed";

        // Rotation error
        Mat3 R_err = result.transform.rotation() - T_true.rotation();
        EXPECT_LT(R_err.norm(), 1e-10)
            << "Trial " << trial << " rotation error = " << R_err.norm();

        // Translation error
        Vec3 t_err = result.transform.translation() - T_true.translation();
        EXPECT_LT(t_err.norm(), 1e-10)
            << "Trial " << trial << " translation error = " << t_err.norm();

        // Residuals should be ~0
        EXPECT_LT(result.diagnostics.weighted_rms, 1e-10);
    }
}

TEST(Kabsch, RecoverIdentity) {
    // source == target -> identity transform, zero residuals.
    SeededRng rng(100);
    auto pts = random_points(rng, 20);
    auto result = align_landmarks(pts, pts);
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.transform.is_identity(1e-10));
    EXPECT_LT(result.diagnostics.weighted_rms, 1e-14);
}

// ============================================================================
// Reflection test: det(R) = +1 enforced
// ============================================================================

TEST(Kabsch, ReflectionRejected) {
    // Configuration where naive SVD would produce a reflection:
    // source in a plane, target is the mirror image.
    Eigen::Matrix<double, 3, Eigen::Dynamic> source(3, 4);
    source.col(0) = Vec3(0, 0, 0);
    source.col(1) = Vec3(1, 0, 0);
    source.col(2) = Vec3(0, 1, 0);
    source.col(3) = Vec3(1, 1, 0);

    // Reflect across Z: (x, y, z) -> (x, y, -z)
    // But all z == 0 so the mirror doesn't change the points.
    // Instead: reflect across X: (x, y, z) -> (-x, y, z)
    Eigen::Matrix<double, 3, Eigen::Dynamic> target = source;
    target.row(0) = -target.row(0);
    // Shift so centroid is nonzero (to avoid trivial case)
    target.colwise() += Vec3(5, 0, 0);

    auto result = align_landmarks(source, target);
    // Should succeed (or warn about coplanar) but det(R) must be +1.
    if (result.success) {
        double det = result.transform.rotation().determinant();
        EXPECT_NEAR(det, 1.0, 1e-12)
            << "det(R) = " << det << " — reflection slipped through!";
    }
}

TEST(Kabsch, ReflectionWith3DPoints) {
    // Non-planar points where SVD would want a reflection.
    Eigen::Matrix<double, 3, Eigen::Dynamic> source(3, 5);
    source.col(0) = Vec3(0, 0, 0);
    source.col(1) = Vec3(1, 0, 0);
    source.col(2) = Vec3(0, 1, 0);
    source.col(3) = Vec3(0, 0, 1);
    source.col(4) = Vec3(1, 1, 1);

    // Mirror across YZ plane
    Eigen::Matrix<double, 3, Eigen::Dynamic> target = source;
    target.row(0) = -target.row(0);

    auto result = align_landmarks(source, target);
    ASSERT_TRUE(result.success);
    double det = result.transform.rotation().determinant();
    EXPECT_NEAR(det, 1.0, 1e-12)
        << "det(R) = " << det << " — reflection not corrected!";
}

// ============================================================================
// Weighted alignment
// ============================================================================

TEST(Kabsch, WeightedSubset) {
    // Heavily weight one pair; the transform should almost perfectly map
    // that pair at the expense of the others.
    Eigen::Matrix<double, 3, Eigen::Dynamic> source(3, 5);
    source.col(0) = Vec3(0, 0, 0);
    source.col(1) = Vec3(1, 0, 0);
    source.col(2) = Vec3(0, 1, 0);
    source.col(3) = Vec3(0, 0, 1);
    source.col(4) = Vec3(1, 1, 1);

    Mat3 R = Eigen::AngleAxisd(0.3, Vec3::UnitZ()).toRotationMatrix();
    Vec3 t(2, 3, 4);
    auto T_true = RigidTransform::from_rotation_translation(R, t);
    auto target = T_true.apply_cloud(source);

    // Add noise to all but pair 0
    SeededRng rng(77);
    std::normal_distribution<double> noise(0.0, 0.5);
    for (int i = 1; i < 5; ++i) {
        target(0, i) += noise(rng);
        target(1, i) += noise(rng);
        target(2, i) += noise(rng);
    }

    // Heavy weight on pair 0
    Eigen::VectorXd w(5);
    w << 1000, 1, 1, 1, 1;

    auto result = align_landmarks(source, target, w);
    ASSERT_TRUE(result.success);

    // Pair 0 should have small residual (it dominates the fit).
    EXPECT_LT(result.diagnostics.residuals[0], 0.01);

    // Weight imbalance (max/min) should be high.
    EXPECT_GT(result.diagnostics.weight_imbalance, 100.0);
}

TEST(Kabsch, UniformWeightsSameAsNoWeights) {
    SeededRng rng(99);
    auto source = random_points(rng, 15);
    auto T = random_transform(rng);
    auto target = T.apply_cloud(source);

    auto r1 = align_landmarks(source, target);
    Eigen::VectorXd ones = Eigen::VectorXd::Ones(15);
    auto r2 = align_landmarks(source, target, ones);

    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);
    EXPECT_TRUE(r1.transform.is_approx(r2.transform, 1e-12));
}

// ============================================================================
// Degenerate configurations → flagged, not silently solved
// ============================================================================

TEST(Kabsch, TooFewPoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 2), t(3, 2);
    s.col(0) = Vec3(0, 0, 0); s.col(1) = Vec3(1, 0, 0);
    t.col(0) = Vec3(1, 0, 0); t.col(1) = Vec3(2, 0, 0);
    auto result = align_landmarks(s, t);
    EXPECT_FALSE(result.success);
}

TEST(Kabsch, CollinearPoints) {
    // 5 points all on the X axis → rank-1 configuration.
    Eigen::Matrix<double, 3, Eigen::Dynamic> source(3, 5);
    for (int i = 0; i < 5; ++i) source.col(i) = Vec3(i * 1.0, 0, 0);
    auto target = source;
    target.colwise() += Vec3(5, 0, 0);

    auto result = align_landmarks(source, target);
    EXPECT_FALSE(result.success)
        << "Collinear config should fail (rank-deficient)";
}

TEST(Kabsch, CoplanarFlagged) {
    // 6 points in the XY plane.
    Eigen::Matrix<double, 3, Eigen::Dynamic> source(3, 6);
    source.col(0) = Vec3(0, 0, 0);
    source.col(1) = Vec3(1, 0, 0);
    source.col(2) = Vec3(0, 1, 0);
    source.col(3) = Vec3(1, 1, 0);
    source.col(4) = Vec3(2, 0, 0);
    source.col(5) = Vec3(0, 2, 0);

    // Apply a pure-Z rotation + translation (within the plane)
    Mat3 R = Eigen::AngleAxisd(0.5, Vec3::UnitZ()).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3(3, 4, 0));
    auto target = T.apply_cloud(source);

    auto result = align_landmarks(source, target);
    // May succeed (planar is solvable for in-plane transforms) but should
    // warn about coplanar / poor conditioning for out-of-plane.
    if (result.success) {
        EXPECT_LT(result.diagnostics.sv_min, result.diagnostics.sv_max * 0.01)
            << "Expected small sv_min for coplanar config";
        bool found_warning = false;
        for (auto& w : result.warnings)
            if (w.find("coplanar") != std::string::npos ||
                w.find("condition") != std::string::npos)
                found_warning = true;
        EXPECT_TRUE(found_warning) << "Expected a coplanar/conditioning warning";
    }
}

// ============================================================================
// Noise sweep: residuals match injected noise
// ============================================================================

TEST(Kabsch, NoiseSweep) {
    SeededRng rng(55);
    auto source = random_points(rng, 100);
    auto T_true = random_transform(rng);
    auto target_clean = T_true.apply_cloud(source);

    for (double sigma : {0.001, 0.01, 0.1, 1.0}) {
        std::normal_distribution<double> noise(0.0, sigma);
        auto target = target_clean;
        for (Eigen::Index i = 0; i < target.cols(); ++i) {
            target(0, i) += noise(rng);
            target(1, i) += noise(rng);
            target(2, i) += noise(rng);
        }

        auto result = align_landmarks(source, target);
        ASSERT_TRUE(result.success) << "Failed at sigma=" << sigma;

        // RMS should be on the order of sigma * sqrt(3) (noise in 3 dims).
        double expected_rms = sigma * std::sqrt(3.0);
        EXPECT_GT(result.diagnostics.unweighted_rms, expected_rms * 0.3)
            << "RMS too low at sigma=" << sigma;
        EXPECT_LT(result.diagnostics.unweighted_rms, expected_rms * 3.0)
            << "RMS too high at sigma=" << sigma;
    }
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(KabschAdversarial, DuplicateSourcePoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 4), t(3, 4);
    s.col(0) = Vec3(0, 0, 0); s.col(1) = Vec3(0, 0, 0);  // duplicate
    s.col(2) = Vec3(1, 0, 0); s.col(3) = Vec3(0, 1, 0);
    t.col(0) = Vec3(1, 0, 0); t.col(1) = Vec3(1, 0, 0);
    t.col(2) = Vec3(2, 0, 0); t.col(3) = Vec3(1, 1, 0);
    auto result = align_landmarks(s, t);
    EXPECT_FALSE(result.success) << "Duplicate source points should fail";
}

TEST(KabschAdversarial, ZeroWeight) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 3);
    s.col(0) = Vec3(0,0,0); s.col(1) = Vec3(1,0,0); s.col(2) = Vec3(0,1,0);
    t.col(0) = Vec3(1,0,0); t.col(1) = Vec3(2,0,0); t.col(2) = Vec3(1,1,0);
    Eigen::VectorXd w(3);
    w << 1.0, 0.0, 1.0;  // zero weight
    auto result = align_landmarks(s, t, w);
    EXPECT_FALSE(result.success);
}

TEST(KabschAdversarial, NegativeWeight) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 3);
    s.col(0) = Vec3(0,0,0); s.col(1) = Vec3(1,0,0); s.col(2) = Vec3(0,1,0);
    t.col(0) = Vec3(1,0,0); t.col(1) = Vec3(2,0,0); t.col(2) = Vec3(1,1,0);
    Eigen::VectorXd w(3);
    w << 1.0, -1.0, 1.0;
    auto result = align_landmarks(s, t, w);
    EXPECT_FALSE(result.success);
}

TEST(KabschAdversarial, OnePointDominates) {
    // One point with 99% of the weight.
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 4), t(3, 4);
    s.col(0) = Vec3(0,0,0); s.col(1) = Vec3(1,0,0);
    s.col(2) = Vec3(0,1,0); s.col(3) = Vec3(0,0,1);
    Mat3 R = Eigen::AngleAxisd(0.1, Vec3::UnitZ()).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3(1,2,3));
    t = T.apply_cloud(s);

    Eigen::VectorXd w(4);
    w << 99.0, 0.333, 0.333, 0.334;  // 99% on point 0

    auto result = align_landmarks(s, t, w);
    ASSERT_TRUE(result.success);
    // max/min = 99/0.333 ≈ 297 — clearly dominant.
    EXPECT_GT(result.diagnostics.weight_imbalance, 100.0);
    bool found = false;
    for (auto& warn : result.warnings)
        if (warn.find("imbalance") != std::string::npos) found = true;
    EXPECT_TRUE(found);
}

TEST(KabschAdversarial, MirrorImage) {
    // Mirror image: target = reflect(source). det correction should
    // prevent a reflection and produce a proper rotation (det=+1).
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 5);
    s.col(0) = Vec3(1, 2, 3);
    s.col(1) = Vec3(4, 5, 6);
    s.col(2) = Vec3(7, 2, 1);
    s.col(3) = Vec3(0, 3, 5);
    s.col(4) = Vec3(2, 8, 4);

    Eigen::Matrix<double, 3, Eigen::Dynamic> t = s;
    t.row(0) = -t.row(0);  // reflect X

    auto result = align_landmarks(s, t);
    ASSERT_TRUE(result.success);
    EXPECT_NEAR(result.transform.rotation().determinant(), 1.0, 1e-12);
}

TEST(KabschAdversarial, MinimumThreePoints) {
    // Exactly 3 non-degenerate points should succeed.
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 3);
    s.col(0) = Vec3(0,0,0); s.col(1) = Vec3(1,0,0); s.col(2) = Vec3(0,0,1);
    auto T = RigidTransform::from_rotation_translation(
        Eigen::AngleAxisd(0.5, Vec3(1,1,0).normalized()).toRotationMatrix(),
        Vec3(1, 2, 3));
    t = T.apply_cloud(s);

    auto result = align_landmarks(s, t);
    ASSERT_TRUE(result.success);
    EXPECT_LT(result.diagnostics.weighted_rms, 1e-10);
}

TEST(KabschAdversarial, NaNInput) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 3);
    s.col(0) = Vec3(0,0,0); s.col(1) = Vec3(1,0,0); s.col(2) = Vec3(0,1,0);
    t = s;
    t(0, 0) = std::numeric_limits<double>::quiet_NaN();
    auto result = align_landmarks(s, t);
    EXPECT_FALSE(result.success);
}

TEST(KabschAdversarial, Determinism) {
    // Same input → bit-identical transform across repeated runs.
    SeededRng rng(123);
    auto source = random_points(rng, 30);
    auto T_true = random_transform(rng);
    auto target = T_true.apply_cloud(source);

    auto r1 = align_landmarks(source, target);
    auto r2 = align_landmarks(source, target);
    ASSERT_TRUE(r1.success);
    ASSERT_TRUE(r2.success);

    // Bit-identical (not just approximately equal).
    EXPECT_EQ(r1.transform.matrix(), r2.transform.matrix());
    EXPECT_EQ(r1.diagnostics.weighted_rms, r2.diagnostics.weighted_rms);
}

TEST(KabschAdversarial, SizeMismatch) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 4);
    s.setZero(); t.setZero();
    auto result = align_landmarks(s, t);
    EXPECT_FALSE(result.success);
}

TEST(KabschAdversarial, WeightSizeMismatch) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 3), t(3, 3);
    s.setZero(); t.setZero();
    Eigen::VectorXd w(5);
    w.setOnes();
    auto result = align_landmarks(s, t, w);
    EXPECT_FALSE(result.success);
}
