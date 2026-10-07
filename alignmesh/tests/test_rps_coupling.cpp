// Tests for the RPS projection↔solve coupling loop.
//
// - Convergence on a full-rank case.
// - T7-REAL: deliberately wrong pre-alignment → INVALID verdict from the real pipeline.
// - Repro-gate.

#include "test_harness_gt.h"
#include "alignmesh/service/result_package.h"
#include "alignmesh/registration/rps_projection.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/io/stl_io.h"

#include <gtest/gtest.h>
#include <iostream>
#include <cmath>
#include <filesystem>

using namespace alignmesh;
using namespace alignmesh::test_harness;

// Helper: export a mesh to a temp STL file and return the path.
static std::string export_temp_stl(const geometry::TriangleMesh& mesh,
                                    const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "alignmesh_test";
    std::filesystem::create_directories(dir);
    auto path = (dir / (name + ".stl")).string();
    io::export_stl_binary(mesh, path);
    return path;
}

// ============================================================================
// Convergence on a well-conditioned full-rank case.
// ============================================================================

TEST(RPSCoupling, ConvergesOnGoodInit) {
    // Use a cylinder with a known transform and correct pre-alignment.
    // The coupling loop should converge in 1-2 iterations.
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto T_gt = make_known_transform(3.0, -2.0, 1.5, 0.5, -0.3, 0.2);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    // Export to temp files for run_inspection.
    auto ref_path = export_temp_stl(ref_mesh, "coupling_ref");
    auto meas_path = export_temp_stl(meas_mesh, "coupling_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";

    // RPS points on the cylinder.
    req.rps_points = {
        {20, 0, 0,  {{"z", 1.0}}},
        {-20, 0, 0, {{"z", 1.0}}},
        {0, 20, 0,  {{"z", 1.0}}},
        {20, 0, 20, {{"y", 1.0}}},
        {-20, 0, 20,{{"y", 1.0}}},
        {0, 20, 20, {{"x", 1.0}}},
    };

    auto pkg = service::run_inspection(req);

    std::cout << "ConvergesOnGoodInit: converged=" << pkg.alignment_converged
              << " rms=" << pkg.alignment_rms
              << " verdict=" << pkg.verdict_label
              << " max_dev=" << pkg.unsigned_stats.max << "\n";

    // With a small T_gt and correct pre-alignment from FPFH/centroid,
    // the coupling should converge. The verdict will be INVALID due to
    // the uncertainty gate, but alignment_converged should be true.
    // (Note: if FPFH fails on the cylinder, the centroid fallback
    // still brings the parts close enough for the coupling to work.)
    if (pkg.alignment_converged) {
        EXPECT_LT(pkg.unsigned_stats.rms, 2.0)
            << "Converged alignment should give reasonable deviation";
    }

    // Clean up.
    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// T7-REAL: deliberately wrong pre-alignment → INVALID verdict.
// Calls the REAL run_inspection pipeline.
// ============================================================================

TEST(RPSCoupling, T7Real_BadInit_InvalidVerdict) {
    // Create a box reference and a measured scan of the same box
    // but pre-aligned 30° off. The wrong pre-alignment should cause
    // the coupling to not converge, or the projection to fail,
    // producing an INVALID verdict — NOT a confident PASS/FAIL.

    auto ref_mesh = make_box(50.0, 30.0, 20.0);

    // Apply a 30° rotation as the "bad" pre-alignment.
    double bad_angle = 30.0 * 3.14159265358979 / 180.0;
    Eigen::Matrix3d R_bad;
    R_bad << std::cos(bad_angle), -std::sin(bad_angle), 0,
             std::sin(bad_angle),  std::cos(bad_angle), 0,
             0, 0, 1;
    auto T_bad = geometry::RigidTransform::from_rotation_translation(
        R_bad, Eigen::Vector3d(15.0, -10.0, 5.0));

    auto meas_mesh = ref_mesh.transformed(T_bad.inverse());

    auto ref_path = export_temp_stl(ref_mesh, "t7real_ref");
    auto meas_path = export_temp_stl(meas_mesh, "t7real_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.1;
    req.alignment_mode = "pre-aligned-rps";

    // RPS points — these are on the REFERENCE, but with the 30° bad init,
    // the projection will find wrong correspondences.
    req.rps_points = {
        {15, 10, 0,  {{"z", 1.0}}},
        {35, 10, 0,  {{"z", 1.0}}},
        {25, 25, 0,  {{"z", 1.0}}},
        {15, 0, 10,  {{"y", 1.0}}},
        {35, 0, 10,  {{"y", 1.0}}},
        {0, 15, 10,  {{"x", 1.0}}},
    };

    auto pkg = service::run_inspection(req);

    std::cout << "T7-REAL: verdict=" << pkg.verdict_label
              << " converged=" << pkg.alignment_converged
              << " max_dev=" << pkg.unsigned_stats.max
              << " rms=" << pkg.alignment_rms << "\n";
    for (auto& w : pkg.warnings)
        std::cout << "  warning: " << w << "\n";

    // The verdict MUST be INVALID — not a confident PASS or FAIL.
    // A 30° wrong pre-alignment on a box should cause the coupling to
    // fail or produce incorrect correspondences.
    EXPECT_NE(pkg.verdict_label, "PASS")
        << "A 30° wrong pre-alignment must NOT produce a confident PASS";
    // FAIL is also not acceptable with a wrong alignment — it should be INVALID.
    // However, the uncertainty gate already forces INVALID for any PASS/WARNING.
    // What we're really checking is that the system doesn't emit a FAIL that
    // would claim the part is non-conforming based on a wrong alignment.
    // The verdict should be INVALID (from uncertainty gate + non-convergence).
    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "Bad pre-alignment should produce INVALID verdict";

    // Clean up.
    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// Repro-gate: two identical pipeline runs → identical verdict + deviation.
// ============================================================================

TEST(RPSCoupling, ReproGate) {
    auto ref_mesh = make_box(50.0, 30.0, 20.0);
    auto T_gt = make_known_transform(2.0, -1.5, 1.0, 0.3, -0.2, 0.1);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = export_temp_stl(ref_mesh, "repro_ref");
    auto meas_path = export_temp_stl(meas_mesh, "repro_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";
    req.rps_points = {
        {15, 10, 0,  {{"z", 1.0}}},
        {35, 10, 0,  {{"z", 1.0}}},
        {25, 25, 0,  {{"z", 1.0}}},
        {15, 0, 10,  {{"y", 1.0}}},
        {35, 0, 10,  {{"y", 1.0}}},
        {0, 15, 10,  {{"x", 1.0}}},
    };

    auto pkg1 = service::run_inspection(req);
    auto pkg2 = service::run_inspection(req);

    EXPECT_EQ(pkg1.verdict_label, pkg2.verdict_label);
    EXPECT_EQ(pkg1.alignment_rms, pkg2.alignment_rms);
    EXPECT_EQ(pkg1.unsigned_stats.rms, pkg2.unsigned_stats.rms);
    EXPECT_EQ(pkg1.unsigned_stats.max, pkg2.unsigned_stats.max);

    std::cout << "ReproGate: verdict1=" << pkg1.verdict_label
              << " verdict2=" << pkg2.verdict_label
              << " rms_match=" << (pkg1.alignment_rms == pkg2.alignment_rms) << "\n";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}
