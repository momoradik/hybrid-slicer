#include <gtest/gtest.h>
#include "alignmesh/io/mesh_validation.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <cmath>
#include <limits>

using namespace alignmesh::io;
using namespace alignmesh::geometry;

// ============================================================================
// Ground-truth: hand-built broken meshes
// ============================================================================

TEST(MeshValidation, ValidClosedMesh) {
    // Tetrahedron: 4 verts, 4 faces, closed, consistent winding.
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0.5, std::sqrt(3.0) / 2.0, 0);
    v.col(3) = Vec3(0.5, std::sqrt(3.0) / 6.0, std::sqrt(6.0) / 3.0);

    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 4);
    t.col(0) << 0, 2, 1;  // bottom (outward-facing normals)
    t.col(1) << 0, 1, 3;
    t.col(2) << 1, 2, 3;
    t.col(3) << 2, 0, 3;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_TRUE(report.boundary_edges.empty());
    EXPECT_TRUE(report.non_manifold_edges.empty());
    EXPECT_TRUE(report.degenerate_triangles.empty());
    EXPECT_TRUE(report.inconsistent_winding.empty());
}

TEST(MeshValidation, EmptyMesh) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 0);
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_TRUE(report.is_empty);
    EXPECT_FALSE(report.is_valid);
}

TEST(MeshValidation, DegenerateTriangle) {
    // A triangle where all three vertices are collinear.
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 3);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(2, 0, 0);  // collinear -> zero area
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_EQ(report.degenerate_triangles.size(), 1u);
    EXPECT_EQ(report.degenerate_triangles[0], 0);
}

TEST(MeshValidation, DuplicateVertices) {
    // Two vertices at the exact same position.
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0.5, 1, 0);
    v.col(3) = Vec3(0, 0, 0);  // duplicate of vertex 0
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_GE(report.duplicate_vertices.size(), 1u);
}

TEST(MeshValidation, Hole) {
    // Two triangles sharing one edge; the other edges are boundary (hole).
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0.5, 1, 0);
    v.col(3) = Vec3(0.5, -1, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 2);
    t.col(0) << 0, 1, 2;
    t.col(1) << 0, 3, 1;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_GT(report.boundary_edges.size(), 0u);
}

TEST(MeshValidation, FlippedFace) {
    // Two adjacent triangles with SAME winding on shared edge = inconsistent.
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 4);
    v.col(0) = Vec3(0, 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0.5, 1, 0);
    v.col(3) = Vec3(0.5, -1, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 2);
    t.col(0) << 0, 1, 2;
    t.col(1) << 0, 1, 3;  // edge 0-1 same direction as tri 0 -> inconsistent
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_GT(report.inconsistent_winding.size(), 0u);
}

TEST(MeshValidation, NaNVertex) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 3);
    v.col(0) = Vec3(std::numeric_limits<double>::quiet_NaN(), 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0, 1, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_TRUE(report.has_nan_or_inf);
    EXPECT_FALSE(report.is_valid);
}

TEST(MeshValidation, InfVertex) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> v(3, 3);
    v.col(0) = Vec3(std::numeric_limits<double>::infinity(), 0, 0);
    v.col(1) = Vec3(1, 0, 0);
    v.col(2) = Vec3(0, 1, 0);
    Eigen::Matrix<int, 3, Eigen::Dynamic> t(3, 1);
    t.col(0) << 0, 1, 2;
    TriangleMesh mesh(std::move(v), std::move(t));

    auto report = validate_mesh(mesh);
    EXPECT_TRUE(report.has_nan_or_inf);
    EXPECT_FALSE(report.is_valid);
}
