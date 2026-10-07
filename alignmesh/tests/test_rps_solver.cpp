// Solver-level tests for the RPS simultaneous weighted directional LS solver.
//
// Proves the solver is EXACTLY the specified objective:
//   min_{R,t} sum_i sum_k w_{i,k} * [d_{i,k} . ((R*m_i+t) - p_i)]^2
//
// MANDATORY — no disabled/skipped tests, no loosened tolerances.
// Fail-first/mutation proof required for RPSDiffersFromKabsch.

#include "test_harness_gt.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <gtest/gtest.h>
#include <Eigen/Dense>
#include <cmath>
#include <iostream>
#include <numbers>

using namespace alignmesh;
using namespace alignmesh::test_harness;
using namespace alignmesh::registration;

static constexpr double kPi = std::numbers::pi;

namespace {

// Build constraint points from ground-truth correspondences.
// nominal[i] = reference point, measured[i] = raw measured point.
std::vector<RPSConstraintPoint> build_constraints(
        const std::vector<Eigen::Vector3d>& nominal,
        const std::vector<Eigen::Vector3d>& measured,
        const std::vector<std::vector<RPSDirectionLock>>& locks) {
    std::vector<RPSConstraintPoint> pts;
    for (std::size_t i = 0; i < nominal.size(); ++i) {
        RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = measured[i];
        cp.locks = locks[i];
        pts.push_back(cp);
    }
    return pts;
}

// Create a rank-6 constraint set on a box with known ground truth.
// Returns {constraints, T_gt} where T_gt maps measured → reference.
struct Rank6Setup {
    std::vector<RPSConstraintPoint> constraints;
    geometry::RigidTransform T_gt;
};

Rank6Setup make_rank6_setup(double rot_deg = 20.0, double tx = 5.0) {
    // 6-point 3-2-1 setup on a box.
    // Reference points on the box faces.
    std::vector<Eigen::Vector3d> nominal = {
        {15, 10, 0},   // A1: -Z face
        {35, 10, 0},   // A2: -Z face
        {25, 25, 0},   // A3: -Z face
        {15, 0, 10},   // B1: -Y face
        {35, 0, 10},   // B2: -Y face
        {0, 15, 10},   // C1: -X face
    };

    std::vector<std::vector<RPSDirectionLock>> locks = {
        {{Eigen::Vector3d(0,0,-1), 1.0}},  // A1: Z lock
        {{Eigen::Vector3d(0,0,-1), 1.0}},  // A2: Z lock
        {{Eigen::Vector3d(0,0,-1), 1.0}},  // A3: Z lock
        {{Eigen::Vector3d(0,-1,0), 1.0}},  // B1: Y lock
        {{Eigen::Vector3d(0,-1,0), 1.0}},  // B2: Y lock
        {{Eigen::Vector3d(-1,0,0), 1.0}},  // C1: X lock
    };

    // Ground-truth transform.
    auto T_gt = make_known_transform(rot_deg, -rot_deg * 0.7, rot_deg * 0.5,
                                      tx, -tx * 0.6, tx * 0.3);

    // Measured points = T_gt^{-1} * nominal (in the measured frame).
    auto T_inv = T_gt.inverse();
    std::vector<Eigen::Vector3d> measured;
    for (auto& p : nominal) {
        measured.push_back(T_inv.apply(p));
    }

    Rank6Setup setup;
    setup.constraints = build_constraints(nominal, measured, locks);
    setup.T_gt = T_gt;
    return setup;
}

} // namespace

// ============================================================================
// WeightsAreLive: changing one weight changes the over-determined result
// ============================================================================

TEST(RPSSolver, WeightsAreLive) {
    // Over-determined config with INCONSISTENT correspondences (noise),
    // so the weighted LS trade-off actually matters.
    auto setup = make_rank6_setup(15.0, 3.0);

    // Add a 7th constraint: extra Z-lock with a deliberately noisy measured point.
    RPSConstraintPoint extra;
    extra.nominal = Eigen::Vector3d(25, 10, 0);
    extra.measured = setup.T_gt.inverse().apply(extra.nominal);
    extra.measured.z() += 0.5;  // 0.5mm noise in Z → creates a trade-off
    extra.locks.push_back({Eigen::Vector3d(0, 0, -1), 1.0});
    setup.constraints.push_back(extra);

    // Solve with equal weights.
    auto constraints_w1 = setup.constraints;
    auto result1 = rps_align(constraints_w1, geometry::RigidTransform());
    ASSERT_TRUE(result1.success);
    EXPECT_GT(result1.redundant_constraints, 0)
        << "Must be over-determined";

    // Change the noisy constraint's weight to 100x and solve again.
    auto constraints_w2 = setup.constraints;
    constraints_w2.back().locks[0].weight = 100.0;
    auto result2 = rps_align(constraints_w2, geometry::RigidTransform());
    ASSERT_TRUE(result2.success);

    // The results must DIFFER (weights are live, not ignored).
    auto m1 = result1.transform.matrix();
    auto m2 = result2.transform.matrix();
    double diff = (m1 - m2).norm();

    std::cout << "WeightsAreLive: transform diff = " << diff
              << " rms1=" << result1.weighted_rms
              << " rms2=" << result2.weighted_rms << "\n";
    EXPECT_GT(diff, 1e-6)
        << "Changing a weight in an over-determined system with noise must change the result";
}

// ============================================================================
// RPSDiffersFromKabsch: directional LS != full-3D Kabsch
// ============================================================================
// A point locked in ONE axis only: the RPS solver constrains only that
// direction, while Kabsch minimizes full 3D distance. They must differ.
//
// FAIL-FIRST: routing through Kabsch makes this test go RED.

TEST(RPSSolver, RPSDiffersFromKabsch) {
    // The RPS directional solver and Kabsch MUST give different answers when
    // measured points have errors in non-locked directions. RPS ignores
    // non-locked components; Kabsch minimizes full 3D distance.
    //
    // Setup: rank-6 config with exact correspondences for a known T_gt,
    // PLUS deliberate cross-direction offsets on Z-locked points.
    // The Z-locked points have XY errors that Kabsch tries to reduce
    // but RPS correctly ignores (only Z matters for those constraints).

    auto T_gt = make_known_transform(5.0, -3.0, 2.0, 1.0, -0.5, 0.3);

    std::vector<Eigen::Vector3d> nominal = {
        {10, 10, 0}, {30, 10, 0}, {20, 25, 0},  // Z-locked
        {10, 0, 10}, {30, 0, 10},                 // Y-locked
        {0, 15, 10},                               // X-locked
    };
    std::vector<std::vector<RPSDirectionLock>> locks = {
        {{Eigen::Vector3d(0,0,1), 1.0}},
        {{Eigen::Vector3d(0,0,1), 1.0}},
        {{Eigen::Vector3d(0,0,1), 1.0}},
        {{Eigen::Vector3d(0,1,0), 1.0}},
        {{Eigen::Vector3d(0,1,0), 1.0}},
        {{Eigen::Vector3d(1,0,0), 1.0}},
    };

    auto T_inv = T_gt.inverse();
    std::vector<Eigen::Vector3d> measured;
    for (auto& p : nominal) measured.push_back(T_inv.apply(p));

    // Add cross-direction offsets to Z-locked measured points.
    // These offsets are in X and Y, which Z-locks should ignore.
    measured[0] += Eigen::Vector3d(0.5, -0.3, 0);
    measured[1] += Eigen::Vector3d(-0.4, 0.6, 0);
    measured[2] += Eigen::Vector3d(0.2, 0.1, 0);

    auto constraints = build_constraints(nominal, measured, locks);

    // RPS solve (directional LS).
    auto rps_result = rps_align(constraints, geometry::RigidTransform());
    ASSERT_TRUE(rps_result.success);

    // Kabsch solve (full-3D LS) on the same point pairs.
    Eigen::Matrix<double, 3, Eigen::Dynamic> src(3, 6), tgt(3, 6);
    for (int i = 0; i < 6; ++i) {
        src.col(i) = measured[static_cast<std::size_t>(i)];
        tgt.col(i) = nominal[static_cast<std::size_t>(i)];
    }
    auto kabsch_result = alignment::align_landmarks(src, tgt);
    ASSERT_TRUE(kabsch_result.success);

    // Transforms must DIFFER: Kabsch tries to fit the XY noise on Z-locked
    // points, RPS ignores it.
    auto rps_mat = rps_result.transform.matrix();
    auto kabsch_mat = kabsch_result.transform.matrix();
    double diff = (rps_mat - kabsch_mat).norm();

    std::cout << "RPSDiffersFromKabsch: diff = " << diff << "\n";
    std::cout << "  RPS translation:    " << rps_result.transform.translation().transpose() << "\n";
    std::cout << "  Kabsch translation: " << kabsch_result.transform.translation().transpose() << "\n";

    EXPECT_GT(diff, 0.001)
        << "RPS directional solve must differ from Kabsch full-3D solve "
           "when measured points have cross-direction errors";

    // RPS Z-residuals on the Z-locked points should still be near-zero
    // (Z direction is correctly constrained despite XY noise).
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(std::abs(rps_result.residuals[static_cast<std::size_t>(i)]
                        .directional_residuals[0]), 0.0, 0.01)
            << "Z-lock residual should be small for point " << i;
    }
}

// ============================================================================
// DOFRank: rank-5 → INVALID, parallel normals → fails, rank-6 → solves
// ============================================================================

TEST(RPSSolver, DOFRank_Rank5_Invalid) {
    auto box = make_box();
    auto config = under_constrained_rps_config(box);

    // Set measured = nominal (identity alignment, just testing the rank gate).
    for (auto& cp : config.constraints) cp.measured = cp.nominal;

    auto result = rps_align(config.constraints, geometry::RigidTransform());

    EXPECT_FALSE(result.success)
        << "Rank-5 config must fail (INVALID)";
    EXPECT_FALSE(result.errors.empty());
    EXPECT_TRUE(result.errors[0].find("Under-constrained") != std::string::npos)
        << "Error must mention under-constrained: " << result.errors[0];
    EXPECT_EQ(result.dof_status.rank, 5);

    std::cout << "DOFRank_Rank5: rank=" << result.dof_status.rank
              << " error: " << result.errors[0] << "\n";
}

TEST(RPSSolver, DOFRank_ParallelNormals_Fail) {
    // All lock directions parallel (Z only) → rank 3, not 6.
    std::vector<RPSConstraintPoint> constraints;
    for (int i = 0; i < 6; ++i) {
        RPSConstraintPoint cp;
        cp.nominal = Eigen::Vector3d(i * 10.0, 0, 0);
        cp.measured = cp.nominal;
        cp.locks.push_back({Eigen::Vector3d(0, 0, 1), 1.0});
        constraints.push_back(cp);
    }

    auto result = rps_align(constraints, geometry::RigidTransform());

    EXPECT_FALSE(result.success);
    EXPECT_LT(result.dof_status.rank, 6);
    std::cout << "DOFRank_Parallel: rank=" << result.dof_status.rank << "\n";
}

TEST(RPSSolver, DOFRank_Rank6_Solves) {
    auto box = make_box();
    auto config = full_rank6_rps_config(box);

    for (auto& cp : config.constraints) cp.measured = cp.nominal;

    auto result = rps_align(config.constraints, geometry::RigidTransform());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.dof_status.rank, 6);
    EXPECT_NEAR(result.weighted_rms, 0.0, 1e-12)
        << "Identity case (measured==nominal) should give zero residuals";
    std::cout << "DOFRank_Rank6: rank=" << result.dof_status.rank
              << " rms=" << result.weighted_rms << "\n";
}

// ============================================================================
// T-RECOVER: known non-trivial T_gt, recover from solver
// ============================================================================

TEST(RPSSolver, TransformRecovery) {
    // Use a 20° rotation + 5mm translation — non-trivial.
    auto setup = make_rank6_setup(20.0, 5.0);

    // Initialize from a rough guess (centroid-aligned identity).
    // The solver should converge from this.
    auto result = rps_align(setup.constraints, geometry::RigidTransform());
    ASSERT_TRUE(result.success);

    // Check recovery.
    auto R_gt = setup.T_gt.rotation();
    auto t_gt = setup.T_gt.translation();
    auto R_sol = result.transform.rotation();
    auto t_sol = result.transform.translation();

    double rot_err = (R_gt - R_sol).norm();
    double t_err = (t_gt - t_sol).norm();

    std::cout << "TransformRecovery: rot_err=" << rot_err
              << " t_err=" << t_err << " rms=" << result.weighted_rms
              << " iters=" << result.iterations << "\n";
    std::cout << "  T_gt.t = " << t_gt.transpose() << "\n";
    std::cout << "  T_sol.t = " << t_sol.transpose() << "\n";

    EXPECT_LT(rot_err, 1e-8) << "Rotation recovery error too large";
    EXPECT_LT(t_err, 1e-8) << "Translation recovery error too large";
    EXPECT_NEAR(result.weighted_rms, 0.0, 1e-10) << "Residuals should be zero for exact correspondences";
}

TEST(RPSSolver, TransformRecovery_LargeAngle) {
    // 30° rotation + 10mm translation — larger than typical but within
    // the solver's convergence basin.
    auto setup = make_rank6_setup(30.0, 10.0);

    auto result = rps_align(setup.constraints, geometry::RigidTransform());
    ASSERT_TRUE(result.success);

    double t_err = (setup.T_gt.translation() - result.transform.translation()).norm();
    double rot_err = (setup.T_gt.rotation() - result.transform.rotation()).norm();

    std::cout << "TransformRecovery_LargeAngle: rot_err=" << rot_err
              << " t_err=" << t_err << " iters=" << result.iterations << "\n";

    EXPECT_LT(rot_err, 1e-6) << "Rotation recovery";
    EXPECT_LT(t_err, 1e-6) << "Translation recovery";
}

// ============================================================================
// Repro-gate: two runs → identical output
// ============================================================================

TEST(RPSSolver, ReproGate) {
    auto setup = make_rank6_setup(15.0, 3.0);

    auto result1 = rps_align(setup.constraints, geometry::RigidTransform());
    auto result2 = rps_align(setup.constraints, geometry::RigidTransform());

    ASSERT_TRUE(result1.success);
    ASSERT_TRUE(result2.success);

    auto m1 = result1.transform.matrix();
    auto m2 = result2.transform.matrix();

    EXPECT_EQ(m1, m2) << "Two runs must be bit-identical";
    EXPECT_EQ(result1.weighted_rms, result2.weighted_rms);
    EXPECT_EQ(result1.iterations, result2.iterations);

    std::cout << "ReproGate: bit-identical=" << (m1 == m2 ? "yes" : "NO") << "\n";
}
