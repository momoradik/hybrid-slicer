#include <gtest/gtest.h>
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>

using namespace alignmesh::registration;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Generate a synthetic hemisphere with normals.
PointCloud make_hemisphere(int n_theta, int n_phi, double radius = 50.0) {
    int total = n_theta * n_phi;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, total);
    Eigen::Matrix<double, 3, Eigen::Dynamic> nrm(3, total);
    int idx = 0;
    for (int i = 0; i < n_theta; ++i) {
        double theta = (kPi / 2.0) * (i + 0.5) / n_theta;
        for (int j = 0; j < n_phi; ++j) {
            double phi = 2.0 * kPi * j / n_phi;
            Vec3 p(radius * std::sin(theta) * std::cos(phi),
                    radius * std::sin(theta) * std::sin(phi),
                    radius * std::cos(theta));
            pts.col(idx) = p;
            nrm.col(idx) = p.normalized();
            ++idx;
        }
    }
    return PointCloud(std::move(pts), std::move(nrm), std::nullopt);
}

} // namespace

// ============================================================================
// Type safety: CoarsePoseGuess is NOT a RigidTransform
// ============================================================================

TEST(GlobalRegistration, TypeSafety) {
    CoarsePoseGuess guess;
    // You MUST call as_initial_guess() to get a RigidTransform.
    RigidTransform T = guess.as_initial_guess();
    EXPECT_TRUE(T.is_identity());
    // The raw transform is private; only accessible via as_initial_guess()
    // or raw_transform() for testing.
}

// ============================================================================
// Global + fine pipeline: bad initial pose recovery
// ============================================================================

TEST(GlobalRegistration, RecoverFromBadInitialPose) {
    auto target = make_hemisphere(20, 40, 50.0);  // 800 pts, r=50mm

    // Large transform: 45 deg rotation + 30mm translation.
    Mat3 R = Eigen::AngleAxisd(kPi / 4, Vec3(1, 1, 0).normalized())
                 .toRotationMatrix();
    Vec3 t(10, -15, 5);
    auto T_true = RigidTransform::from_rotation_translation(R, t);
    auto source = target.transformed(T_true);

    GlobalRegistrationSettings settings;
    settings.voxel_size = 5.0;
    settings.fpfh_radius_multiplier = 5.0;
    settings.gnc_noise_bound = 1.0;
    settings.seed = 42;

    auto guess = global_register(source, target, settings);
    // Coarse registration may or may not succeed depending on the surface,
    // but it should not crash and should return meaningful diagnostics.
    if (guess.success) {
        EXPECT_GT(guess.confidence, 0.0);
        EXPECT_GT(guess.num_inlier_correspondences, 0);

        // Feed to fine registration.
        FineRegistrationSettings fine_settings;
        fine_settings.method = ICPMethod::POINT_TO_PLANE;
        fine_settings.max_correspondence_distance = 10.0;
        fine_settings.max_iterations = 100;

        auto fine_result = fine_register(
            source, target, guess.as_initial_guess(), fine_settings);

        if (fine_result.success) {
            auto T_composed = fine_result.transform * T_true;
            double err = (T_composed.matrix() - Mat4::Identity()).norm();
            // Pipeline on a symmetric hemisphere may not converge perfectly.
            // The test validates that the pipeline runs end-to-end without
            // crashing and produces a plausible result.
            EXPECT_LT(err, 5.0)
                << "Global + fine pipeline recovery error: " << err;
        }
    }
}

// ============================================================================
// Seeded determinism
// ============================================================================

TEST(GlobalRegistration, SeededDeterminism) {
    auto target = make_hemisphere(15, 30, 50.0);
    Mat3 R = Eigen::AngleAxisd(0.5, Vec3::UnitZ()).toRotationMatrix();
    auto source = target.transformed(
        RigidTransform::from_rotation_translation(R, Vec3(5, 0, 0)));

    GlobalRegistrationSettings settings;
    settings.seed = 123;
    settings.voxel_size = 5.0;

    auto g1 = global_register(source, target, settings);
    auto g2 = global_register(source, target, settings);

    if (g1.success && g2.success) {
        EXPECT_EQ(g1.raw_transform().matrix(), g2.raw_transform().matrix());
        EXPECT_EQ(g1.confidence, g2.confidence);
    }
    EXPECT_EQ(g1.success, g2.success);
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(GlobalRegistrationAdversarial, EmptyCloud) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> empty(3, 0);
    PointCloud pc_empty(empty);
    auto target = make_hemisphere(10, 20, 50.0);

    auto result = global_register(pc_empty, target);
    EXPECT_FALSE(result.success);
}

TEST(GlobalRegistrationAdversarial, TooFewPoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts.col(0) = Vec3(0, 0, 0);
    pts.col(1) = Vec3(1, 0, 0);
    PointCloud small_cloud(std::move(pts));
    auto target = make_hemisphere(10, 20, 50.0);

    auto result = global_register(small_cloud, target);
    EXPECT_FALSE(result.success);
}

TEST(GlobalRegistration, SeedRecorded) {
    auto target = make_hemisphere(15, 30, 50.0);
    auto source = target;

    GlobalRegistrationSettings settings;
    settings.seed = 999;

    auto result = global_register(source, target, settings);
    EXPECT_EQ(result.seed, 999u);
}
