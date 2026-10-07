// Tests for the ReferenceGeometry abstraction and MeshReference implementation.
// These run WITHOUT OCCT — they verify the abstraction layer works correctly
// with the existing mesh tier.

#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/io/step_io.h"

#include <gtest/gtest.h>
#include <Eigen/Core>
#include <cmath>

using namespace alignmesh;

namespace {

// Create a simple unit-cube mesh (8 vertices, 12 triangles).
geometry::TriangleMesh make_unit_cube() {
    Eigen::Matrix<double, 3, Eigen::Dynamic> V(3, 8);
    V.col(0) = Eigen::Vector3d(0, 0, 0);
    V.col(1) = Eigen::Vector3d(1, 0, 0);
    V.col(2) = Eigen::Vector3d(1, 1, 0);
    V.col(3) = Eigen::Vector3d(0, 1, 0);
    V.col(4) = Eigen::Vector3d(0, 0, 1);
    V.col(5) = Eigen::Vector3d(1, 0, 1);
    V.col(6) = Eigen::Vector3d(1, 1, 1);
    V.col(7) = Eigen::Vector3d(0, 1, 1);

    Eigen::Matrix<int, 3, Eigen::Dynamic> F(3, 12);
    // -Z face
    F.col(0)  = Eigen::Vector3i(0, 2, 1);
    F.col(1)  = Eigen::Vector3i(0, 3, 2);
    // +Z face
    F.col(2)  = Eigen::Vector3i(4, 5, 6);
    F.col(3)  = Eigen::Vector3i(4, 6, 7);
    // -Y face
    F.col(4)  = Eigen::Vector3i(0, 1, 5);
    F.col(5)  = Eigen::Vector3i(0, 5, 4);
    // +Y face
    F.col(6)  = Eigen::Vector3i(2, 3, 7);
    F.col(7)  = Eigen::Vector3i(2, 7, 6);
    // -X face
    F.col(8)  = Eigen::Vector3i(0, 4, 7);
    F.col(9)  = Eigen::Vector3i(0, 7, 3);
    // +X face
    F.col(10) = Eigen::Vector3i(1, 2, 6);
    F.col(11) = Eigen::Vector3i(1, 6, 5);

    return geometry::TriangleMesh(V, F);
}

} // namespace

TEST(MeshReference, ClosestPointOutside) {
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);

    // Point above the +Z face at (0.5, 0.5, 2.0).
    auto r = ref->closest_point_on_surface(Eigen::Vector3d(0.5, 0.5, 2.0));
    EXPECT_EQ(r.status, geometry::SurfaceQueryStatus::OK);
    EXPECT_NEAR(r.point.x(), 0.5, 1e-10);
    EXPECT_NEAR(r.point.y(), 0.5, 1e-10);
    EXPECT_NEAR(r.point.z(), 1.0, 1e-10);
    EXPECT_NEAR(r.unsigned_distance, 1.0, 1e-10);
    EXPECT_GT(r.signed_distance, 0);  // outside
}

TEST(MeshReference, ClosestPointOnSurface) {
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);

    // Point exactly on the surface.
    auto r = ref->closest_point_on_surface(Eigen::Vector3d(0.5, 0.5, 1.0));
    EXPECT_EQ(r.status, geometry::SurfaceQueryStatus::OK);
    EXPECT_NEAR(r.unsigned_distance, 0.0, 1e-10);
}

TEST(MeshReference, NormalAt) {
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);

    // Normal on the +Z face.
    auto nr = ref->normal_at(Eigen::Vector3d(0.5, 0.5, 1.0));
    EXPECT_EQ(nr.status, geometry::SurfaceQueryStatus::OK);
    EXPECT_NEAR(std::abs(nr.unit_normal.z()), 1.0, 1e-10);
}

TEST(MeshReference, TypeName) {
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);
    EXPECT_EQ(ref->type_name(), "mesh");
}

TEST(MeshReference, DeviationViaAbstraction) {
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);

    // 4 points at known distances from the cube surface.
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 4);
    pts.col(0) = Eigen::Vector3d(0.5, 0.5, 1.5);  // 0.5 above +Z
    pts.col(1) = Eigen::Vector3d(0.5, 0.5, 0.5);  // inside (0.5 from nearest face)
    pts.col(2) = Eigen::Vector3d(0.5, 0.5, 1.0);  // on surface
    pts.col(3) = Eigen::Vector3d(-0.3, 0.5, 0.5); // 0.3 outside -X face

    analysis::DeviationSettings settings;
    settings.tolerance = 1.0;

    auto dev = analysis::compute_deviation(pts, *ref, settings);
    EXPECT_TRUE(dev.success);
    EXPECT_EQ(dev.deviations.size(), 4u);

    // Point 0: 0.5 above +Z face.
    EXPECT_NEAR(dev.deviations[0].distance, 0.5, 1e-10);

    // Point 2: on surface.
    EXPECT_NEAR(dev.deviations[2].distance, 0.0, 1e-10);

    // Point 3: 0.3 outside -X face.
    EXPECT_NEAR(dev.deviations[3].distance, 0.3, 1e-10);
}

TEST(MeshReference, DeviationMeshVsAbstractionConsistency) {
    // The ReferenceGeometry overload must produce the same results as the
    // direct TriangleMesh overload for the mesh tier.
    auto cube = make_unit_cube();
    auto ref = geometry::make_mesh_reference(cube);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 3);
    pts.col(0) = Eigen::Vector3d(0.5, 0.5, 1.5);
    pts.col(1) = Eigen::Vector3d(2.0, 0.5, 0.5);
    pts.col(2) = Eigen::Vector3d(0.5, -1.0, 0.5);

    analysis::DeviationSettings settings;
    settings.tolerance = 1.0;

    auto dev_mesh = analysis::compute_deviation(pts, cube, settings);
    auto dev_ref = analysis::compute_deviation(pts, *ref, settings);

    ASSERT_TRUE(dev_mesh.success);
    ASSERT_TRUE(dev_ref.success);
    ASSERT_EQ(dev_mesh.deviations.size(), dev_ref.deviations.size());

    for (std::size_t i = 0; i < dev_mesh.deviations.size(); ++i) {
        EXPECT_NEAR(dev_mesh.deviations[i].distance,
                    dev_ref.deviations[i].distance, 1e-12)
            << "Point " << i << " unsigned distance mismatch";
    }
}

TEST(StepDetection, LooksLikeStep) {
    // Positive: valid STEP header.
    {
        const char* header = "ISO-10303-21;\nHEADER;\n";
        auto data = reinterpret_cast<const uint8_t*>(header);
        EXPECT_TRUE(alignmesh::io::looks_like_step(data, std::strlen(header)));
    }
    // Positive: with BOM.
    {
        std::string bom_header = "\xEF\xBB\xBFISO-10303-21;\n";
        auto data = reinterpret_cast<const uint8_t*>(bom_header.data());
        EXPECT_TRUE(alignmesh::io::looks_like_step(data, bom_header.size()));
    }
    // Negative: STL file.
    {
        const char* stl = "solid test\n";
        auto data = reinterpret_cast<const uint8_t*>(stl);
        EXPECT_FALSE(alignmesh::io::looks_like_step(data, std::strlen(stl)));
    }
    // Negative: too short.
    {
        const char* s = "ISO";
        auto data = reinterpret_cast<const uint8_t*>(s);
        EXPECT_FALSE(alignmesh::io::looks_like_step(data, 3));
    }
}

#if ALIGNMESH_WITH_STEP

TEST(StepIO, ImportNonexistentFile) {
    std::vector<uint8_t> bytes = {'I', 'S', 'O', '-', '1', '0', '3', '0',
                                   '3', '-', '2', '1', ';', '\n'};
    auto result = alignmesh::io::import_step(
        "nonexistent.step", bytes, "deadbeef");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errors.empty());
}

#else

TEST(StepIO, NotCompiledError) {
    std::vector<uint8_t> bytes;
    auto result = alignmesh::io::import_step("test.step", bytes, "hash");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errors.empty());
    // Should mention ALIGNMESH_WITH_STEP.
    bool mentions_step = result.errors[0].find("STEP support not compiled") !=
                         std::string::npos;
    EXPECT_TRUE(mentions_step);
}

#endif
