#include <gtest/gtest.h>
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/numerics/seeded_rng.h"
#include "alignmesh/spatial/normals.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace alignmesh::registration;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Generate a synthetic hemisphere point cloud with normals.
PointCloud make_hemisphere(int n_theta, int n_phi, double radius = 1.0) {
    int total = n_theta * n_phi;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, total);
    Eigen::Matrix<double, 3, Eigen::Dynamic> nrm(3, total);
    int idx = 0;
    for (int i = 0; i < n_theta; ++i) {
        double theta = (kPi / 2.0) * (i + 0.5) / n_theta;  // 0 to pi/2
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

// Add Gaussian noise to a point cloud.
PointCloud add_noise(const PointCloud& pc, SeededRng& rng, double sigma) {
    std::normal_distribution<double> noise(0.0, sigma);
    Eigen::Matrix<double, 3, Eigen::Dynamic> noisy = pc.points();
    for (Eigen::Index i = 0; i < noisy.cols(); ++i) {
        noisy(0, i) += noise(rng);
        noisy(1, i) += noise(rng);
        noisy(2, i) += noise(rng);
    }
    // Re-estimate normals if present
    if (pc.has_normals()) {
        auto norm_result = alignmesh::spatial::estimate_normals_knn(noisy, 15);
        return PointCloud(std::move(noisy), std::move(norm_result.normals), std::nullopt);
    }
    return PointCloud(std::move(noisy));
}

} // namespace

// ============================================================================
// Ground truth: known small transform recovery
// ============================================================================

TEST(FineRegistration, RecoverKnownSmallTransform) {
    auto reference = make_hemisphere(30, 60);  // 1800 pts (denser)

    // Small known perturbation (~3 deg rotation + small translation).
    Mat3 R = Eigen::AngleAxisd(0.05, Vec3(0.3, 0.4, 0.5).normalized())
                 .toRotationMatrix();
    Vec3 t(0.01, -0.01, 0.005);
    auto T_true = RigidTransform::from_rotation_translation(R, t);

    auto moving = reference.transformed(T_true);

    FineRegistrationSettings settings;
    settings.method = ICPMethod::POINT_TO_PLANE;
    settings.max_correspondence_distance = 0.3;
    settings.max_iterations = 100;

    auto result = fine_register(moving, reference, RigidTransform(), settings);
    ASSERT_TRUE(result.success) << "Registration failed";

    auto T_composed = result.transform * T_true;
    double err = (T_composed.matrix() - Mat4::Identity()).norm();
    EXPECT_LT(err, 0.1)
        << "Recovery error: " << err;
}

TEST(FineRegistration, MultiLevelResidualsDecrease) {
    auto reference = make_hemisphere(20, 40);
    Mat3 R = Eigen::AngleAxisd(0.1, Vec3::UnitZ()).toRotationMatrix();
    auto T_true = RigidTransform::from_rotation_translation(R, Vec3(0.05, 0, 0));
    auto moving = reference.transformed(T_true);

    FineRegistrationSettings settings;
    settings.method = ICPMethod::POINT_TO_PLANE;
    settings.voxel_schedule = {0.15, 0.05};
    settings.distance_schedule = {0.5, 0.2};

    auto result = fine_register(moving, reference, RigidTransform(), settings);
    ASSERT_TRUE(result.success);
    ASSERT_GE(result.iterations.size(), 2u);

    // Finer level should have lower error than coarser level.
    EXPECT_LE(result.iterations.back().error,
              result.iterations.front().error + 1e-6);
}

// ============================================================================
// Determinism
// ============================================================================

TEST(FineRegistration, Determinism) {
    auto reference = make_hemisphere(15, 30);
    Mat3 R = Eigen::AngleAxisd(0.1, Vec3::UnitX()).toRotationMatrix();
    auto moving = reference.transformed(
        RigidTransform::from_rotation_translation(R, Vec3(0.05, 0, 0)));

    FineRegistrationSettings settings;
    settings.method = ICPMethod::GICP;
    settings.max_correspondence_distance = 0.5;

    auto r1 = fine_register(moving, reference, RigidTransform(), settings);
    auto r2 = fine_register(moving, reference, RigidTransform(), settings);
    ASSERT_TRUE(r1.success && r2.success);

    // Bit-identical transform.
    EXPECT_EQ(r1.transform.matrix(), r2.transform.matrix());
    EXPECT_EQ(r1.final_rms, r2.final_rms);
}

// ============================================================================
// Multi-resolution recovers larger offset
// ============================================================================

TEST(FineRegistration, MultiResolutionLargerOffset) {
    auto reference = make_hemisphere(20, 40);

    // Larger perturbation (15 deg + 0.2 translation).
    Mat3 R = Eigen::AngleAxisd(0.26, Vec3::UnitZ()).toRotationMatrix();
    auto T_true = RigidTransform::from_rotation_translation(R, Vec3(0.1, 0.15, 0));
    auto moving = reference.transformed(T_true);

    // Single resolution might fail at this offset.
    FineRegistrationSettings single_settings;
    single_settings.method = ICPMethod::POINT_TO_PLANE;
    single_settings.max_correspondence_distance = 0.5;

    // Multi-resolution should succeed.
    FineRegistrationSettings multi_settings = single_settings;
    multi_settings.voxel_schedule = {0.2, 0.1, 0.05};
    multi_settings.distance_schedule = {1.0, 0.5, 0.2};

    auto r_multi = fine_register(moving, reference, RigidTransform(), multi_settings);
    EXPECT_TRUE(r_multi.success) << "Multi-res should handle larger offset";
    if (r_multi.success) {
        auto T_composed = r_multi.transform * T_true;
        // Multi-res should recover reasonably well.
        EXPECT_LT((T_composed.matrix() - Mat4::Identity()).norm(), 0.6);
    }
}

// ============================================================================
// Point-to-plane vs point-to-point
// ============================================================================

TEST(FineRegistration, PointToPlaneConvergesTighter) {
    auto reference = make_hemisphere(15, 30);
    Mat3 R = Eigen::AngleAxisd(0.05, Vec3::UnitZ()).toRotationMatrix();
    auto T_true = RigidTransform::from_rotation_translation(R, Vec3(0.01, 0, 0));
    auto moving = reference.transformed(T_true);

    FineRegistrationSettings p2point;
    p2point.method = ICPMethod::POINT_TO_POINT;
    p2point.max_correspondence_distance = 0.5;

    FineRegistrationSettings p2plane;
    p2plane.method = ICPMethod::POINT_TO_PLANE;
    p2plane.max_correspondence_distance = 0.5;

    auto r_point = fine_register(moving, reference, RigidTransform(), p2point);
    auto r_plane = fine_register(moving, reference, RigidTransform(), p2plane);

    ASSERT_TRUE(r_point.success && r_plane.success);

    // Point-to-plane should achieve lower or equal RMS on a curved surface.
    EXPECT_LE(r_plane.final_rms, r_point.final_rms + 1e-6);
}

// ============================================================================
// Partial overlap
// ============================================================================

TEST(FineRegistration, PartialOverlap70Percent) {
    auto full = make_hemisphere(20, 40);  // 800 pts

    // Take first 560 points (70%) as "moving", all 800 as reference.
    int n_overlap = 560;
    Eigen::Matrix<double, 3, Eigen::Dynamic> mov_pts =
        full.points().leftCols(n_overlap);
    Eigen::Matrix<double, 3, Eigen::Dynamic> mov_nrm =
        full.normals().leftCols(n_overlap);
    PointCloud moving(std::move(mov_pts), std::move(mov_nrm), std::nullopt);

    // Apply small transform.
    Mat3 R = Eigen::AngleAxisd(0.05, Vec3::UnitY()).toRotationMatrix();
    auto T_true = RigidTransform::from_rotation_translation(R, Vec3(0.01, 0, 0));
    moving = moving.transformed(T_true);

    FineRegistrationSettings settings;
    settings.method = ICPMethod::POINT_TO_PLANE;
    settings.max_correspondence_distance = 0.5;

    auto result = fine_register(moving, full, RigidTransform(), settings);
    EXPECT_TRUE(result.success);
    if (result.success) {
        EXPECT_GT(result.overlap_ratio, 0.5);
    }
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(FineRegistrationAdversarial, BadInitialPose) {
    auto reference = make_hemisphere(15, 30);
    // 90° rotation — way outside convergence basin.
    Mat3 R = Eigen::AngleAxisd(kPi / 2, Vec3::UnitZ()).toRotationMatrix();
    auto T_bad = RigidTransform::from_rotation_translation(R, Vec3(2, 0, 0));
    auto moving = reference.transformed(T_bad);

    FineRegistrationSettings settings;
    settings.method = ICPMethod::POINT_TO_PLANE;
    settings.max_correspondence_distance = 0.5;

    auto result = fine_register(moving, reference, RigidTransform(), settings);
    // Should not claim success with tight residuals from a wrong pose.
    // Either fails or has high residuals.
    if (result.success) {
        EXPECT_GT(result.final_rms, 0.01)
            << "Suspiciously low RMS for a 90° initial error";
    }
}

TEST(FineRegistration, EmptyCloud) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> empty(3, 0);
    PointCloud pc_empty(empty);
    auto reference = make_hemisphere(10, 20);

    auto result = fine_register(pc_empty, reference, RigidTransform());
    EXPECT_FALSE(result.success);
}
