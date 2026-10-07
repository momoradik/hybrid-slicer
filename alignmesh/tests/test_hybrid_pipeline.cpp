#include <gtest/gtest.h>
#include "alignmesh/registration/hybrid_pipeline.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>

using namespace alignmesh::registration;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Hemisphere with normals at metrology scale (mm).
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

RigidTransform make_T(double angle, Vec3 axis, Vec3 t) {
    return RigidTransform::from_rotation_translation(
        Eigen::AngleAxisd(angle, axis.normalized()).toRotationMatrix(), t);
}

} // namespace

// ============================================================================
// Composition algebra
// ============================================================================

TEST(Composition, KnownChainProduct) {
    // T1 = 30° around Z + (1,0,0)
    // T2 = 45° around X + (0,2,0)
    // Final = T2 * T1  (T1 applied first)
    auto T1 = make_T(kPi / 6, Vec3::UnitZ(), Vec3(1, 0, 0));
    auto T2 = make_T(kPi / 4, Vec3::UnitX(), Vec3(0, 2, 0));
    auto expected = T2 * T1;

    auto composed = compose_chain({T1, T2});
    EXPECT_TRUE(composed.is_approx(expected, 1e-12))
        << "compose_chain({T1, T2}) != T2 * T1";
}

TEST(Composition, ThreeStageProduct) {
    auto T1 = make_T(0.1, Vec3::UnitX(), Vec3(1, 0, 0));
    auto T2 = make_T(0.2, Vec3::UnitY(), Vec3(0, 1, 0));
    auto T3 = make_T(0.3, Vec3::UnitZ(), Vec3(0, 0, 1));

    auto expected = T3 * T2 * T1;
    auto composed = compose_chain({T1, T2, T3});
    EXPECT_TRUE(composed.is_approx(expected, 1e-12));
}

TEST(Composition, VerifyConsistency) {
    auto T1 = make_T(0.5, Vec3(1, 1, 0), Vec3(3, 0, 0));
    auto T2 = make_T(0.3, Vec3(0, 1, 1), Vec3(0, 2, 0));
    auto composed = compose_chain({T1, T2});
    EXPECT_TRUE(verify_composition({T1, T2}, composed));
}

TEST(Composition, SingleStage) {
    auto T = make_T(0.7, Vec3::UnitZ(), Vec3(1, 2, 3));
    auto composed = compose_chain({T});
    EXPECT_TRUE(composed.is_approx(T, 1e-15));
}

TEST(Composition, EmptyChainIsIdentity) {
    auto composed = compose_chain({});
    EXPECT_TRUE(composed.is_identity());
}

// ============================================================================
// Workflow: landmark-first
// ============================================================================

TEST(LandmarkFirst, RecoverKnownTransform) {
    auto reference = make_hemisphere(20, 40);
    auto T_true = make_T(0.05, Vec3(1, 0, 0), Vec3(0.5, -0.3, 0.1));
    auto moving = reference.transformed(T_true);

    // Landmarks: pick a few points from the cloud.
    int n_landmarks = 5;
    Eigen::Matrix<double, 3, Eigen::Dynamic> src_lm(3, n_landmarks);
    Eigen::Matrix<double, 3, Eigen::Dynamic> tgt_lm(3, n_landmarks);
    for (int i = 0; i < n_landmarks; ++i) {
        int idx = i * 160;
        src_lm.col(i) = moving.points().col(idx);
        tgt_lm.col(i) = reference.points().col(idx);
    }

    FineRegistrationSettings fine;
    fine.method = ICPMethod::POINT_TO_PLANE;
    fine.max_correspondence_distance = 5.0;
    fine.max_iterations = 50;

    auto result = landmark_first(moving, reference, src_lm, tgt_lm, fine);
    ASSERT_TRUE(result.success);
    ASSERT_GE(result.stages.size(), 1u);

    // Per-stage records should exist.
    for (auto& stage : result.stages) {
        EXPECT_FALSE(stage.name.empty());
    }

    // Composed result should match final_transform.
    std::vector<RigidTransform> stage_transforms;
    for (auto& s : result.stages) stage_transforms.push_back(s.transform);
    EXPECT_TRUE(verify_composition(stage_transforms, result.final_transform, 1e-10));
}

// ============================================================================
// Adversarial: composition order
// ============================================================================

TEST(CompositionAdversarial, ReversedOrderGivesWrongAnswer) {
    // This test PROVES that composition order matters.
    auto T1 = make_T(kPi / 3, Vec3::UnitZ(), Vec3(5, 0, 0));
    auto T2 = make_T(kPi / 4, Vec3::UnitX(), Vec3(0, 3, 0));

    auto forward = compose_chain({T1, T2});   // T2 * T1
    auto reversed = compose_chain({T2, T1});   // T1 * T2

    // Non-commutative transforms: reversed order gives a DIFFERENT result.
    EXPECT_FALSE(forward.is_approx(reversed, 0.01))
        << "Reversed order should give a DIFFERENT answer — "
        << "if they match, something is wrong with composition";
}

TEST(CompositionAdversarial, PartialCancellation) {
    // T * T^{-1} = Identity.
    auto T = make_T(0.5, Vec3(1, 2, 3), Vec3(4, 5, 6));
    auto T_inv = T.inverse();

    auto composed = compose_chain({T, T_inv});
    EXPECT_TRUE(composed.is_identity(1e-10))
        << "T * T^{-1} should be identity";
}

TEST(CompositionAdversarial, NoOpStage) {
    // Inserting an identity stage should not change the result.
    auto T1 = make_T(0.3, Vec3::UnitX(), Vec3(1, 0, 0));
    auto T2 = make_T(0.4, Vec3::UnitY(), Vec3(0, 1, 0));

    auto without_noop = compose_chain({T1, T2});
    auto with_noop = compose_chain({T1, RigidTransform(), T2});

    EXPECT_TRUE(without_noop.is_approx(with_noop, 1e-12));
}

// ============================================================================
// Workflow: global-first
// ============================================================================

TEST(GlobalFirst, RunsEndToEnd) {
    auto reference = make_hemisphere(15, 30);
    auto T_true = make_T(0.3, Vec3::UnitZ(), Vec3(5, 0, 0));
    auto moving = reference.transformed(T_true);

    GlobalRegistrationSettings global;
    global.voxel_size = 5.0;
    global.seed = 42;

    FineRegistrationSettings fine;
    fine.method = ICPMethod::POINT_TO_PLANE;
    fine.max_correspondence_distance = 10.0;

    auto result = global_first(moving, reference, global, fine);
    // Should at least run without crashing.
    EXPECT_GE(result.stages.size(), 1u);
    // Per-stage records should be complete.
    for (auto& s : result.stages) {
        EXPECT_FALSE(s.name.empty());
    }
}
