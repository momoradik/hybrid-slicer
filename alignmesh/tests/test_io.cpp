#include <gtest/gtest.h>
#include "alignmesh/io/sha256.h"
#include "alignmesh/io/stl_io.h"
#include "alignmesh/io/ply_io.h"
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/io/mesh_validation.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace alignmesh::io;
using namespace alignmesh::geometry;

namespace {

// Build a simple unit cube (8 verts, 12 triangles).
TriangleMesh make_cube() {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 8);
    v.col(0) = Vec3(0, 0, 0); v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(1, 1, 0); v.col(3) = Vec3(0, 1, 0);
    v.col(4) = Vec3(0, 0, 1); v.col(5) = Vec3(1, 0, 1);
    v.col(6) = Vec3(1, 1, 1); v.col(7) = Vec3(0, 1, 1);

    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 12);
    // bottom
    t.col(0)  << 0, 2, 1; t.col(1)  << 0, 3, 2;
    // top
    t.col(2)  << 4, 5, 6; t.col(3)  << 4, 6, 7;
    // front
    t.col(4)  << 0, 1, 5; t.col(5)  << 0, 5, 4;
    // back
    t.col(6)  << 2, 3, 7; t.col(7)  << 2, 7, 6;
    // left
    t.col(8)  << 0, 4, 7; t.col(9)  << 0, 7, 3;
    // right
    t.col(10) << 1, 2, 6; t.col(11) << 1, 6, 5;
    return TriangleMesh(std::move(v), std::move(t));
}

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

// ============================================================================
// SHA-256
// ============================================================================

TEST(SHA256, KnownVector) {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string hash = sha256_hex(nullptr, 0);
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SHA256, KnownVectorAbc) {
    // SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const uint8_t abc[] = {'a', 'b', 'c'};
    EXPECT_EQ(sha256_hex(abc, 3),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(SHA256, StabilityAndSensitivity) {
    // Same input -> same hash.
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    std::string h1 = sha256_hex(data);
    std::string h2 = sha256_hex(data);
    EXPECT_EQ(h1, h2);

    // 1-byte change -> different hash.
    data[2] = 99;
    std::string h3 = sha256_hex(data);
    EXPECT_NE(h1, h3);
}

// ============================================================================
// STL round-trip
// ============================================================================

TEST(STL, BinaryRoundTrip) {
    auto cube = make_cube();
    std::string path = temp_path("test_cube.stl");

    auto exp_result = export_stl_binary(cube, path);
    ASSERT_TRUE(exp_result.success);
    EXPECT_FALSE(exp_result.warnings.empty());  // precision-loss warning

    auto bytes = read_file(path);
    auto imported = import_stl(bytes);

    EXPECT_EQ(imported.metadata.format, "stl-binary");
    EXPECT_EQ(imported.metadata.coordinate_precision, "float32");
    EXPECT_EQ(imported.metadata.triangle_count, 12u);

    // Vertex coordinates should be float32-exact (1.0 and 0.0 are exact in float32).
    for (Eigen::Index i = 0; i < imported.mesh.num_vertices(); ++i) {
        for (int c = 0; c < 3; ++c) {
            double val = imported.mesh.vertices()(c, i);
            EXPECT_TRUE(val == 0.0 || val == 1.0)
                << "Unexpected value " << val << " at vertex " << i;
        }
    }

    std::filesystem::remove(path);
}

TEST(STL, Float32QuantizationLoss) {
    // A coordinate with clear float32 quantization loss.
    // Larger values amplify the quantization step.
    double precise_val = 123456.789;
    float f_val = static_cast<float>(precise_val);
    double loss = std::abs(precise_val - static_cast<double>(f_val));
    // float32 mantissa is 23 bits; at magnitude ~1e5, step ~0.008.
    EXPECT_GT(loss, 1e-5);   // there IS measurable loss
    EXPECT_LT(loss, 1.0);    // but not catastrophic

    // Build a single-triangle mesh with the precise coordinate.
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 3);
    v.col(0) = Vec3(precise_val, 0, 0);
    v.col(1) = Vec3(0, precise_val, 0);
    v.col(2) = Vec3(0, 0, precise_val);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    std::string path = temp_path("test_quant.stl");
    export_stl_binary(mesh, path);
    auto bytes = read_file(path);
    auto imported = import_stl(bytes);

    // Verify the quantization matches float32 cast.
    EXPECT_EQ(imported.mesh.vertices()(0, 0), static_cast<double>(f_val));

    std::filesystem::remove(path);
}

// ============================================================================
// PLY round-trip
// ============================================================================

TEST(PLY, BinaryDoubleRoundTrip) {
    auto cube = make_cube();
    std::string path = temp_path("test_cube.ply");

    auto exp_result = export_ply_binary_double(cube, path);
    ASSERT_TRUE(exp_result.success);

    auto bytes = read_file(path);
    auto imported = import_ply(bytes);

    EXPECT_EQ(imported.metadata.format, "ply-binary-le");
    EXPECT_EQ(imported.metadata.coordinate_precision, "float64");
    EXPECT_EQ(imported.metadata.vertex_count, 8u);
    EXPECT_EQ(imported.metadata.triangle_count, 12u);

    // Double precision preserves coordinates exactly (binary64 round-trip).
    for (Eigen::Index i = 0; i < cube.num_vertices(); ++i) {
        EXPECT_EQ(imported.mesh.vertices()(0, i), cube.vertices()(0, i));
        EXPECT_EQ(imported.mesh.vertices()(1, i), cube.vertices()(1, i));
        EXPECT_EQ(imported.mesh.vertices()(2, i), cube.vertices()(2, i));
    }

    std::filesystem::remove(path);
}

TEST(PLY, DoublePreservesFullPrecision) {
    // A coordinate that requires double precision.
    double val = 1.2345678901234567;  // 17 significant digits

    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 3);
    v.col(0) = Vec3(val, 0, 0);
    v.col(1) = Vec3(0, val, 0);
    v.col(2) = Vec3(0, 0, val);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    std::string path = temp_path("test_double.ply");
    export_ply_binary_double(mesh, path);
    auto bytes = read_file(path);
    auto imported = import_ply(bytes);

    // Exact binary64 preservation.
    EXPECT_EQ(imported.mesh.vertices()(0, 0), val);

    std::filesystem::remove(path);
}

// ============================================================================
// ImmutableSourceStore
// ============================================================================

TEST(ImmutableSourceStore, StoreAndRetrieve) {
    auto cube = make_cube();
    std::string path = temp_path("store_test.ply");
    export_ply_binary_double(cube, path);

    ImmutableSourceStore store;
    auto result = store.import_file(path);

    EXPECT_FALSE(result.hash.empty());
    EXPECT_EQ(result.hash.size(), 64u);
    EXPECT_TRUE(store.has(result.hash));
    EXPECT_FALSE(store.original_bytes(result.hash).empty());
    EXPECT_EQ(store.metadata(result.hash).vertex_count, 8u);

    // Mesh has the source hash set.
    EXPECT_TRUE(result.mesh.is_source());
    EXPECT_EQ(result.mesh.source_hash(), result.hash);

    std::filesystem::remove(path);
}

TEST(ImmutableSourceStore, HashStability) {
    auto cube = make_cube();
    std::string path = temp_path("hash_stable.ply");
    export_ply_binary_double(cube, path);

    ImmutableSourceStore store1, store2;
    auto r1 = store1.import_file(path);
    auto r2 = store2.import_file(path);

    EXPECT_EQ(r1.hash, r2.hash);

    std::filesystem::remove(path);
}

// ============================================================================
// Real file: V2Sina.stl
// ============================================================================

TEST(RealFile, V2SinaSTL) {
    std::string path = "C:/Users/Sina/OneDrive/Desktop/V2Sina.stl";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "V2Sina.stl not found at " << path;
    }

    ImmutableSourceStore store;
    auto result = store.import_file(path);

    // Report metadata
    std::cout << "\n=== V2Sina.stl metadata ===" << std::endl;
    std::cout << "Format:     " << result.metadata.format << std::endl;
    std::cout << "Precision:  " << result.metadata.coordinate_precision << std::endl;
    std::cout << "Vertices:   " << result.metadata.vertex_count << std::endl;
    std::cout << "Triangles:  " << result.metadata.triangle_count << std::endl;
    std::cout << "BBox min:   " << result.metadata.bbox_min.transpose() << std::endl;
    std::cout << "BBox max:   " << result.metadata.bbox_max.transpose() << std::endl;
    std::cout << "SHA-256:    " << result.hash << std::endl;

    EXPECT_EQ(result.metadata.format, "stl-binary");
    EXPECT_EQ(result.metadata.coordinate_precision, "float32");
    EXPECT_EQ(result.metadata.triangle_count, 363539u);
    EXPECT_TRUE(result.mesh.is_source());

    // Validate
    auto report = validate_mesh(result.mesh);
    std::cout << "\n=== V2Sina.stl validation ===" << std::endl;
    for (auto& w : report.warnings) std::cout << "  WARN: " << w << std::endl;
    for (auto& e : report.errors) std::cout << "  ERR:  " << e << std::endl;
    if (report.is_valid) std::cout << "  RESULT: VALID" << std::endl;
    else std::cout << "  RESULT: ISSUES DETECTED" << std::endl;
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(Adversarial, EmptyFile) {
    EXPECT_THROW(import_stl(nullptr, 0), std::runtime_error);
}

TEST(Adversarial, TruncatedBinarySTL) {
    // Valid header but truncated triangle data.
    std::vector<uint8_t> data(100, 0);
    // Set triangle count to 1000 (but only 16 bytes of data after header)
    uint32_t count = 1000;
    std::memcpy(data.data() + 80, &count, 4);
    EXPECT_THROW(import_stl(data), std::runtime_error);
}

TEST(Adversarial, HugeTriangleCount) {
    // Triangle count that would require >100M triangles.
    std::vector<uint8_t> data(84, 0);
    uint32_t count = 200'000'000;
    std::memcpy(data.data() + 80, &count, 4);
    EXPECT_THROW(import_stl(data), std::runtime_error);
}

TEST(Adversarial, NaNCoordinatesSTL) {
    // Build a valid binary STL with one triangle containing NaN.
    std::vector<uint8_t> data(84 + 50, 0);
    uint32_t count = 1;
    std::memcpy(data.data() + 80, &count, 4);

    float nan_val = std::numeric_limits<float>::quiet_NaN();
    float zero = 0.0f;
    float one = 1.0f;
    // normal
    std::memcpy(data.data() + 84, &zero, 4);
    std::memcpy(data.data() + 88, &zero, 4);
    std::memcpy(data.data() + 92, &one, 4);
    // vertex 0: NaN
    std::memcpy(data.data() + 96, &nan_val, 4);
    std::memcpy(data.data() + 100, &zero, 4);
    std::memcpy(data.data() + 104, &zero, 4);
    // vertex 1
    std::memcpy(data.data() + 108, &one, 4);
    std::memcpy(data.data() + 112, &zero, 4);
    std::memcpy(data.data() + 116, &zero, 4);
    // vertex 2
    std::memcpy(data.data() + 120, &zero, 4);
    std::memcpy(data.data() + 124, &one, 4);
    std::memcpy(data.data() + 128, &zero, 4);

    auto result = import_stl(data);
    // Import should succeed but warn.
    bool found_nan_warning = false;
    for (auto& w : result.warnings)
        if (w.find("NaN") != std::string::npos) found_nan_warning = true;
    EXPECT_TRUE(found_nan_warning);
}

TEST(Adversarial, CorruptPLYHeader) {
    std::vector<uint8_t> data = {'p', 'l', 'y', '\n', 'g', 'a', 'r', 'b', 'a', 'g', 'e'};
    EXPECT_THROW(import_ply(data), std::runtime_error);
}

TEST(Adversarial, TruncatedPLY) {
    // Valid header but no data.
    std::string hdr = "ply\nformat binary_little_endian 1.0\n"
                      "element vertex 100\nproperty float x\nproperty float y\n"
                      "property float z\nelement face 10\n"
                      "property list uchar int vertex_indices\nend_header\n";
    std::vector<uint8_t> data(hdr.begin(), hdr.end());
    EXPECT_THROW(import_ply(data), std::runtime_error);
}
