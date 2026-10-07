// Fail-safe tests: RPS failure must produce INVALID, never ICP fallback.

#include "test_harness_gt.h"
#include "alignmesh/service/result_package.h"
#include "alignmesh/io/stl_io.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <iostream>

using namespace alignmesh;
using namespace alignmesh::test_harness;

static std::string temp_stl(const geometry::TriangleMesh& mesh, const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "alignmesh_failsafe";
    std::filesystem::create_directories(dir);
    auto path = (dir / (name + ".stl")).string();
    io::export_stl_binary(mesh, path);
    return path;
}

// ============================================================================
// RPS points off the geometry → INVALID (not ICP fallback)
// ============================================================================

TEST(RPSFailSafe, ProjectionFailure_InvalidNotFallback) {
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto T_gt = make_known_transform(2.0, -1.0, 0.5, 0.5, -0.3, 0.2);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = temp_stl(ref_mesh, "fs_ref");
    auto meas_path = temp_stl(meas_mesh, "fs_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";

    // RPS points DELIBERATELY off the geometry (at 999,999,999).
    // Projection will find zero neighbors → RPS must fail → INVALID.
    req.rps_points = {
        {999, 999, 999, {{"z", 1.0}}},
        {998, 999, 999, {{"z", 1.0}}},
        {999, 998, 999, {{"z", 1.0}}},
        {999, 999, 998, {{"y", 1.0}}},
        {998, 999, 998, {{"y", 1.0}}},
        {999, 998, 998, {{"x", 1.0}}},
    };

    auto pkg = service::run_inspection(req);

    std::cout << "ProjectionFailure: verdict=" << pkg.verdict_label
              << " basis=" << pkg.alignment_basis << "\n";
    for (auto& w : pkg.warnings)
        if (w.find("RPS") != std::string::npos)
            std::cout << "  " << w.substr(0, 120) << "\n";

    // MUST be INVALID — NOT a PASS/FAIL from an ICP fallback.
    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "RPS projection failure must produce INVALID, not a confident verdict from ICP fallback";

    // The alignment_basis must indicate RPS, not coarse-to-fine.
    EXPECT_TRUE(pkg.alignment_basis.find("RPS") != std::string::npos)
        << "Alignment basis must mention RPS (not 'coarse-to-fine'): " << pkg.alignment_basis;
    EXPECT_TRUE(pkg.alignment_basis.find("FAILED") != std::string::npos)
        << "Alignment basis must indicate failure: " << pkg.alignment_basis;

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// Valid RPS case still works (no regression)
// ============================================================================

TEST(RPSFailSafe, ValidCase_StillWorks) {
    // Solver-level test: known correspondences, rank-6, should succeed.
    auto T_gt = make_known_transform(5.0, -3.0, 2.0, 1.0, -0.5, 0.3);

    std::vector<Eigen::Vector3d> nominal = {
        {10, 10, 0}, {30, 10, 0}, {20, 25, 0},
        {10, 0, 10}, {30, 0, 10}, {0, 15, 10},
    };
    std::vector<registration::RPSConstraintPoint> constraints;
    std::vector<Eigen::Vector3d> dirs = {
        {0,0,-1}, {0,0,-1}, {0,0,-1}, {0,-1,0}, {0,-1,0}, {-1,0,0}};
    for (std::size_t i = 0; i < nominal.size(); ++i) {
        registration::RPSConstraintPoint cp;
        cp.nominal = nominal[i];
        cp.measured = T_gt.inverse().apply(nominal[i]);
        cp.locks.push_back({dirs[i], 1.0});
        constraints.push_back(cp);
    }

    auto result = registration::rps_align(constraints, geometry::RigidTransform());
    ASSERT_TRUE(result.success) << "Valid rank-6 RPS must succeed";

    double t_err = (T_gt.translation() - result.transform.translation()).norm();
    EXPECT_LT(t_err, 1e-8) << "Valid case must recover T_gt";
    std::cout << "ValidCase: t_err=" << t_err << " rms=" << result.weighted_rms << "\n";
}

// ============================================================================
// PARTIAL failure: 2 of 6 points fail → INVALID (not proceed with 4)
// ============================================================================

TEST(RPSFailSafe, PartialProjectionFailure_InvalidNotDegrade) {
    // Cylinder ref: radius=20, height=40, centered at origin.
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto meas_mesh = ref_mesh;  // identity alignment — scan == reference

    auto ref_path = temp_stl(ref_mesh, "pf_ref");
    auto meas_path = temp_stl(meas_mesh, "pf_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";

    // 4 points ON the cylinder surface (radius 20, within height [0,40])
    // 2 points DELIBERATELY off-geometry at (999,999,999)
    req.rps_points = {
        {20, 0,  10, {{"x", 1.0}}},   // on surface (x=20, y=0, z=10)
        {0,  20, 10, {{"y", 1.0}}},   // on surface (x=0, y=20, z=10)
        {-20, 0, 30, {{"x", 1.0}}},   // on surface (x=-20, y=0, z=30)
        {0, -20, 30, {{"y", 1.0}}},   // on surface
        {999, 999, 999, {{"z", 1.0}}}, // OFF GEOMETRY — will fail projection
        {998, 998, 998, {{"z", 1.0}}}, // OFF GEOMETRY — will fail projection
    };

    auto pkg = service::run_inspection(req);

    std::cout << "PartialFailure: verdict=" << pkg.verdict_label
              << " basis=" << pkg.alignment_basis << "\n";
    for (auto& w : pkg.warnings)
        if (w.find("RPS") != std::string::npos)
            std::cout << "  " << w.substr(0, 120) << "\n";

    // MUST be INVALID — partial failure is silent degradation if it proceeds.
    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "Partial RPS failure (2/6 off-geometry) must produce INVALID, "
           "not silently proceed with 4 constraints";

    // Alignment basis must indicate RPS failure.
    EXPECT_TRUE(pkg.alignment_basis.find("FAILED") != std::string::npos)
        << "Alignment basis must indicate failure: " << pkg.alignment_basis;

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// Repro-gate
// ============================================================================

TEST(RPSFailSafe, ReproGate) {
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto meas_mesh = ref_mesh;

    auto ref_path = temp_stl(ref_mesh, "rg_ref");
    auto meas_path = temp_stl(meas_mesh, "rg_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";
    req.rps_points = {
        {999, 999, 999, {{"z", 1.0}}},
        {998, 999, 999, {{"z", 1.0}}},
        {999, 998, 999, {{"z", 1.0}}},
        {999, 999, 998, {{"y", 1.0}}},
        {998, 999, 998, {{"y", 1.0}}},
        {999, 998, 998, {{"x", 1.0}}},
    };

    auto p1 = service::run_inspection(req);
    auto p2 = service::run_inspection(req);

    EXPECT_EQ(p1.verdict_label, p2.verdict_label);
    EXPECT_EQ(p1.verdict_label, "INVALID");

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}
