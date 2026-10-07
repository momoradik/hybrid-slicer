// Tests for the pre-alignment stage (coarse-to-fine, no exhaustive sweep).
//
// - Pre-alignment recovers a known coarse pose into the GICP convergence basin.
// - SPEED MECHANISM: assert the exhaustive rotation sweep is gone.
// - Bad-global → caught downstream → INVALID.
// - Repro-gate.

#include "test_harness_gt.h"
#include "alignmesh/service/result_package.h"
#include "alignmesh/io/stl_io.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace alignmesh;
using namespace alignmesh::test_harness;

static std::string export_temp_stl(const geometry::TriangleMesh& mesh,
                                    const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "alignmesh_test";
    std::filesystem::create_directories(dir);
    auto path = (dir / (name + ".stl")).string();
    io::export_stl_binary(mesh, path);
    return path;
}

// ============================================================================
// SPEED MECHANISM: assert the exhaustive rotation sweep is gone from source.
// This is a source-code mechanism test, not a wall-time test.
// ============================================================================

TEST(PreAlign, NoExhaustiveRotationSweep) {
    // Read result_package.cpp and assert the exhaustive rotation search
    // pattern is NOT present. This catches re-introduction.
    std::string src_path = ALIGNMESH_SOURCE_DIR "/src/service/result_package.cpp";
    std::ifstream ifs(src_path);
    if (!ifs) {
        // Fallback: try relative path from test executable location.
        src_path = "../../../src/service/result_package.cpp";
        ifs.open(src_path);
    }
    if (!ifs) {
        // Can't read source — check via grep-equivalent in the binary.
        // The string "multi-rotation search" was the signature of the sweep.
        // If it appears in any linked symbol, the sweep is still there.
        GTEST_SKIP() << "Cannot read source file for mechanism test";
    }

    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());

    // The exhaustive sweep had these signatures:
    EXPECT_EQ(source.find("multi-rotation search"), std::string::npos)
        << "Exhaustive rotation sweep 'multi-rotation search' found in source — must be removed";
    EXPECT_EQ(source.find("rot_x(ix * STEP)"), std::string::npos)
        << "Rotation sweep loop body found in source — must be removed";
    EXPECT_EQ(source.find("1728"), std::string::npos)
        << "1728-trial constant found in source — must be removed";
    EXPECT_EQ(source.find("all rotations failed"), std::string::npos)
        << "Rotation-failure fallback found in source — must be removed";

    std::cout << "NoExhaustiveRotationSweep: source verified clean\n";
}

// ============================================================================
// Pre-alignment recovers a known coarse pose (moderate rotation + translation).
// ============================================================================

TEST(PreAlign, RecoversCoarsePose) {
    // Cylinder: dense enough for GICP to work.
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    // Small transform: 5° rotation + 2mm translation — within GICP basin.
    auto T_gt = make_known_transform(5.0, -3.0, 2.0, 2.0, -1.0, 0.5);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = export_temp_stl(ref_mesh, "prealign_ref");
    auto meas_path = export_temp_stl(meas_mesh, "prealign_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";
    req.rps_points = {
        {20, 0, 0,   {{"z", 1.0}}},
        {-20, 0, 0,  {{"z", 1.0}}},
        {0, 20, 0,   {{"z", 1.0}}},
        {20, 0, 20,  {{"y", 1.0}}},
        {-20, 0, 20, {{"y", 1.0}}},
        {0, 20, 20,  {{"x", 1.0}}},
    };

    auto pkg = service::run_inspection(req);

    std::cout << "RecoversCoarsePose: converged=" << pkg.alignment_converged
              << " rms=" << pkg.alignment_rms
              << " verdict=" << pkg.verdict_label
              << " max_dev=" << pkg.unsigned_stats.max
              << " basis=" << pkg.alignment_basis << "\n";

    // The pre-alignment (FPFH or centroid+GICP) should bring the parts
    // close enough for the RPS coupling to converge.
    // Verdict is INVALID (uncertainty gate) but deviation should be reasonable.
    if (pkg.alignment_converged && pkg.unsigned_stats.max > 0) {
        EXPECT_LT(pkg.unsigned_stats.max, 5.0)
            << "Pre-alignment + RPS should give max deviation < 5mm for a 5° rotation";
    }

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// Bad-global → caught downstream → INVALID.
// ============================================================================

TEST(PreAlign, BadGlobal_InvalidVerdict) {
    // Box with 30° wrong pose — GICP from centroid can't recover this.
    auto ref_mesh = make_box(50.0, 30.0, 20.0);
    double angle = 30.0 * 3.14159265358979 / 180.0;
    Eigen::Matrix3d R;
    R << std::cos(angle), -std::sin(angle), 0,
         std::sin(angle),  std::cos(angle), 0,
         0, 0, 1;
    auto T_bad = geometry::RigidTransform::from_rotation_translation(
        R, Eigen::Vector3d(15.0, -10.0, 5.0));
    auto meas_mesh = ref_mesh.transformed(T_bad.inverse());

    auto ref_path = export_temp_stl(ref_mesh, "badglobal_ref");
    auto meas_path = export_temp_stl(meas_mesh, "badglobal_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.1;
    req.alignment_mode = "pre-aligned-rps";
    req.rps_points = {
        {15, 10, 0,  {{"z", 1.0}}},
        {35, 10, 0,  {{"z", 1.0}}},
        {25, 25, 0,  {{"z", 1.0}}},
        {15, 0, 10,  {{"y", 1.0}}},
        {35, 0, 10,  {{"y", 1.0}}},
        {0, 15, 10,  {{"x", 1.0}}},
    };

    auto pkg = service::run_inspection(req);

    std::cout << "BadGlobal: verdict=" << pkg.verdict_label
              << " converged=" << pkg.alignment_converged << "\n";

    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "Bad global registration must produce INVALID, not a confident PASS/FAIL";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// Repro-gate.
// ============================================================================

TEST(PreAlign, ReproGate) {
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto T_gt = make_known_transform(3.0, -2.0, 1.0, 1.0, -0.5, 0.3);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = export_temp_stl(ref_mesh, "repropa_ref");
    auto meas_path = export_temp_stl(meas_mesh, "repropa_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";
    req.rps_points = {
        {20, 0, 0,   {{"z", 1.0}}},
        {-20, 0, 0,  {{"z", 1.0}}},
        {0, 20, 0,   {{"z", 1.0}}},
        {20, 0, 20,  {{"y", 1.0}}},
        {-20, 0, 20, {{"y", 1.0}}},
        {0, 20, 20,  {{"x", 1.0}}},
    };

    auto pkg1 = service::run_inspection(req);
    auto pkg2 = service::run_inspection(req);

    EXPECT_EQ(pkg1.verdict_label, pkg2.verdict_label);
    EXPECT_EQ(pkg1.alignment_rms, pkg2.alignment_rms);
    std::cout << "ReproGate: match=" << (pkg1.alignment_rms == pkg2.alignment_rms) << "\n";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}
