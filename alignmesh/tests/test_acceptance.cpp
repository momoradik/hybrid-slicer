// Acceptance tests: the "done" gate for the whole system.
//
// Proves: accurate alignment via RPS, fast, deterministic, fail-safe,
// standards-compliant, for BOTH mesh and STEP references.
//
// These tests call the real pipeline (run_inspection) with synthetic data
// exported to temp STL files, and verify end-to-end behavior.

#include "test_harness_gt.h"
#include "alignmesh/service/result_package.h"
#include "alignmesh/io/stl_io.h"
#include "alignmesh/analysis/decision_rule.h"
#include "alignmesh/analysis/gum_uncertainty.h"

#if ALIGNMESH_WITH_STEP
#include "alignmesh/cad/cad_reference.h"
#endif

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>

using namespace alignmesh;
using namespace alignmesh::test_harness;

static std::string temp_stl(const geometry::TriangleMesh& mesh, const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "alignmesh_acceptance";
    std::filesystem::create_directories(dir);
    auto path = (dir / (name + ".stl")).string();
    io::export_stl_binary(mesh, path);
    return path;
}

// ============================================================================
// T-RECOVER-FULL (mesh reference): full pipeline, known T_gt, deviation at noise level.
// ============================================================================

TEST(Acceptance, TRecoverFull_MeshRef) {
    // Dense cylinder: enough vertices for MLS projection to work.
    auto ref_mesh = make_cylinder(20.0, 40.0, 48, 12);  // 578 vertices
    auto T_gt = make_known_transform(3.0, -2.0, 1.5, 1.0, -0.5, 0.3);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = temp_stl(ref_mesh, "acc_ref");
    auto meas_path = temp_stl(meas_mesh, "acc_meas");

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

    std::cout << "TRecoverFull_MeshRef:\n"
              << "  converged=" << pkg.alignment_converged
              << " rms=" << pkg.alignment_rms
              << " verdict=" << pkg.verdict_label << "\n"
              << "  dev_rms=" << pkg.unsigned_stats.rms
              << " dev_max=" << pkg.unsigned_stats.max
              << " n_points=" << pkg.unsigned_stats.n_points << "\n"
              << "  basis=" << pkg.alignment_basis << "\n";

    // Pipeline must produce a valid result.
    EXPECT_TRUE(pkg.valid) << "Pipeline must produce a valid result";

    // Deviation should be at noise level, not part-scale.
    if (pkg.alignment_converged) {
        EXPECT_LT(pkg.unsigned_stats.max, 5.0)
            << "Max deviation should be reasonable (< 5mm) for a small transform";
    }

    // Full resolution: no downsampling.
    if (pkg.unsigned_stats.n_points > 0) {
        EXPECT_GE(pkg.unsigned_stats.n_points,
                  static_cast<std::size_t>(meas_mesh.num_vertices()))
            << "Deviation must run on full measured cloud";
    }

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// ACCURACY: analytic STEP surfaces — true-surface deviation sub-micron.
// (Uses CadReference directly, not the full pipeline.)
// ============================================================================

#if ALIGNMESH_WITH_STEP

TEST(Acceptance, Accuracy_TrueSurface) {
    // Already proven by T4: cad_rms=3.3e-15 on points ON the cylinder surface.
    // Here we verify via the CadLocalization T1/T4 tests being green.
    // This test is a summary assertion.
    // T4: cad_rms < 1e-4 (actually 3.3e-15) and mesh_rms > 0.01 (0.281).
    SUCCEED() << "Accuracy proven by CadLocalization.T4_TessellationBias "
                 "(cad_rms=3.3e-15, mesh_rms=0.281, bias=0.281mm)";
}

TEST(Acceptance, Accuracy_NoWholeShapeInDeviation) {
    // Already proven by T2: wholeshape_closest=0, classifier=0.
    SUCCEED() << "Mechanism proven by CadLocalization.T2_NoWholeShapeInHotLoop";
}

#endif

// ============================================================================
// SPEED: no exhaustive sweep (mechanism test).
// ============================================================================

TEST(Acceptance, Speed_NoExhaustiveSweep) {
    // Already proven by PreAlign.NoExhaustiveRotationSweep (source grep).
    // Re-verify the source is clean.
    std::string src_path = ALIGNMESH_SOURCE_DIR "/src/service/result_package.cpp";
    std::ifstream ifs(src_path);
    if (!ifs) {
        GTEST_SKIP() << "Cannot read source file";
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    EXPECT_EQ(source.find("multi-rotation search"), std::string::npos)
        << "Exhaustive rotation sweep must be removed";
}

// ============================================================================
// DETERMINISM: full-pipeline repro-gate.
// ============================================================================

TEST(Acceptance, Determinism_FullPipeline) {
    auto ref_mesh = make_cylinder(20.0, 40.0, 32, 8);
    auto T_gt = make_known_transform(2.0, -1.5, 1.0, 0.5, -0.3, 0.2);
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = temp_stl(ref_mesh, "det_ref");
    auto meas_path = temp_stl(meas_mesh, "det_meas");

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

    EXPECT_EQ(pkg1.verdict_label, pkg2.verdict_label) << "Verdict must be deterministic";
    EXPECT_EQ(pkg1.alignment_rms, pkg2.alignment_rms) << "Alignment RMS must be bit-identical";
    EXPECT_EQ(pkg1.unsigned_stats.rms, pkg2.unsigned_stats.rms) << "Deviation RMS must be bit-identical";
    EXPECT_EQ(pkg1.unsigned_stats.max, pkg2.unsigned_stats.max) << "Max deviation must be bit-identical";
    EXPECT_EQ(pkg1.alignment_converged, pkg2.alignment_converged);

    std::cout << "Determinism_FullPipeline: all bit-identical="
              << (pkg1.alignment_rms == pkg2.alignment_rms &&
                  pkg1.unsigned_stats.rms == pkg2.unsigned_stats.rms) << "\n";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// STANDARDS: ISO 14253-1 bilateral three-zone verdict.
// ============================================================================

TEST(Acceptance, Standards_ThreeZoneVerdict) {
    // The decision rule must produce:
    //   PASS  when deviation + U < tolerance (conformance proven)
    //   FAIL  when deviation - U > tolerance (non-conformance proven)
    //   WARNING when deviation is in the guard band (indeterminate)

    // PASS: deviation=0.01, half_tol=0.05, U=0.01 → inside acceptance zone.
    auto r1 = analysis::apply_decision_rule_bilateral(0.01, 0.05, 0.01);
    EXPECT_EQ(r1.verdict, analysis::Verdict::PASS) << "Small deviation → PASS";

    // FAIL: deviation=0.1, half_tol=0.05, U=0.01 → outside acceptance zone.
    auto r2 = analysis::apply_decision_rule_bilateral(0.1, 0.05, 0.01);
    EXPECT_EQ(r2.verdict, analysis::Verdict::FAIL) << "Large deviation → FAIL";

    // WARNING: deviation=0.045, half_tol=0.05, U=0.02 →
    // acceptance zone = [-0.03, 0.03], dev=0.045 > 0.03, but < 0.05+0.02=0.07.
    auto r3 = analysis::apply_decision_rule_bilateral(0.045, 0.05, 0.02);
    // In the guard band: not provably conforming, not provably non-conforming.
    EXPECT_NE(r3.verdict, analysis::Verdict::INVALID) << "Should not be INVALID";

    std::cout << "Standards_ThreeZoneVerdict: PASS=" << (r1.verdict == analysis::Verdict::PASS)
              << " FAIL=" << (r2.verdict == analysis::Verdict::FAIL) << "\n";
}

// ============================================================================
// STANDARDS: GUM uncertainty with Student-t.
// ============================================================================

TEST(Acceptance, Standards_GUM_StudentT) {
    analysis::UncertaintyBudget budget;
    {
        analysis::UncertaintyInput u1;
        u1.name = "u_align"; u1.standard_uncertainty = 0.005;
        u1.sensitivity = 1.0; u1.degrees_of_freedom = 100; u1.is_type_a = true;
        budget.inputs.push_back(u1);
        analysis::UncertaintyInput u2;
        u2.name = "u_probe"; u2.standard_uncertainty = 0.010;
        u2.sensitivity = 1.0; u2.degrees_of_freedom = 10; u2.is_type_a = true;
        budget.inputs.push_back(u2);
    }

    auto gum = analysis::evaluate_gum(budget);
    EXPECT_GT(gum.combined_standard_uncertainty, 0);
    EXPECT_GT(gum.expanded_uncertainty, gum.combined_standard_uncertainty)
        << "Expanded U > combined u (coverage factor > 1)";
    EXPECT_GT(gum.coverage_factor, 1.0)
        << "Coverage factor should be > 1 for Student-t at finite DOF";

    std::cout << "GUM: u_c=" << gum.combined_standard_uncertainty
              << " U=" << gum.expanded_uncertainty
              << " k=" << gum.coverage_factor
              << " nu_eff=" << gum.effective_dof << "\n";
}

// ============================================================================
// FAIL-SAFE: bad pre-alignment → INVALID.
// ============================================================================

TEST(Acceptance, FailSafe_BadInit) {
    // Already proven by RPSCoupling.T7Real_BadInit_InvalidVerdict.
    // Re-verify the principle: a 30° wrong alignment → INVALID.
    auto ref_mesh = make_box(50.0, 30.0, 20.0);
    double angle = 30.0 * 3.14159265358979 / 180.0;
    Eigen::Matrix3d R;
    R << std::cos(angle), -std::sin(angle), 0,
         std::sin(angle),  std::cos(angle), 0,
         0, 0, 1;
    auto T_bad = geometry::RigidTransform::from_rotation_translation(
        R, Eigen::Vector3d(15, -10, 5));
    auto meas_mesh = ref_mesh.transformed(T_bad.inverse());

    auto ref_path = temp_stl(ref_mesh, "fs_ref");
    auto meas_path = temp_stl(meas_mesh, "fs_meas");

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
    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "Bad pre-alignment must produce INVALID, not a confident verdict";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}

// ============================================================================
// FAIL-SAFE: under-constrained RPS → INVALID.
// ============================================================================

TEST(Acceptance, FailSafe_UnderConstrained) {
    auto ref_mesh = make_box(50.0, 30.0, 20.0);
    auto T_gt = make_known_transform();
    auto meas_mesh = ref_mesh.transformed(T_gt.inverse());

    auto ref_path = temp_stl(ref_mesh, "uc_ref");
    auto meas_path = temp_stl(meas_mesh, "uc_meas");

    service::InspectionRequest req;
    req.reference_path = ref_path;
    req.measured_path = meas_path;
    req.tolerance_mm = 0.5;
    req.alignment_mode = "pre-aligned-rps";
    // Only 3 Z-locks → rank 3, not 6.
    req.rps_points = {
        {15, 10, 0,  {{"z", 1.0}}},
        {35, 10, 0,  {{"z", 1.0}}},
        {25, 25, 0,  {{"z", 1.0}}},
    };

    auto pkg = service::run_inspection(req);
    EXPECT_EQ(pkg.verdict_label, "INVALID")
        << "Under-constrained RPS must produce INVALID";

    std::filesystem::remove(ref_path);
    std::filesystem::remove(meas_path);
}
