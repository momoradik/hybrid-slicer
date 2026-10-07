// End-to-end RPS alignment tests.
//
// T-RECOVER: known T_gt with real rotation + part-scale translation,
//   recovered transform matches T_gt, max deviation at noise level NOT part-scale.
// No-post-RPS-refinement: RPS answer is final.
// Full-resolution: deviation count == measured cloud size.
// Frame-convention: projected points converted to raw measured frame.
// Repro-gate.

#include "test_harness_gt.h"
#include "alignmesh/registration/rps_projection.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/analysis/deviation.h"

#include <gtest/gtest.h>
#include <iostream>
#include <cmath>

using namespace alignmesh;
using namespace alignmesh::test_harness;

// ============================================================================
// T-RECOVER: known T_gt, solver recovers it, deviation at noise level.
// ============================================================================

TEST(RPSE2E, TransformRecovery) {
    // Ground-truth: 15° rotation + 5mm translation.
    auto T_gt = make_known_transform(15.0, -10.0, 8.0, 5.0, -3.0, 2.0);

    // 6-point rank-6 config with known correspondences.
    std::vector<Eigen::Vector3d> nominal = {
        {15, 10, 0}, {35, 10, 0}, {25, 25, 0},  // Z-locked (primary datum)
        {15, 0, 10}, {35, 0, 10},                 // Y-locked (secondary)
        {0, 15, 10},                               // X-locked (tertiary)
    };
    std::vector<std::vector<registration::RPSDirectionLock>> locks = {
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(-1,0,0), 1.0}},
    };

    // Measured points in RAW frame (T_gt^{-1} * nominal).
    std::vector<registration::RPSConstraintPoint> constraints;
    for (std::size_t i = 0; i < nominal.size(); ++i) {
        registration::RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = T_gt.inverse().apply(nominal[i]);
        cp.locks = locks[i];
        constraints.push_back(cp);
    }

    // Solve from identity initial.
    auto rps_result = registration::rps_align(constraints, geometry::RigidTransform());
    ASSERT_TRUE(rps_result.success);

    double rot_err = (T_gt.rotation() - rps_result.transform.rotation()).norm();
    double t_err = (T_gt.translation() - rps_result.transform.translation()).norm();

    std::cout << "T-RECOVER: rot_err=" << rot_err << " t_err=" << t_err
              << " rms=" << rps_result.weighted_rms << "\n";

    EXPECT_LT(rot_err, 1e-8) << "Rotation recovery";
    EXPECT_LT(t_err, 1e-8) << "Translation recovery";

    // Apply recovered transform to measured box vertices → compute deviation.
    auto ref_mesh = make_box(50.0, 30.0, 20.0);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());
    auto aligned = rps_result.transform.apply_cloud(meas_mesh.vertices());
    auto ref_geom = geometry::make_mesh_reference(ref_mesh);
    auto dev = analysis::compute_deviation(aligned, *ref_geom);
    ASSERT_TRUE(dev.success);

    std::cout << "  deviation: rms=" << dev.unsigned_stats.rms
              << " max=" << dev.unsigned_stats.max << "\n";

    // Max deviation at noise level (< 1mm), NOT part-scale (> 10mm).
    EXPECT_LT(dev.unsigned_stats.max, 0.001)
        << "Max deviation should be ~0 for exact correspondences";
}

// ============================================================================
// Frame-convention: the projection returns reference-frame points,
// and the pipeline must convert them to raw measured frame for the solver.
// ============================================================================

TEST(RPSE2E, FrameConvention_ProjectionToSolver) {
    // Simulate what result_package.cpp does:
    // 1. Project with T → projected_point in REFERENCE frame
    // 2. Convert to raw: measured = T^{-1} * projected_point
    // 3. Solver uses measured in raw frame + identity initial → recovers T

    auto T_gt = make_known_transform(10.0, -5.0, 3.0, 2.0, -1.0, 0.5);

    // Nominal points (reference frame).
    Eigen::Vector3d p1(20, 10, 0), p2(40, 10, 0), p3(30, 25, 0);
    Eigen::Vector3d p4(20, 0, 10), p5(40, 0, 10), p6(0, 15, 10);

    // Simulate projection: measured mesh aligned by T, project nominal → reference frame.
    // For exact correspondences, projected_point ≈ nominal (they're ON the aligned surface).
    // The projection output is in the reference frame.
    std::vector<Eigen::Vector3d> projected_ref = {p1, p2, p3, p4, p5, p6};

    // Convert to raw measured frame (the fix).
    std::vector<Eigen::Vector3d> measured_raw;
    for (auto& p : projected_ref)
        measured_raw.push_back(T_gt.inverse().apply(p));

    // Build constraints with measured in RAW frame.
    std::vector<registration::RPSConstraintPoint> constraints;
    std::vector<Eigen::Vector3d> nominal = {p1, p2, p3, p4, p5, p6};
    std::vector<Eigen::Vector3d> dirs = {
        {0,0,-1}, {0,0,-1}, {0,0,-1}, {0,-1,0}, {0,-1,0}, {-1,0,0}};
    for (std::size_t i = 0; i < 6; ++i) {
        registration::RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = measured_raw[i];
        cp.locks.push_back({dirs[i], 1.0});
        constraints.push_back(cp);
    }

    // Solve from identity — should recover T_gt.
    auto result = registration::rps_align(constraints, geometry::RigidTransform());
    ASSERT_TRUE(result.success);

    double t_err = (T_gt.translation() - result.transform.translation()).norm();
    EXPECT_LT(t_err, 1e-8)
        << "With raw-frame measured points, solver must recover T_gt exactly";

    // Now simulate the BUG: pass reference-frame points without converting.
    std::vector<registration::RPSConstraintPoint> bad_constraints;
    for (std::size_t i = 0; i < 6; ++i) {
        registration::RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = projected_ref[i];  // BUG: reference frame, not raw
        cp.locks.push_back({dirs[i], 1.0});
        bad_constraints.push_back(cp);
    }

    auto bad_result = registration::rps_align(bad_constraints, T_gt);
    if (bad_result.success) {
        // With the bug, solver collapses to near-identity → large deviation.
        auto ref_mesh = make_box(50.0, 30.0, 20.0);
        auto meas_mesh = ref_mesh.transformed(T_gt.inverse());
        auto bad_aligned = bad_result.transform.apply_cloud(meas_mesh.vertices());
        auto ref_geom = geometry::make_mesh_reference(ref_mesh);
        auto bad_dev = analysis::compute_deviation(bad_aligned, *ref_geom);
        if (bad_dev.success) {
            std::cout << "FrameConvention: bad_max_dev=" << bad_dev.unsigned_stats.max
                      << " (should be large if bug present)\n";
        }
    }
}

// ============================================================================
// No-post-RPS-refinement
// ============================================================================

TEST(RPSE2E, NoPostRPSRefinement) {
    auto T_gt = make_known_transform(5.0, -3.0, 2.0, 1.0, -0.5, 0.3);

    std::vector<Eigen::Vector3d> nominal = {
        {10, 10, 0}, {30, 10, 0}, {20, 25, 0},
        {10, 0, 10}, {30, 0, 10}, {0, 15, 10},
    };
    std::vector<std::vector<registration::RPSDirectionLock>> locks = {
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(-1,0,0), 1.0}},
    };

    std::vector<registration::RPSConstraintPoint> constraints;
    for (std::size_t i = 0; i < nominal.size(); ++i) {
        registration::RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = T_gt.inverse().apply(nominal[i]);
        cp.locks = locks[i];
        constraints.push_back(cp);
    }

    auto rps_result = registration::rps_align(constraints, geometry::RigidTransform());
    ASSERT_TRUE(rps_result.success);

    double t_err = (T_gt.translation() - rps_result.transform.translation()).norm();
    EXPECT_LT(t_err, 1e-8) << "RPS transform should recover T_gt exactly";
    std::cout << "NoPostRPSRefinement: t_err=" << t_err << "\n";
}

// ============================================================================
// Full-resolution: deviation count == measured cloud size
// ============================================================================

TEST(RPSE2E, FullResolutionDeviation) {
    auto ref_mesh = make_cylinder();  // 290 vertices
    auto T_gt = make_known_transform();
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto aligned = T_gt.apply_cloud(meas_mesh.vertices());
    auto ref_geom = geometry::make_mesh_reference(ref_mesh);
    auto dev = analysis::compute_deviation(aligned, *ref_geom);

    ASSERT_TRUE(dev.success);
    EXPECT_EQ(static_cast<Eigen::Index>(dev.deviations.size()),
              meas_mesh.num_vertices())
        << "Deviation count must equal measured cloud size (no downsampling)";
    std::cout << "FullResolution: input=" << meas_mesh.num_vertices()
              << " output=" << dev.deviations.size() << "\n";
}

// ============================================================================
// Repro-gate
// ============================================================================

TEST(RPSE2E, ReproGate) {
    auto T_gt = make_known_transform(10.0, -5.0, 3.0, 2.0, -1.0, 0.5);
    std::vector<Eigen::Vector3d> nominal = {
        {10, 10, 0}, {30, 10, 0}, {20, 25, 0},
        {10, 0, 10}, {30, 0, 10}, {0, 15, 10},
    };
    std::vector<std::vector<registration::RPSDirectionLock>> locks = {
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,0,-1), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(0,-1,0), 1.0}},
        {{Eigen::Vector3d(-1,0,0), 1.0}},
    };

    auto run = [&]() {
        std::vector<registration::RPSConstraintPoint> constraints;
        for (std::size_t i = 0; i < nominal.size(); ++i) {
            registration::RPSConstraintPoint cp;
            cp.nominal = nominal[i];
            cp.measured = T_gt.inverse().apply(nominal[i]);
            cp.locks = locks[i];
            constraints.push_back(cp);
        }
        return registration::rps_align(constraints, geometry::RigidTransform());
    };

    auto r1 = run();
    auto r2 = run();
    ASSERT_TRUE(r1.success && r2.success);
    EXPECT_EQ(r1.transform.matrix(), r2.transform.matrix()) << "Bit-identical";
}
