#include <gtest/gtest.h>
#include "alignmesh/io/pointcloud_io.h"
#include "alignmesh/io/immutable_source_store.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace alignmesh;

static std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

// ── Basic XYZ parsing ────────────────────────────────────────────────

TEST(PointCloudIO, BasicXYZ) {
    std::string data =
        "1.0 2.0 3.0\n"
        "4.0 5.0 6.0\n"
        "7.0 8.0 9.0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 3);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(result.cloud.points()(1, 0), 2.0);
    EXPECT_DOUBLE_EQ(result.cloud.points()(2, 0), 3.0);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 2), 7.0);
    EXPECT_EQ(result.metadata.format, "pointcloud-text");
    EXPECT_EQ(result.metadata.coordinate_precision, "float64");
    EXPECT_EQ(result.metadata.vertex_count, 3u);
    EXPECT_EQ(result.metadata.triangle_count, 0u);
}

// ── Double precision round-trip ──────────────────────────────────────

TEST(PointCloudIO, DoublePrecision) {
    // Sub-micron coordinates must survive parsing exactly.
    std::string data =
        "100.00000123456789 200.00000987654321 300.00000111222333\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 1);
    // strtod should recover the value to within 1 ULP.
    EXPECT_NEAR(result.cloud.points()(0, 0), 100.00000123456789, 1e-14);
    EXPECT_NEAR(result.cloud.points()(1, 0), 200.00000987654321, 1e-14);
}

// ── Comma-delimited ──────────────────────────────────────────────────

TEST(PointCloudIO, CommaDelimited) {
    std::string data = "1.5,2.5,3.5\n4.5,5.5,6.5\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 2);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 1), 4.5);
}

// ── Comments and headers skipped ─────────────────────────────────────

TEST(PointCloudIO, CommentsAndHeaders) {
    std::string data =
        "# This is a comment\n"
        "// Another comment\n"
        "x y z nx ny nz\n"
        "1.0 2.0 3.0 0.0 0.0 1.0\n"
        "! exclamation comment\n"
        "4.0 5.0 6.0 0.0 1.0 0.0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 2);
    EXPECT_TRUE(result.cloud.has_normals());
    EXPECT_DOUBLE_EQ(result.cloud.normals()(2, 0), 1.0); // nz of first point
}

// ── PTS header (point count on first line) ───────────────────────────

TEST(PointCloudIO, PTSHeader) {
    std::string data =
        "3\n"
        "10.0 20.0 30.0\n"
        "40.0 50.0 60.0\n"
        "70.0 80.0 90.0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 3);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 0), 10.0);
}

// ── NaN/Inf rejection (fail-safe) ────────────────────────────────────

TEST(PointCloudIO, RejectsNaNInf) {
    std::string data =
        "1.0 2.0 3.0\n"
        "nan 5.0 6.0\n"
        "7.0 inf 9.0\n"
        "10.0 11.0 12.0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    // Lines with NaN/Inf are rejected; 2 valid points remain
    ASSERT_EQ(result.cloud.size(), 2);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 1), 10.0);
    EXPECT_GE(result.warnings.size(), 1u); // at least one warning about rejected lines
}

// ── Empty file throws ────────────────────────────────────────────────

TEST(PointCloudIO, EmptyFileThrows) {
    EXPECT_THROW(io::import_pointcloud(to_bytes("")), std::runtime_error);
}

// ── All-comments file throws ─────────────────────────────────────────

TEST(PointCloudIO, AllCommentsThrows) {
    std::string data = "# comment only\n// another\n";
    EXPECT_THROW(io::import_pointcloud(to_bytes(data)), std::runtime_error);
}

// ── Single point ─────────────────────────────────────────────────────

TEST(PointCloudIO, SinglePoint) {
    std::string data = "42.5 -13.7 0.001\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 1);
    EXPECT_DOUBLE_EQ(result.cloud.points()(0, 0), 42.5);
    EXPECT_DOUBLE_EQ(result.cloud.points()(1, 0), -13.7);
    EXPECT_DOUBLE_EQ(result.cloud.points()(2, 0), 0.001);
}

// ── Bbox computed correctly ──────────────────────────────────────────

TEST(PointCloudIO, BboxCorrect) {
    std::string data =
        "-10.0 0.0 5.0\n"
        "10.0 20.0 -5.0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    EXPECT_DOUBLE_EQ(result.metadata.bbox_min.x(), -10.0);
    EXPECT_DOUBLE_EQ(result.metadata.bbox_max.x(), 10.0);
    EXPECT_DOUBLE_EQ(result.metadata.bbox_min.z(), -5.0);
    EXPECT_DOUBLE_EQ(result.metadata.bbox_max.z(), 5.0);
}

// ── looks_like_pointcloud detection ──────────────────────────────────

TEST(PointCloudIO, DetectsPointCloud) {
    std::string xyz = "1.0 2.0 3.0\n4.0 5.0 6.0\n";
    EXPECT_TRUE(io::looks_like_pointcloud(
        reinterpret_cast<const uint8_t*>(xyz.data()), xyz.size()));

    std::string ply = "ply\nformat ascii 1.0\n";
    EXPECT_FALSE(io::looks_like_pointcloud(
        reinterpret_cast<const uint8_t*>(ply.data()), ply.size()));

    std::string empty = "";
    EXPECT_FALSE(io::looks_like_pointcloud(
        reinterpret_cast<const uint8_t*>(empty.data()), 0));
}

// ── ImportedSource with point cloud ──────────────────────────────────

TEST(PointCloudIO, ImportedSourceVariant) {
    // Create an ImportedSource from a point cloud
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts << 1, 4,  2, 5,  3, 6;
    geometry::PointCloud cloud(pts);

    io::SourceMetadata meta;
    meta.format = "pointcloud-text";
    meta.coordinate_precision = "float64";
    meta.vertex_count = 2;

    io::ImportedSource src{std::move(cloud), meta, {}, "testhash"};
    EXPECT_TRUE(src.is_cloud());
    EXPECT_FALSE(src.is_mesh());
    EXPECT_EQ(src.points().cols(), 2);
    EXPECT_DOUBLE_EQ(src.points()(0, 0), 1.0);
}

// ── Windows line endings ─────────────────────────────────────────────

TEST(PointCloudIO, WindowsLineEndings) {
    std::string data = "1.0 2.0 3.0\r\n4.0 5.0 6.0\r\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 2);
}

// ── Extra columns ignored ────────────────────────────────────────────

TEST(PointCloudIO, ExtraColumnsIgnored) {
    // Columns beyond 6 (e.g., intensity, RGB) are silently ignored
    std::string data = "1.0 2.0 3.0 0.5 0.5 0.5 128 255 0\n";
    auto result = io::import_pointcloud(to_bytes(data));
    ASSERT_EQ(result.cloud.size(), 1);
    EXPECT_TRUE(result.cloud.has_normals());
    EXPECT_DOUBLE_EQ(result.cloud.normals()(0, 0), 0.5);
}
