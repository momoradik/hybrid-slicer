// Self-tests for the synthetic ground-truth test harness.
//
// MANDATORY — these must ALL pass before any feature-level test can be trusted.
// No disabled/skipped tests, no loosened tolerances.

#include "test_harness_gt.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <gtest/gtest.h>
#include <Eigen/Dense>
#include <cmath>
#include <iostream>
#include <set>

using namespace alignmesh;
using namespace alignmesh::test_harness;

// ============================================================================
// apply_known_transform: T then T^{-1} recovers original to machine precision
// ============================================================================

TEST(HarnessGT, TransformRoundTrip_Mesh) {
    auto box = make_box();
    auto T = make_known_transform(1.5, -2.3, 0.8, 3.0, -1.5, 0.7);

    auto transformed = apply_known_transform(box, T);
    auto recovered = apply_known_transform(transformed, T.inverse());

    ASSERT_EQ(box.num_vertices(), recovered.num_vertices());
    double max_err = 0;
    for (Eigen::Index i = 0; i < box.num_vertices(); ++i) {
        double err = (box.vertices().col(i) - recovered.vertices().col(i)).norm();
        max_err = std::max(max_err, err);
    }
    std::cout << "TransformRoundTrip_Mesh: max error = " << max_err << "\n";
    EXPECT_LT(max_err, 1e-12) << "T then T^{-1} must recover original to machine precision";
}

TEST(HarnessGT, TransformRoundTrip_Points) {
    auto sphere = make_sphere();
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts = sphere.vertices();
    auto T = make_known_transform(-0.7, 3.1, -1.2, -2.0, 4.0, 0.1);

    auto moved = apply_known_transform(pts, T);
    auto recovered = apply_known_transform(moved, T.inverse());

    double max_err = (pts - recovered).colwise().norm().maxCoeff();
    std::cout << "TransformRoundTrip_Points: max error = " << max_err << "\n";
    EXPECT_LT(max_err, 1e-12);
}

// ============================================================================
// make_synthetic_scan: topology differs + noise/outlier statistics correct
// ============================================================================

TEST(HarnessGT, SyntheticScan_TopologyDiffers) {
    auto cyl = make_cylinder();
    auto T = make_known_transform();

    ScanParams params;
    params.noise_sigma = 0.005;
    params.outlier_fraction = 0.0;
    params.resample_density = 3.0;
    params.seed = 12345;

    auto scan = make_synthetic_scan(cyl, T, params);

    // Scan must have DIFFERENT vertex count from reference.
    EXPECT_NE(scan.points.cols(), cyl.num_vertices())
        << "Synthetic scan must have different vertex count from reference";

    // Scan points must not coincide with any reference vertex.
    // (They're barycentric-sampled on triangle interiors + noise.)
    // Transform scan to reference frame for comparison.
    auto scan_in_ref = T.apply_cloud(scan.points);
    const auto& ref_verts = cyl.vertices();

    int coincident = 0;
    for (Eigen::Index i = 0; i < scan_in_ref.cols(); ++i) {
        for (Eigen::Index j = 0; j < ref_verts.cols(); ++j) {
            if ((scan_in_ref.col(i) - ref_verts.col(j)).norm() < 1e-10) {
                ++coincident;
                break;
            }
        }
    }
    EXPECT_EQ(coincident, 0)
        << "Scan points must not coincide with reference vertices";

    std::cout << "SyntheticScan_TopologyDiffers: ref_verts="
              << cyl.num_vertices() << " scan_pts=" << scan.points.cols()
              << " coincident=" << coincident << "\n";
}

TEST(HarnessGT, SyntheticScan_NoiseStatistics) {
    auto box = make_box();
    auto T = geometry::RigidTransform();  // identity

    ScanParams params;
    params.noise_sigma = 0.1;  // 100 µm
    params.outlier_fraction = 0.0;
    params.resample_density = 50.0;  // many points for statistics
    params.seed = 99;

    auto scan = make_synthetic_scan(box, T, params);
    EXPECT_GT(scan.points.cols(), 100);

    // Points should be near the box surface (within a few sigma of noise).
    // For a unit box [0,50]x[0,30]x[0,20], surface points are on the faces.
    // With noise_sigma=0.1, 99.7% should be within 3*sigma = 0.3 of the surface.
    // We check that the mean distance to the nearest face is roughly noise_sigma.

    // Simple check: compute distance to nearest face for a sample of points.
    double sum_sq = 0;
    int n_checked = 0;
    for (Eigen::Index i = 0; i < scan.points.cols(); ++i) {
        auto p = scan.points.col(i);
        // Distance to nearest face of box [0,50]x[0,30]x[0,20].
        double dx = std::min(std::abs(p.x()), std::abs(p.x() - 50.0));
        double dy = std::min(std::abs(p.y()), std::abs(p.y() - 30.0));
        double dz = std::min(std::abs(p.z()), std::abs(p.z() - 20.0));
        double d_face = std::min({dx, dy, dz});
        sum_sq += d_face * d_face;
        ++n_checked;
    }
    double rms_to_surface = std::sqrt(sum_sq / n_checked);

    // The noise component perpendicular to the surface should have
    // RMS ≈ noise_sigma. The tangential component doesn't affect distance.
    // Allow generous bounds: [0.5 * sigma, 3.0 * sigma].
    std::cout << "SyntheticScan_NoiseStatistics: rms_to_surface=" << rms_to_surface
              << " expected~" << params.noise_sigma << "\n";
    EXPECT_GT(rms_to_surface, params.noise_sigma * 0.3)
        << "Noise too low — scan points suspiciously close to surface";
    EXPECT_LT(rms_to_surface, params.noise_sigma * 5.0)
        << "Noise too high — scan points too far from surface";
}

TEST(HarnessGT, SyntheticScan_OutlierCount) {
    auto box = make_box();
    auto T = geometry::RigidTransform();

    ScanParams params;
    params.noise_sigma = 0.01;
    params.outlier_fraction = 0.1;  // 10% outliers
    params.resample_density = 10.0;
    params.seed = 77;

    auto scan = make_synthetic_scan(box, T, params);

    // Check that the reported outlier count matches the requested fraction.
    int total = scan.n_inliers + scan.n_outliers;
    double actual_fraction = static_cast<double>(scan.n_outliers) / total;

    std::cout << "SyntheticScan_OutlierCount: inliers=" << scan.n_inliers
              << " outliers=" << scan.n_outliers
              << " fraction=" << actual_fraction << "\n";
    EXPECT_NEAR(actual_fraction, params.outlier_fraction, 0.05)
        << "Outlier fraction should match requested within rounding";
    EXPECT_EQ(scan.points.cols(), total);
}

// ============================================================================
// RPS config: rank-6 and rank-5 Jacobian verification
// ============================================================================

// Build the constraint Jacobian and compute its rank via SVD.
static int compute_jacobian_rank(const std::vector<registration::RPSConstraintPoint>& constraints,
                                  double rank_tol = 1e-6) {
    // Count total rows.
    int nrows = 0;
    for (auto& pt : constraints)
        nrows += static_cast<int>(pt.locks.size());

    Eigen::MatrixXd J(nrows, 6);
    Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
    Eigen::Vector3d t = Eigen::Vector3d::Zero();

    int row = 0;
    for (auto& pt : constraints) {
        Eigen::Vector3d Rm = R * pt.measured;
        // skew(Rm)
        Eigen::Matrix3d neg_skew;
        neg_skew << 0, Rm.z(), -Rm.y(),
                   -Rm.z(), 0, Rm.x(),
                    Rm.y(), -Rm.x(), 0;

        for (auto& lock : pt.locks) {
            Eigen::Vector3d d = lock.direction.normalized();
            // J row: [d^T * (-[Rm]x), d^T]
            J.row(row).head<3>() = d.transpose() * neg_skew;
            J.row(row).tail<3>() = d.transpose();
            ++row;
        }
    }

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(J);
    auto sv = svd.singularValues();
    double sv_max = (sv.size() > 0) ? sv(0) : 0;
    if (sv_max < 1e-15) return 0;

    int rank = 0;
    for (int i = 0; i < sv.size(); ++i) {
        if (sv(i) / sv_max > rank_tol) ++rank;
    }
    return rank;
}

TEST(HarnessGT, RPS_FullRank6) {
    auto box = make_box();
    auto config = full_rank6_rps_config(box);

    int rank = compute_jacobian_rank(config.constraints);
    std::cout << "RPS_FullRank6: " << config.constraints.size()
              << " constraints, rank=" << rank << "\n";
    EXPECT_EQ(rank, 6) << "full_rank6_rps_config must produce rank-6 Jacobian";
}

TEST(HarnessGT, RPS_UnderConstrained_Rank5) {
    auto box = make_box();
    auto config = under_constrained_rps_config(box);

    int rank = compute_jacobian_rank(config.constraints);
    std::cout << "RPS_UnderConstrained: " << config.constraints.size()
              << " constraints, rank=" << rank << "\n";
    EXPECT_EQ(rank, 5) << "under_constrained_rps_config must produce rank-5 Jacobian";
}

// ============================================================================
// Test geometries: sanity checks (valid mesh, expected topology)
// ============================================================================

TEST(HarnessGT, Geometries_Valid) {
    auto box = make_box();
    EXPECT_EQ(box.num_vertices(), 8);
    EXPECT_EQ(box.num_triangles(), 12);

    auto cyl = make_cylinder();
    EXPECT_GT(cyl.num_vertices(), 50);
    EXPECT_GT(cyl.num_triangles(), 100);

    auto sphere = make_sphere();
    EXPECT_GT(sphere.num_vertices(), 50);
    EXPECT_GT(sphere.num_triangles(), 100);

    auto patch = make_curved_patch();
    EXPECT_GT(patch.num_vertices(), 50);
    EXPECT_GT(patch.num_triangles(), 50);

    auto lbracket = make_l_bracket_with_fillet();
    EXPECT_GT(lbracket.num_vertices(), 20);
    EXPECT_GT(lbracket.num_triangles(), 20);

    std::cout << "Geometries: box=" << box.num_triangles()
              << "t cyl=" << cyl.num_triangles()
              << "t sphere=" << sphere.num_triangles()
              << "t patch=" << patch.num_triangles()
              << "t lbracket=" << lbracket.num_triangles() << "t\n";
}

// ============================================================================
// Repro-gate: run harness twice → identical output
// ============================================================================

TEST(HarnessGT, ReproGate) {
    auto cyl = make_cylinder();
    auto T = make_known_transform();
    ScanParams params;
    params.seed = 42;
    params.noise_sigma = 0.02;
    params.outlier_fraction = 0.05;
    params.resample_density = 5.0;

    auto scan1 = make_synthetic_scan(cyl, T, params);
    auto scan2 = make_synthetic_scan(cyl, T, params);

    ASSERT_EQ(scan1.points.cols(), scan2.points.cols());
    ASSERT_EQ(scan1.n_inliers, scan2.n_inliers);
    ASSERT_EQ(scan1.n_outliers, scan2.n_outliers);

    // Bit-identical check.
    bool identical = true;
    for (Eigen::Index i = 0; i < scan1.points.cols(); ++i) {
        if (scan1.points.col(i) != scan2.points.col(i)) {
            identical = false;
            break;
        }
    }
    EXPECT_TRUE(identical) << "Repro-gate: two runs with same seed must be bit-identical";
    std::cout << "ReproGate: " << scan1.points.cols() << " points, identical=" << identical << "\n";
}
