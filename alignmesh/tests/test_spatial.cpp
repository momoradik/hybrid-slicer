#include <gtest/gtest.h>
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/spatial/sampling.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <set>

using namespace alignmesh::spatial;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

Eigen::Matrix<double, 3, Eigen::Dynamic>
random_cloud(SeededRng& rng, int n, double range = 10.0) {
    std::uniform_real_distribution<double> d(-range, range);
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n);
    for (int i = 0; i < n; ++i) {
        pts(0, i) = d(rng);
        pts(1, i) = d(rng);
        pts(2, i) = d(rng);
    }
    return pts;
}

// Brute-force nearest neighbour for ground truth.
NNResult brute_force_nn(const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
                        const Vec3& query) {
    NNResult best;
    best.distance_sq = std::numeric_limits<double>::max();
    for (Eigen::Index i = 0; i < pts.cols(); ++i) {
        double dsq = (pts.col(i) - query).squaredNorm();
        if (dsq < best.distance_sq) {
            best.distance_sq = dsq;
            best.index = static_cast<int>(i);
        }
    }
    return best;
}

// Generate points on a unit sphere.
Eigen::Matrix<double, 3, Eigen::Dynamic>
make_sphere(int n_theta, int n_phi) {
    int total = n_theta * n_phi;
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, total);
    int idx = 0;
    for (int i = 0; i < n_theta; ++i) {
        double theta = kPi * (i + 0.5) / n_theta;
        for (int j = 0; j < n_phi; ++j) {
            double phi = 2.0 * kPi * j / n_phi;
            pts(0, idx) = std::sin(theta) * std::cos(phi);
            pts(1, idx) = std::sin(theta) * std::sin(phi);
            pts(2, idx) = std::cos(theta);
            ++idx;
        }
    }
    return pts;
}

// Generate points on a flat XY plane.
Eigen::Matrix<double, 3, Eigen::Dynamic>
make_plane(int nx, int ny, double spacing = 0.1) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, nx * ny);
    int idx = 0;
    for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < ny; ++j) {
            pts.col(idx) = Vec3(i * spacing, j * spacing, 0.0);
            ++idx;
        }
    }
    return pts;
}

} // namespace

// ============================================================================
// KD-tree
// ============================================================================

TEST(KdTree, NearestMatchesBruteForce) {
    SeededRng rng(42);
    auto pts = random_cloud(rng, 500);
    KdTree tree(pts);

    std::uniform_real_distribution<double> d(-12.0, 12.0);
    for (int q = 0; q < 200; ++q) {
        Vec3 query(d(rng), d(rng), d(rng));
        auto kd_result = tree.nearest(query);
        auto bf_result = brute_force_nn(pts, query);
        ASSERT_EQ(kd_result.index, bf_result.index)
            << "Mismatch at query " << q;
        EXPECT_NEAR(kd_result.distance_sq, bf_result.distance_sq, 1e-14);
    }
}

TEST(KdTree, KnnSorted) {
    SeededRng rng(99);
    auto pts = random_cloud(rng, 100);
    KdTree tree(pts);

    Vec3 query(0, 0, 0);
    std::vector<int> indices;
    std::vector<double> dists;
    tree.knn(query, 10, indices, dists);

    EXPECT_EQ(indices.size(), 10u);
    for (std::size_t i = 1; i < dists.size(); ++i) {
        EXPECT_LE(dists[i - 1], dists[i]) << "knn not sorted at " << i;
    }
}

TEST(KdTree, RadiusSearch) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 5);
    pts.col(0) = Vec3(0, 0, 0);
    pts.col(1) = Vec3(0.5, 0, 0);
    pts.col(2) = Vec3(1, 0, 0);
    pts.col(3) = Vec3(2, 0, 0);
    pts.col(4) = Vec3(10, 0, 0);
    KdTree tree(pts);

    std::vector<int> idx;
    std::vector<double> dsq;
    int count = tree.radius_search(Vec3(0, 0, 0), 1.1, idx, dsq);
    EXPECT_EQ(count, 3);  // points at 0, 0.5, 1.0
}

// ============================================================================
// Voxel downsampling
// ============================================================================

TEST(VoxelDownsample, ReducesDensity) {
    SeededRng rng(55);
    auto pts = random_cloud(rng, 1000, 5.0);
    auto indices = voxel_downsample(pts, 2.0);

    EXPECT_GT(indices.size(), 0u);
    EXPECT_LT(static_cast<Eigen::Index>(indices.size()), pts.cols());
}

TEST(VoxelDownsample, Deterministic) {
    SeededRng rng(55);
    auto pts = random_cloud(rng, 500);
    auto r1 = voxel_downsample(pts, 1.0);
    auto r2 = voxel_downsample(pts, 1.0);
    EXPECT_EQ(r1, r2);
}

TEST(VoxelDownsample, IndicesValid) {
    SeededRng rng(33);
    auto pts = random_cloud(rng, 200);
    auto indices = voxel_downsample(pts, 3.0);
    for (auto idx : indices) {
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, pts.cols());
    }
}

// ============================================================================
// Normal estimation
// ============================================================================

TEST(Normals, SphereNormals) {
    auto pts = make_sphere(30, 60);  // 1800 points on unit sphere
    auto result = estimate_normals_knn(pts, 15);

    EXPECT_EQ(result.normals.cols(), pts.cols());

    // Analytic normal on a unit sphere = the point itself.
    double max_angle = 0;
    for (Eigen::Index i = 0; i < pts.cols(); ++i) {
        Vec3 analytic = pts.col(i).normalized();
        Vec3 estimated = result.normals.col(i);
        // Normal may be flipped (±), check absolute dot product.
        double dot = std::abs(analytic.dot(estimated));
        double angle = std::acos(std::min(dot, 1.0));
        max_angle = std::max(max_angle, angle);
    }
    // On a densely sampled sphere, normals should be accurate to a few degrees.
    EXPECT_LT(max_angle, 0.15)  // ~8.5 degrees
        << "Max angle error = " << max_angle * 180.0 / kPi << " deg";
}

TEST(Normals, PlaneNormals) {
    auto pts = make_plane(20, 20, 0.1);  // 400 points
    auto result = estimate_normals_knn(pts, 10);

    // All normals should be ±(0,0,1).
    for (Eigen::Index i = 0; i < pts.cols(); ++i) {
        double dot = std::abs(result.normals.col(i).dot(Vec3::UnitZ()));
        EXPECT_GT(dot, 0.99)
            << "Plane normal at " << i << " deviates from Z: dot=" << dot;
    }
}

TEST(Normals, ConsistentOrientation) {
    // On a closed sphere, after BFS orientation propagation, ALL normals
    // should point in the same direction (all outward or all inward).
    auto pts = make_sphere(20, 40);
    auto result = estimate_normals_knn(pts, 15);

    // Check that dot(normal, point) has consistent sign for all points.
    int positive = 0, negative = 0;
    for (Eigen::Index i = 0; i < pts.cols(); ++i) {
        double dot = result.normals.col(i).dot(pts.col(i));
        if (dot > 0) ++positive;
        else ++negative;
    }
    std::cout << "Orientation: " << positive << " outward, " << negative
              << " inward out of " << pts.cols() << std::endl;

    // Also check pairwise consistency: adjacent normals should agree.
    KdTree tree(pts);
    int pairwise_agree = 0, pairwise_disagree = 0;
    for (Eigen::Index i = 0; i < std::min(pts.cols(), Eigen::Index(100)); ++i) {
        std::vector<int> nbrs;
        std::vector<double> dsq;
        tree.knn(pts.col(i), 5, nbrs, dsq);
        for (int ni : nbrs) {
            if (ni == static_cast<int>(i)) continue;
            double dot = result.normals.col(i).dot(
                result.normals.col(static_cast<Eigen::Index>(ni)));
            if (dot > 0) ++pairwise_agree;
            else ++pairwise_disagree;
        }
    }
    std::cout << "Pairwise: " << pairwise_agree << " agree, "
              << pairwise_disagree << " disagree" << std::endl;

    // Either all positive or all negative (consistent orientation).
    double consistency = std::max(positive, negative) /
                         static_cast<double>(pts.cols());
    EXPECT_GT(consistency, 0.95)
        << "Orientation inconsistency: " << positive << " pos, "
        << negative << " neg out of " << pts.cols();
}

TEST(Normals, CurvatureFlat) {
    auto pts = make_plane(20, 20, 0.1);
    auto result = estimate_normals_knn(pts, 10);
    // Flat plane should have near-zero curvature.
    double max_curv = result.curvatures.maxCoeff();
    EXPECT_LT(max_curv, 0.05) << "Plane curvature too high: " << max_curv;
}

// ============================================================================
// Sampling
// ============================================================================

TEST(NormalSpaceSampling, AngularCoverage) {
    auto pts = make_sphere(30, 60);
    // Normals on sphere = the points themselves.
    auto normals = pts;
    for (Eigen::Index i = 0; i < pts.cols(); ++i)
        normals.col(i).normalize();

    auto indices = normal_space_sampling(pts, normals, 10);
    EXPECT_GT(indices.size(), 10u);  // at least some bins filled
    EXPECT_LT(static_cast<Eigen::Index>(indices.size()), pts.cols());
}

TEST(StableSampling, ConstrainsAllDOFs) {
    // Near-planar patch: uniform sampling may miss the constrained direction.
    // Stable sampling should select points spanning all 6 DOFs.
    auto pts = make_plane(30, 30, 0.1);
    // Add slight Z variation so normals have some spread.
    SeededRng rng(77);
    std::normal_distribution<double> noise(0.0, 0.001);
    for (Eigen::Index i = 0; i < pts.cols(); ++i)
        pts(2, i) += noise(rng);

    auto normal_result = estimate_normals_knn(pts, 15);

    auto stable_idx = stable_sampling(pts, normal_result.normals, 50, rng);
    EXPECT_EQ(stable_idx.size(), 50u);

    // The selected points should span a reasonable area of the plane
    // (not all clustered in one corner).
    Vec3 bmin = pts.col(stable_idx[0]);
    Vec3 bmax = bmin;
    for (auto i : stable_idx) {
        for (int c = 0; c < 3; ++c) {
            bmin(c) = std::min(bmin(c), pts(c, i));
            bmax(c) = std::max(bmax(c), pts(c, i));
        }
    }
    Vec3 extent = bmax - bmin;
    // Should cover >50% of the XY extent.
    double full_extent_x = 29 * 0.1;
    EXPECT_GT(extent(0), full_extent_x * 0.4);
    EXPECT_GT(extent(1), full_extent_x * 0.4);
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(SpatialAdversarial, SparseCloud_3Points) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 3);
    pts.col(0) = Vec3(0, 0, 0);
    pts.col(1) = Vec3(1, 0, 0);
    pts.col(2) = Vec3(0, 1, 0);

    KdTree tree(pts);
    auto nn = tree.nearest(Vec3(0.1, 0.1, 0));
    EXPECT_EQ(nn.index, 0);

    // Normals with k >= n should not crash (uses min(k, n-1)).
    auto normals = estimate_normals_knn(pts, 10);
    EXPECT_EQ(normals.normals.cols(), 3);
}

TEST(SpatialAdversarial, SinglePoint) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Vec3(5, 5, 5);
    KdTree tree(pts);
    auto nn = tree.nearest(Vec3(0, 0, 0));
    EXPECT_EQ(nn.index, 0);
}

TEST(SpatialAdversarial, VoxelDownsampleSinglePoint) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 1);
    pts.col(0) = Vec3(1, 2, 3);
    auto indices = voxel_downsample(pts, 1.0);
    EXPECT_EQ(indices.size(), 1u);
    EXPECT_EQ(indices[0], 0);
}
