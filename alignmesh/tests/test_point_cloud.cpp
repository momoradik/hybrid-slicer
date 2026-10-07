#include <gtest/gtest.h>
#include "alignmesh/geometry/point_cloud.h"

#include <numbers>

using namespace alignmesh::geometry;

TEST(PointCloud, ConstructFromPoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 4);
    pts << 1, 2, 3, 4,
           5, 6, 7, 8,
           9, 10, 11, 12;
    PointCloud pc(pts);

    EXPECT_EQ(pc.size(), 4);
    EXPECT_FALSE(pc.has_normals());
    EXPECT_FALSE(pc.has_covariances());
    EXPECT_FALSE(pc.is_source());
    EXPECT_TRUE(pc.points().isApprox(pts));
}

TEST(PointCloud, ConstructWithNormals) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts << 1, 2, 3, 4, 5, 6;

    Eigen::Matrix<double, 3, Eigen::Dynamic> normals(3, 2);
    normals << 0, 0, 0, 0, 1, 1;

    PointCloud pc(pts, normals, std::nullopt);

    EXPECT_TRUE(pc.has_normals());
    EXPECT_TRUE(pc.normals().isApprox(normals));
    EXPECT_FALSE(pc.has_covariances());
}

TEST(PointCloud, ConstructWithCovariances) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 1, 2, 3;

    std::vector<Mat3> covs = {Mat3::Identity() * 0.01};

    PointCloud pc(pts, std::nullopt, covs);

    EXPECT_TRUE(pc.has_covariances());
    EXPECT_EQ(pc.covariances().size(), 1u);
    EXPECT_TRUE(pc.covariances()[0].isApprox(Mat3::Identity() * 0.01));
}

TEST(PointCloud, SourceTracking) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 0, 0, 0;

    PointCloud source(pts, std::nullopt, std::nullopt, "abc123hash");
    EXPECT_TRUE(source.is_source());
    EXPECT_EQ(source.source_hash(), "abc123hash");
}

TEST(PointCloud, TransformPoints) {
    // 90 deg CCW around Z, no translation.
    Mat3 R;
    R << 0, -1, 0,
         1,  0, 0,
         0,  0, 1;
    auto T = RigidTransform::from_rotation_translation(R, Vec3::Zero());

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 2);
    pts.col(0) = Vec3(1, 0, 0);
    pts.col(1) = Vec3(0, 1, 0);
    PointCloud pc(pts);

    auto pc2 = pc.transformed(T);

    // (1,0,0) -> (0,1,0)
    EXPECT_NEAR(pc2.points()(0, 0), 0.0, 1e-15);
    EXPECT_NEAR(pc2.points()(1, 0), 1.0, 1e-15);

    // (0,1,0) -> (-1,0,0)
    EXPECT_NEAR(pc2.points()(0, 1), -1.0, 1e-15);
    EXPECT_NEAR(pc2.points()(1, 1),  0.0, 1e-15);
}

TEST(PointCloud, TransformNormals) {
    // Normals rotate by R only (no translation).
    Mat3 R;
    R << 0, -1, 0,
         1,  0, 0,
         0,  0, 1;
    auto T = RigidTransform::from_rotation_translation(R, Vec3(100, 200, 300));

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 0, 0, 0;

    Eigen::Matrix<double, 3, Eigen::Dynamic> normals(3, 1);
    normals << 1, 0, 0;  // +X normal

    PointCloud pc(pts, normals, std::nullopt);
    auto pc2 = pc.transformed(T);

    EXPECT_TRUE(pc2.has_normals());
    EXPECT_NEAR(pc2.normals()(0, 0), 0.0, 1e-15);
    EXPECT_NEAR(pc2.normals()(1, 0), 1.0, 1e-15);
    EXPECT_NEAR(pc2.normals()(2, 0), 0.0, 1e-15);
}

TEST(PointCloud, TransformCovariances) {
    // Under rigid transform: S' = R*S*R^T
    Mat3 R;
    R << 0, -1, 0,
         1,  0, 0,
         0,  0, 1;
    auto T = RigidTransform::from_rotation_translation(R, Vec3(1, 2, 3));

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 0, 0, 0;

    Mat3 cov;
    cov << 4, 0, 0,
           0, 1, 0,
           0, 0, 1;
    std::vector<Mat3> covs = {cov};

    PointCloud pc(pts, std::nullopt, covs);
    auto pc2 = pc.transformed(T);

    EXPECT_TRUE(pc2.has_covariances());
    Mat3 expected = R * cov * R.transpose();
    EXPECT_TRUE(pc2.covariances()[0].isApprox(expected, 1e-14));
}

TEST(PointCloud, TransformedNotSource) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 0, 0, 0;

    PointCloud source(pts, std::nullopt, std::nullopt, "hash");
    EXPECT_TRUE(source.is_source());

    auto derived = source.transformed(RigidTransform());
    EXPECT_FALSE(derived.is_source());
    EXPECT_TRUE(derived.source_hash().empty());
}

TEST(PointCloud, TransformDoesNotMutateSource) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts << 1, 2, 3;

    PointCloud source(pts, std::nullopt, std::nullopt, "immutable");
    auto T = RigidTransform::from_rotation_translation(
        Mat3::Identity(), Vec3(10, 20, 30));

    auto derived = source.transformed(T);

    // Source must be unchanged.
    EXPECT_NEAR(source.points()(0, 0), 1.0, 1e-15);
    EXPECT_NEAR(source.points()(1, 0), 2.0, 1e-15);
    EXPECT_NEAR(source.points()(2, 0), 3.0, 1e-15);

    // Derived has new values.
    EXPECT_NEAR(derived.points()(0, 0), 11.0, 1e-15);
    EXPECT_NEAR(derived.points()(1, 0), 22.0, 1e-15);
    EXPECT_NEAR(derived.points()(2, 0), 33.0, 1e-15);
}
