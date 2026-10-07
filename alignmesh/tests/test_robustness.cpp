#include <gtest/gtest.h>
#include "alignmesh/registration/robustness.h"
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace alignmesh::registration;
using namespace alignmesh::alignment;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

// Generate N paired correspondences from a known transform, with a fraction
// of gross outliers added to the target.
struct TestCorrespondences {
    Eigen::Matrix<double, 3, Eigen::Dynamic> source;
    Eigen::Matrix<double, 3, Eigen::Dynamic> target;
    RigidTransform T_true;
    int n_inliers;
    int n_outliers;
};

TestCorrespondences make_test_data(
        SeededRng& rng, int n_inlier, int n_outlier,
        double noise_sigma = 0.001, double outlier_range = 10.0) {
    TestCorrespondences tc;
    int n = n_inlier + n_outlier;
    tc.source.resize(3, n);
    tc.target.resize(3, n);
    tc.n_inliers = n_inlier;
    tc.n_outliers = n_outlier;

    // Known transform
    Mat3 R = Eigen::AngleAxisd(0.3, Vec3(0.2, 0.5, 0.8).normalized())
                 .toRotationMatrix();
    Vec3 t(1.0, -0.5, 0.3);
    tc.T_true = RigidTransform::from_rotation_translation(R, t);

    std::uniform_real_distribution<double> pos(-5.0, 5.0);
    std::normal_distribution<double> noise(0.0, noise_sigma);
    std::uniform_real_distribution<double> outlier(-outlier_range, outlier_range);

    // Inliers: correct correspondences with small noise
    for (int i = 0; i < n_inlier; ++i) {
        Vec3 s(pos(rng), pos(rng), pos(rng));
        Vec3 tgt = tc.T_true.apply(s);
        tgt += Vec3(noise(rng), noise(rng), noise(rng));
        tc.source.col(i) = s;
        tc.target.col(i) = tgt;
    }

    // Outliers: random target points (no relation to source)
    for (int i = 0; i < n_outlier; ++i) {
        int idx = n_inlier + i;
        tc.source.col(idx) = Vec3(pos(rng), pos(rng), pos(rng));
        tc.target.col(idx) = Vec3(outlier(rng), outlier(rng), outlier(rng));
    }

    return tc;
}

} // namespace

// ============================================================================
// Robust kernels
// ============================================================================

TEST(RobustKernels, HuberWeight) {
    double c = 1.0;
    // Inside threshold: weight = 1
    EXPECT_NEAR(robust_weight(RobustKernel::HUBER, 0.5 * 0.5, c), 1.0, 1e-10);
    // Outside threshold: weight < 1
    double w = robust_weight(RobustKernel::HUBER, 3.0 * 3.0, c);
    EXPECT_GT(w, 0.0);
    EXPECT_LT(w, 1.0);
}

TEST(RobustKernels, TukeyWeight) {
    double c = 2.0;
    // Inside: weight > 0
    double w_in = robust_weight(RobustKernel::TUKEY, 1.0, c);
    EXPECT_GT(w_in, 0.0);
    // Far outside: weight = 0 (hard reject)
    double w_out = robust_weight(RobustKernel::TUKEY, 100.0, c);
    EXPECT_NEAR(w_out, 0.0, 1e-10);
}

TEST(RobustKernels, GemanMcClureWeight) {
    double c = 1.0;
    // At r=0: weight = 1
    EXPECT_NEAR(robust_weight(RobustKernel::GEMAN_MCCLURE, 0.0, c), 1.0, 1e-10);
    // Increases r → decreasing weight
    double w1 = robust_weight(RobustKernel::GEMAN_MCCLURE, 1.0, c);
    double w2 = robust_weight(RobustKernel::GEMAN_MCCLURE, 10.0, c);
    EXPECT_GT(w1, w2);
}

// ============================================================================
// Trimmed ICP
// ============================================================================

TEST(TrimmedICP, RecoversWithOutliers) {
    SeededRng rng(42);
    auto tc = make_test_data(rng, 80, 20);  // 20% outliers

    TrimmedICPSettings settings;
    settings.trim_fraction = 0.75;  // keep 75% (enough for the 80% inliers)

    auto result = trimmed_icp(tc.source, tc.target, settings);
    ASSERT_TRUE(result.success);

    // Should recover the known transform.
    auto T_err = result.transform * tc.T_true.inverse();
    EXPECT_TRUE(T_err.is_identity(0.1))
        << "Recovery error: " << (T_err.matrix() - Mat4::Identity()).norm();

    EXPECT_GT(result.overlap_percent, 50.0);
}

TEST(TrimmedICP, PlainLSFailsWithOutliers) {
    SeededRng rng(42);
    auto tc = make_test_data(rng, 80, 20);

    // Plain Kabsch (no trimming) should fail with outliers.
    auto plain = align_landmarks(tc.source, tc.target);
    ASSERT_TRUE(plain.success);

    auto T_err = plain.transform * tc.T_true.inverse();
    double plain_err = (T_err.matrix() - Mat4::Identity()).norm();

    // Now trimmed
    TrimmedICPSettings settings;
    settings.trim_fraction = 0.75;
    auto robust = trimmed_icp(tc.source, tc.target, settings);
    ASSERT_TRUE(robust.success);

    auto T_err2 = robust.transform * tc.T_true.inverse();
    double robust_err = (T_err2.matrix() - Mat4::Identity()).norm();

    EXPECT_LT(robust_err, plain_err)
        << "Trimmed (" << robust_err << ") should beat plain (" << plain_err << ")";
}

TEST(TrimmedICP, TrimFractionSweep) {
    SeededRng rng(55);
    auto tc = make_test_data(rng, 90, 10);  // 10% outliers

    // Sweeping trim_fraction: more trimming → fewer inliers used.
    for (double frac : {0.5, 0.7, 0.85, 0.95}) {
        TrimmedICPSettings settings;
        settings.trim_fraction = frac;
        auto result = trimmed_icp(tc.source, tc.target, settings);
        ASSERT_TRUE(result.success) << "Failed at trim_fraction=" << frac;
        // Inlier count should be approximately frac * N.
        int expected = static_cast<int>(frac * 100);
        EXPECT_NEAR(result.num_inliers, expected, 5)
            << "trim_fraction=" << frac;
    }
}

// ============================================================================
// M-estimator alignment
// ============================================================================

TEST(MEstimator, HuberRecoversWithOutliers) {
    SeededRng rng(77);
    auto tc = make_test_data(rng, 80, 20);

    MEstimatorSettings settings;
    settings.kernel = RobustKernel::HUBER;
    settings.kernel_param = 0.5;

    auto result = m_estimator_align(tc.source, tc.target, settings);
    ASSERT_TRUE(result.success);

    auto T_err = result.transform * tc.T_true.inverse();
    EXPECT_TRUE(T_err.is_identity(0.1))
        << "Huber error: " << (T_err.matrix() - Mat4::Identity()).norm();
}

TEST(MEstimator, TukeyRejectsOutliers) {
    SeededRng rng(99);
    auto tc = make_test_data(rng, 80, 20);

    MEstimatorSettings settings;
    settings.kernel = RobustKernel::TUKEY;
    settings.kernel_param = 1.0;

    auto result = m_estimator_align(tc.source, tc.target, settings);
    ASSERT_TRUE(result.success);
    EXPECT_GT(result.outlier_percent, 10.0);  // should detect outliers
}

// ============================================================================
// GNC
// ============================================================================

TEST(GNC, RecoversWithOutliers) {
    SeededRng rng(123);
    auto tc = make_test_data(rng, 80, 20);

    GNCSettings settings;
    settings.kernel_param = 0.5;

    auto result = gnc_align(tc.source, tc.target, settings);
    ASSERT_TRUE(result.success);

    auto T_err = result.transform * tc.T_true.inverse();
    EXPECT_TRUE(T_err.is_identity(0.1))
        << "GNC error: " << (T_err.matrix() - Mat4::Identity()).norm();
}

TEST(GNC, GraduatedWeights) {
    // GNC should produce weights between 0 and 1, with outliers near 0.
    SeededRng rng(200);
    auto tc = make_test_data(rng, 80, 20);

    GNCSettings settings;
    settings.kernel_param = 0.5;

    auto result = gnc_align(tc.source, tc.target, settings);
    ASSERT_TRUE(result.success);

    // Count how many of the known outlier indices got low weights.
    int correctly_downweighted = 0;
    for (int i = tc.n_inliers; i < tc.n_inliers + tc.n_outliers; ++i) {
        if (result.weights[static_cast<std::size_t>(i)] < 0.1)
            ++correctly_downweighted;
    }
    EXPECT_GT(correctly_downweighted, tc.n_outliers / 2)
        << "GNC should downweight most outliers";
}

// ============================================================================
// Determinism
// ============================================================================

TEST(Robustness, Determinism) {
    SeededRng rng1(42), rng2(42);
    auto tc1 = make_test_data(rng1, 80, 20);
    auto tc2 = make_test_data(rng2, 80, 20);

    TrimmedICPSettings settings;
    settings.trim_fraction = 0.75;

    auto r1 = trimmed_icp(tc1.source, tc1.target, settings);
    auto r2 = trimmed_icp(tc2.source, tc2.target, settings);

    ASSERT_TRUE(r1.success && r2.success);
    EXPECT_EQ(r1.transform.matrix(), r2.transform.matrix());
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(RobustnessAdversarial, FiftyPercentOutliers) {
    SeededRng rng(300);
    auto tc = make_test_data(rng, 50, 50);  // 50% outliers

    // Trimmed with aggressive trim
    TrimmedICPSettings settings;
    settings.trim_fraction = 0.45;
    auto result = trimmed_icp(tc.source, tc.target, settings);
    // Should succeed or flag — never silently wrong.
    if (result.success) {
        auto T_err = result.transform * tc.T_true.inverse();
        double err = (T_err.matrix() - Mat4::Identity()).norm();
        // Either recovers well or flags via warnings.
        if (err > 0.5) {
            EXPECT_FALSE(result.warnings.empty())
                << "Wrong answer should have warnings";
        }
    }
}

TEST(RobustnessAdversarial, SixtyPercentOutliers_GNC) {
    SeededRng rng(400);
    auto tc = make_test_data(rng, 40, 60);  // 60% outliers

    GNCSettings settings;
    settings.kernel_param = 0.5;
    auto result = gnc_align(tc.source, tc.target, settings);

    if (result.success) {
        // GNC should either recover or flag low inlier count.
        EXPECT_GT(result.outlier_percent, 30.0);
    }
}

TEST(RobustnessAdversarial, TooFewPoints) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> s(3, 2), t(3, 2);
    s.setRandom(); t.setRandom();
    auto result = trimmed_icp(s, t);
    EXPECT_FALSE(result.success);
}
