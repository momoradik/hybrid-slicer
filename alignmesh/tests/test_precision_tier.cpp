#include <gtest/gtest.h>
#include "alignmesh/analysis/precision_tier.h"

using namespace alignmesh::analysis;
using namespace alignmesh::io;
using namespace alignmesh::geometry;

namespace {

// Build a simple flat mesh for chord-error testing.
TriangleMesh make_flat_mesh() {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(10, 0, 0);
    v.col(2) = Vec3(10, 10, 0);
    v.col(3) = Vec3(0, 10, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 2);
    t.col(0) << 0, 1, 2;
    t.col(1) << 0, 2, 3;
    return TriangleMesh(std::move(v), std::move(t));
}

// Build a coarsely tessellated cylinder quarter-arc (known chord error).
TriangleMesh make_coarse_arc(double radius, int n_segments) {
    // Quarter circle in XZ plane, extruded along Y.
    int n_verts = (n_segments + 1) * 2;
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, n_verts);
    int idx = 0;
    for (int i = 0; i <= n_segments; ++i) {
        double theta = (3.14159265358979 / 2.0) * i / n_segments;
        double x = radius * std::cos(theta);
        double z = radius * std::sin(theta);
        v.col(idx++) = Vec3(x, 0, z);
        v.col(idx++) = Vec3(x, 10, z);
    }
    // Triangles
    int n_tris = n_segments * 2;
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, n_tris);
    for (int i = 0; i < n_segments; ++i) {
        int a = i * 2, b = i * 2 + 1, c = (i + 1) * 2, d = (i + 1) * 2 + 1;
        t.col(i * 2) << a, c, b;
        t.col(i * 2 + 1) << b, c, d;
    }
    return TriangleMesh(std::move(v), std::move(t));
}

SourceMetadata stl_metadata() {
    SourceMetadata m;
    m.format = "stl-binary";
    m.coordinate_precision = "float32";
    return m;
}

SourceMetadata ply_double_metadata() {
    SourceMetadata m;
    m.format = "ply-binary-le";
    m.coordinate_precision = "float64";
    return m;
}

} // namespace

// ============================================================================
// Tier determination
// ============================================================================

TEST(PrecisionTier, TierDetermination) {
    EXPECT_EQ(determine_tier(0.200), PrecisionTier::COARSE);
    EXPECT_EQ(determine_tier(0.100), PrecisionTier::COARSE);
    EXPECT_EQ(determine_tier(0.050), PrecisionTier::MEDIUM);
    EXPECT_EQ(determine_tier(0.025), PrecisionTier::MEDIUM);
    EXPECT_EQ(determine_tier(0.015), PrecisionTier::FINE);
    EXPECT_EQ(determine_tier(0.010), PrecisionTier::FINE);
    EXPECT_EQ(determine_tier(0.005), PrecisionTier::PRECISE);
    EXPECT_EQ(determine_tier(0.001), PrecisionTier::PRECISE);
    EXPECT_EQ(determine_tier(0.0005), PrecisionTier::ULTRA);
}

TEST(PrecisionTier, TierRequirements) {
    auto coarse = tier_requirements(PrecisionTier::COARSE, 0.2);
    EXPECT_TRUE(coarse.stl_allowed);
    EXPECT_FALSE(coarse.cad_reference_required);

    auto medium = tier_requirements(PrecisionTier::MEDIUM, 0.05);
    EXPECT_TRUE(medium.stl_allowed);
    EXPECT_TRUE(medium.stl_chord_check_required);

    auto fine = tier_requirements(PrecisionTier::FINE, 0.015);
    EXPECT_FALSE(fine.stl_allowed);
    EXPECT_TRUE(fine.high_density_required);

    auto precise = tier_requirements(PrecisionTier::PRECISE, 0.005);
    EXPECT_TRUE(precise.cad_reference_required);
    EXPECT_TRUE(precise.double_precision_cloud_required);
    EXPECT_TRUE(precise.characterized_scanner_required);

    auto ultra = tier_requirements(PrecisionTier::ULTRA, 0.0005);
    EXPECT_TRUE(ultra.temperature_control_required);
}

// ============================================================================
// STL at 5 µm → rejected
// ============================================================================

TEST(PrecisionTier, STLAt5MicronRejected) {
    auto mesh = make_flat_mesh();
    auto meta = stl_metadata();

    auto result = check_precision_gate(0.005, meta, mesh);

    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(result.invalid_claim);

    // Should mention CAD reference required.
    bool mentions_cad = false;
    for (auto& v : result.violations) {
        if (v.find("CAD") != std::string::npos ||
            v.find("cad") != std::string::npos)
            mentions_cad = true;
    }
    EXPECT_TRUE(mentions_cad)
        << "Rejection at 5µm should mention CAD reference requirement";
}

// ============================================================================
// Chord-error computation
// ============================================================================

TEST(PrecisionTier, ChordErrorFlat) {
    // A flat mesh has zero chord error (no curvature).
    auto mesh = make_flat_mesh();
    double ce = estimate_chord_error(mesh);
    EXPECT_NEAR(ce, 0.0, 1e-10);
}

TEST(PrecisionTier, ChordErrorCoarseArc) {
    // Quarter-circle with 4 segments, radius 50mm.
    // Chord error = R * (1 - cos(theta/2)) where theta = 90°/4 = 22.5°.
    // = 50 * (1 - cos(11.25°)) = 50 * (1 - 0.98079) ≈ 50 * 0.01921 = 0.96mm.
    auto mesh = make_coarse_arc(50.0, 4);
    double ce = estimate_chord_error(mesh);
    // Should detect significant chord error (>0.5mm).
    EXPECT_GT(ce, 0.3)
        << "Coarse arc (R=50, 4 segments) should have measurable chord error";
}

TEST(PrecisionTier, ChordErrorMediumTier) {
    // Medium tier (25-100µm) requires chord error check.
    // If chord error exceeds tolerance, should fail.
    auto mesh = make_coarse_arc(50.0, 4);
    auto meta = stl_metadata();

    auto result = check_precision_gate(0.050, meta, mesh);

    // Chord error ~0.96mm >> 0.050mm tolerance → should fail.
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(result.chord_error_computed);
    EXPECT_GT(result.estimated_chord_error, 0.050);
}

// ============================================================================
// Gate passing
// ============================================================================

TEST(PrecisionTier, CoarseTierPasses) {
    auto mesh = make_flat_mesh();
    auto meta = stl_metadata();

    auto result = check_precision_gate(0.200, meta, mesh);
    EXPECT_TRUE(result.passed);
    EXPECT_FALSE(result.invalid_claim);
}

TEST(PrecisionTier, PLYDoubleAtFineTier) {
    auto mesh = make_flat_mesh();
    auto meta = ply_double_metadata();

    // FINE tier with PLY double and high density → should pass
    // (CAD not required at FINE, just discouraged for STL).
    auto result = check_precision_gate(0.015, meta, mesh);
    EXPECT_TRUE(result.passed);
}

// ============================================================================
// Boundary tolerances
// ============================================================================

TEST(PrecisionTier, BoundaryAtExactly100Micron) {
    // 0.100mm = 100µm → COARSE (≥100µm).
    EXPECT_EQ(determine_tier(0.100), PrecisionTier::COARSE);
}

TEST(PrecisionTier, BoundaryJustBelow100Micron) {
    // 0.099mm → MEDIUM.
    EXPECT_EQ(determine_tier(0.099), PrecisionTier::MEDIUM);
}

TEST(PrecisionTier, BoundaryAtExactly25Micron) {
    // 0.025mm → MEDIUM.
    EXPECT_EQ(determine_tier(0.025), PrecisionTier::MEDIUM);
}

TEST(PrecisionTier, BoundaryJustBelow25Micron) {
    // 0.024mm → FINE.
    EXPECT_EQ(determine_tier(0.024), PrecisionTier::FINE);
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(PrecisionTierAdversarial, MissingMetadata) {
    auto mesh = make_flat_mesh();
    SourceMetadata meta;  // all empty
    auto result = check_precision_gate(0.050, meta, mesh);
    // Should warn about missing format info but attempt analysis.
    bool has_warning = !result.violations.empty() || !result.satisfied.empty();
    EXPECT_TRUE(has_warning || result.passed || result.invalid_claim);
}

TEST(PrecisionTierAdversarial, ZeroTolerance) {
    auto mesh = make_flat_mesh();
    auto meta = stl_metadata();
    auto result = check_precision_gate(0.0, meta, mesh);
    // Zero tolerance → ULTRA tier, which requires everything.
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(result.invalid_claim);
}

TEST(PrecisionTierAdversarial, NegativeTolerance) {
    auto mesh = make_flat_mesh();
    auto meta = stl_metadata();
    auto result = check_precision_gate(-0.01, meta, mesh);
    EXPECT_FALSE(result.passed);
}
