#include <gtest/gtest.h>
#include "alignmesh/service/result_package.h"
#include "alignmesh/io/stl_io.h"
#include "alignmesh/io/ply_io.h"

#include <filesystem>

using namespace alignmesh::service;
using namespace alignmesh::analysis;

namespace {

// Create a simple test mesh file.
std::string create_test_mesh(const std::string& name) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = alignmesh::geometry::Vec3(0, 0, 0);
    v.col(1) = alignmesh::geometry::Vec3(10, 0, 0);
    v.col(2) = alignmesh::geometry::Vec3(10, 10, 0);
    v.col(3) = alignmesh::geometry::Vec3(0, 10, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 2);
    t.col(0) << 0, 1, 2;
    t.col(1) << 0, 2, 3;
    alignmesh::geometry::TriangleMesh mesh(std::move(v), std::move(t));

    auto path = (std::filesystem::temp_directory_path() / name).string();
    alignmesh::io::export_ply_binary_double(mesh, path);
    return path;
}

} // namespace

// ============================================================================
// Result package integrity
// ============================================================================

TEST(ServiceFirewall, ResultPackageHasFingerprint) {
    auto ref = create_test_mesh("fw_ref.ply");
    auto meas = create_test_mesh("fw_meas.ply");

    InspectionRequest req;
    req.reference_path = ref;
    req.measured_path = meas;
    req.tolerance_mm = 1.0;

    auto pkg = run_inspection(req);

    // Core version and fingerprint must be present.
    EXPECT_FALSE(pkg.core_version.empty());
    EXPECT_FALSE(pkg.fingerprint.compiler_id.empty());
    EXPECT_FALSE(pkg.timestamp.empty());

    std::filesystem::remove(ref);
    std::filesystem::remove(meas);
}

TEST(ServiceFirewall, HeatmapPreComputed) {
    auto ref = create_test_mesh("fw_ref2.ply");
    auto meas = create_test_mesh("fw_meas2.ply");

    InspectionRequest req;
    req.reference_path = ref;
    req.measured_path = meas;
    req.tolerance_mm = 1.0;

    auto pkg = run_inspection(req);

    // Points must have pre-computed colors (not zero).
    if (!pkg.points.empty()) {
        // Colors should be computed (at least one channel nonzero for some points).
        bool has_color = false;
        for (auto& p : pkg.points) {
            if (p.color.r > 0 || p.color.g > 0 || p.color.b > 0)
                has_color = true;
        }
        EXPECT_TRUE(has_color)
            << "Heatmap colors must be pre-computed by the core";
    }

    // Heatmap label must be present (full-resolution deviation).
    EXPECT_FALSE(pkg.heatmap_label.empty())
        << "Heatmap label must be set";

    std::filesystem::remove(ref);
    std::filesystem::remove(meas);
}

// ============================================================================
// Firewall: result values come from core, not recomputed
// ============================================================================

TEST(ServiceFirewall, StatisticsFromCore) {
    auto ref = create_test_mesh("fw_ref3.ply");
    auto meas = create_test_mesh("fw_meas3.ply");

    InspectionRequest req;
    req.reference_path = ref;
    req.measured_path = meas;
    req.tolerance_mm = 1.0;

    auto pkg = run_inspection(req);

    // Statistics must be populated by the core.
    EXPECT_GT(pkg.unsigned_stats.n_points, 0);

    // The UI would receive exactly these values — no recomputation needed.
    // Verify the package contains all the fields the UI needs to render.
    EXPECT_FALSE(pkg.verdict_label.empty());
    EXPECT_FALSE(pkg.alignment_mode.empty());

    std::filesystem::remove(ref);
    std::filesystem::remove(meas);
}

// ============================================================================
// Firewall: deviation_to_color is in the CORE
// ============================================================================

TEST(ServiceFirewall, ColorMappingInCore) {
    // The color mapping function is in the core, not the UI.
    // Verify it produces valid colors.
    auto blue = deviation_to_color(-1.0, -1.0, 1.0);
    auto green = deviation_to_color(0.0, -1.0, 1.0);
    auto red = deviation_to_color(1.0, -1.0, 1.0);

    EXPECT_GT(blue.b, blue.r);   // blue at low end
    EXPECT_GT(green.g, 100);      // green in middle
    EXPECT_GT(red.r, red.b);     // red at high end
}

// ============================================================================
// Core unavailable: UI must not fabricate results
// ============================================================================

TEST(ServiceFirewall, InvalidFileReturnsError) {
    InspectionRequest req;
    req.reference_path = "/nonexistent/file.ply";
    req.measured_path = "/also/nonexistent.ply";
    req.tolerance_mm = 0.1;

    auto pkg = run_inspection(req);

    // Must not claim valid results.
    EXPECT_FALSE(pkg.valid);
    EXPECT_FALSE(pkg.errors.empty());
    // Must NOT have a PASS verdict.
    EXPECT_NE(pkg.verdict, Verdict::PASS);
}

// ============================================================================
// Transform matrix is pre-computed
// ============================================================================

TEST(ServiceFirewall, TransformInPackage) {
    auto ref = create_test_mesh("fw_ref4.ply");
    auto meas = create_test_mesh("fw_meas4.ply");

    InspectionRequest req;
    req.reference_path = ref;
    req.measured_path = meas;
    req.tolerance_mm = 1.0;

    auto pkg = run_inspection(req);

    // Transform matrix should be populated (identity for matching meshes).
    // The UI uses this for the before/after overlay.
    double trace = pkg.transform_matrix[0] + pkg.transform_matrix[5] +
                   pkg.transform_matrix[10] + pkg.transform_matrix[15];
    EXPECT_GT(trace, 0) << "Transform matrix should be populated";

    std::filesystem::remove(ref);
    std::filesystem::remove(meas);
}

// ============================================================================
// Hashes recorded for provenance
// ============================================================================

TEST(ServiceFirewall, HashesRecorded) {
    auto ref = create_test_mesh("fw_ref5.ply");
    auto meas = create_test_mesh("fw_meas5.ply");

    InspectionRequest req;
    req.reference_path = ref;
    req.measured_path = meas;
    req.tolerance_mm = 1.0;

    auto pkg = run_inspection(req);
    EXPECT_EQ(pkg.reference_hash.size(), 64u);
    EXPECT_EQ(pkg.measured_hash.size(), 64u);

    std::filesystem::remove(ref);
    std::filesystem::remove(meas);
}
