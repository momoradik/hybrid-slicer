#include <gtest/gtest.h>
#include "alignmesh/geometry/triangle_mesh.h"

using namespace alignmesh::geometry;

namespace {

// Unit tetrahedron: 4 vertices, 4 triangles, fully closed.
TriangleMesh make_tetrahedron() {
    Eigen::Matrix<double, 3, Eigen::Dynamic> verts(3, 4);
    verts.col(0) = Vec3(0, 0, 0);
    verts.col(1) = Vec3(1, 0, 0);
    verts.col(2) = Vec3(0.5, std::sqrt(3.0) / 2.0, 0);
    verts.col(3) = Vec3(0.5, std::sqrt(3.0) / 6.0, std::sqrt(6.0) / 3.0);

    TriangleMesh::TriMatrix tris(3, 4);
    tris.col(0) << 0, 1, 2;   // bottom
    tris.col(1) << 0, 1, 3;   // front
    tris.col(2) << 1, 2, 3;   // right
    tris.col(3) << 0, 2, 3;   // left
    return TriangleMesh(verts, tris);
}

} // namespace

TEST(TriangleMesh, SingleTriangle) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> verts(3, 3);
    verts.col(0) = Vec3(0, 0, 0);
    verts.col(1) = Vec3(1, 0, 0);
    verts.col(2) = Vec3(0, 1, 0);

    TriangleMesh::TriMatrix tris(3, 1);
    tris.col(0) << 0, 1, 2;
    TriangleMesh mesh(verts, tris);

    EXPECT_EQ(mesh.num_vertices(), 3);
    EXPECT_EQ(mesh.num_triangles(), 1);

    // All edges are boundary (-1).
    EXPECT_EQ(mesh.triangle_adjacency()(0, 0), -1);
    EXPECT_EQ(mesh.triangle_adjacency()(1, 0), -1);
    EXPECT_EQ(mesh.triangle_adjacency()(2, 0), -1);

    // Each vertex is in triangle 0.
    EXPECT_EQ(mesh.vertex_adjacency()[0].size(), 1u);
    EXPECT_EQ(mesh.vertex_adjacency()[0][0], 0);
}

TEST(TriangleMesh, TwoTrianglesSharedEdge) {
    //     2
    //    / \        .
    //   0---1
    //    \ /
    //     3
    Eigen::Matrix<double, 3, Eigen::Dynamic> verts(3, 4);
    verts.col(0) = Vec3(0, 0, 0);
    verts.col(1) = Vec3(1, 0, 0);
    verts.col(2) = Vec3(0.5, 1, 0);
    verts.col(3) = Vec3(0.5, -1, 0);

    TriangleMesh::TriMatrix tris(3, 2);
    tris.col(0) << 0, 1, 2;  // top triangle
    tris.col(1) << 0, 1, 3;  // bottom triangle

    TriangleMesh mesh(verts, tris);

    // Edge 0 of tri 0 connects v0-v1, shared with tri 1.
    EXPECT_EQ(mesh.triangle_adjacency()(0, 0), 1);
    // Other edges of tri 0 are boundary.
    EXPECT_EQ(mesh.triangle_adjacency()(1, 0), -1);
    EXPECT_EQ(mesh.triangle_adjacency()(2, 0), -1);

    // Edge 0 of tri 1 connects v0-v1, shared with tri 0.
    EXPECT_EQ(mesh.triangle_adjacency()(0, 1), 0);
    EXPECT_EQ(mesh.triangle_adjacency()(1, 1), -1);
    EXPECT_EQ(mesh.triangle_adjacency()(2, 1), -1);

    // Vertex adjacency: v0 and v1 are in both triangles.
    EXPECT_EQ(mesh.vertex_adjacency()[0].size(), 2u);
    EXPECT_EQ(mesh.vertex_adjacency()[1].size(), 2u);
    // v2 only in tri 0, v3 only in tri 1.
    EXPECT_EQ(mesh.vertex_adjacency()[2].size(), 1u);
    EXPECT_EQ(mesh.vertex_adjacency()[3].size(), 1u);
}

TEST(TriangleMesh, TetrahedronAdjacency) {
    auto mesh = make_tetrahedron();
    EXPECT_EQ(mesh.num_vertices(), 4);
    EXPECT_EQ(mesh.num_triangles(), 4);

    // In a closed tetrahedron every edge is shared by exactly 2 triangles,
    // so no boundary edges.
    for (int f = 0; f < 4; ++f) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_GE(mesh.triangle_adjacency()(j, f), 0)
                << "Unexpected boundary at face=" << f << " edge=" << j;
        }
    }

    // Each vertex appears in exactly 3 faces.
    for (int v = 0; v < 4; ++v) {
        EXPECT_EQ(mesh.vertex_adjacency()[static_cast<size_t>(v)].size(), 3u)
            << "Vertex " << v << " has wrong face count";
    }
}

TEST(TriangleMesh, Transform) {
    auto mesh = make_tetrahedron();

    Vec3 t(10, 20, 30);
    auto T = RigidTransform::from_rotation_translation(Mat3::Identity(), t);
    auto mesh2 = mesh.transformed(T);

    // Vertices are translated.
    for (Eigen::Index i = 0; i < mesh.num_vertices(); ++i) {
        EXPECT_TRUE(mesh2.vertices().col(i).isApprox(
            mesh.vertices().col(i) + t, 1e-14));
    }

    // Topology is unchanged.
    EXPECT_TRUE(mesh2.triangles() == mesh.triangles());
    EXPECT_EQ(mesh2.triangle_adjacency(), mesh.triangle_adjacency());
}

TEST(TriangleMesh, SourceTracking) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> verts(3, 3);
    verts.col(0) = Vec3(0, 0, 0);
    verts.col(1) = Vec3(1, 0, 0);
    verts.col(2) = Vec3(0, 1, 0);

    TriangleMesh::TriMatrix tris(3, 1);
    tris.col(0) << 0, 1, 2;

    TriangleMesh source(verts, tris, "mesh_hash_xyz");
    EXPECT_TRUE(source.is_source());
    EXPECT_EQ(source.source_hash(), "mesh_hash_xyz");

    auto derived = source.transformed(RigidTransform());
    EXPECT_FALSE(derived.is_source());
}

TEST(TriangleMesh, TransformDoesNotMutateSource) {
    auto mesh = make_tetrahedron();
    Vec3 v0_orig = mesh.vertices().col(0);

    auto T = RigidTransform::from_rotation_translation(
        Mat3::Identity(), Vec3(100, 200, 300));
    auto mesh2 = mesh.transformed(T);

    // Original is unchanged.
    EXPECT_TRUE(mesh.vertices().col(0).isApprox(v0_orig, 1e-15));
}
