#include <gtest/gtest.h>
#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <numbers>
#include <random>

using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

RigidTransform random_transform(SeededRng& rng) {
    std::uniform_real_distribution<double> angle(-kPi, kPi);
    std::uniform_real_distribution<double> trans(-10.0, 10.0);
    std::normal_distribution<double> gauss(0.0, 1.0);

    Vec3 axis(gauss(rng), gauss(rng), gauss(rng));
    double n = axis.norm();
    if (n < 1e-12) axis = Vec3::UnitX();
    else axis /= n;

    Mat3 R = Eigen::AngleAxisd(angle(rng), axis).toRotationMatrix();
    Vec3 t(trans(rng), trans(rng), trans(rng));
    return RigidTransform::from_rotation_translation(R, t);
}

} // namespace

// ============================================================================
// Ground-truth tests
// ============================================================================

TEST(RigidTransform, DefaultIsIdentity) {
    RigidTransform T;
    EXPECT_TRUE(T.is_identity());
    EXPECT_TRUE(T.matrix().isApprox(Mat4::Identity()));
}

TEST(RigidTransform, KnownRotationTranslation) {
    // 90 deg CCW around Z + translation (1,2,3).
    // R*(1,0,0) = (0,1,0), so result = (0+1, 1+2, 0+3) = (1, 3, 3).
    Mat3 R;
    R << 0, -1, 0,
         1,  0, 0,
         0,  0, 1;
    Vec3 t(1.0, 2.0, 3.0);
    auto T = RigidTransform::from_rotation_translation(R, t);

    Vec3 p = T.apply(Vec3(1.0, 0.0, 0.0));
    EXPECT_NEAR(p(0), 1.0, 1e-15);
    EXPECT_NEAR(p(1), 3.0, 1e-15);
    EXPECT_NEAR(p(2), 3.0, 1e-15);
}

TEST(RigidTransform, ConventionXToY) {
    // THE convention test: 90 deg CCW around Z maps +X to +Y.
    //
    // Column-vector convention (p' = R*p):
    //   Column 0 of R = R*(1,0,0) = (0,1,0)
    //   Column 1 of R = R*(0,1,0) = (-1,0,0)
    //   Column 2 of R = R*(0,0,1) = (0,0,1)
    //
    // R = | 0  -1   0 |
    //     | 1   0   0 |
    //     | 0   0   1 |
    //
    // 4x4 matrix (no translation):
    //   M(0,0)= 0  M(0,1)=-1  M(0,2)= 0  M(0,3)= 0
    //   M(1,0)= 1  M(1,1)= 0  M(1,2)= 0  M(1,3)= 0
    //   M(2,0)= 0  M(2,1)= 0  M(2,2)= 1  M(2,3)= 0
    //   M(3,0)= 0  M(3,1)= 0  M(3,2)= 0  M(3,3)= 1

    Mat3 R = Eigen::AngleAxisd(kPi / 2.0, Vec3::UnitZ()).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3::Zero());
    const Mat4& M = T.matrix();

    EXPECT_NEAR(M(0, 0),  0.0, 1e-15);
    EXPECT_NEAR(M(0, 1), -1.0, 1e-15);
    EXPECT_NEAR(M(0, 2),  0.0, 1e-15);
    EXPECT_NEAR(M(1, 0),  1.0, 1e-15);
    EXPECT_NEAR(M(1, 1),  0.0, 1e-15);
    EXPECT_NEAR(M(1, 2),  0.0, 1e-15);
    EXPECT_NEAR(M(2, 0),  0.0, 1e-15);
    EXPECT_NEAR(M(2, 1),  0.0, 1e-15);
    EXPECT_NEAR(M(2, 2),  1.0, 1e-15);

    EXPECT_EQ(M(0, 3), 0.0);
    EXPECT_EQ(M(1, 3), 0.0);
    EXPECT_EQ(M(2, 3), 0.0);
    EXPECT_EQ(M(3, 0), 0.0);
    EXPECT_EQ(M(3, 1), 0.0);
    EXPECT_EQ(M(3, 2), 0.0);
    EXPECT_EQ(M(3, 3), 1.0);

    // Functional: +X -> +Y
    Vec3 r1 = T.apply(Vec3::UnitX());
    EXPECT_NEAR(r1(0), 0.0, 1e-15);
    EXPECT_NEAR(r1(1), 1.0, 1e-15);
    EXPECT_NEAR(r1(2), 0.0, 1e-15);

    // +Y -> -X
    Vec3 r2 = T.apply(Vec3::UnitY());
    EXPECT_NEAR(r2(0), -1.0, 1e-15);
    EXPECT_NEAR(r2(1),  0.0, 1e-15);
    EXPECT_NEAR(r2(2),  0.0, 1e-15);
}

TEST(RigidTransform, InverseIsIdentity) {
    SeededRng rng(42);
    for (int i = 0; i < 1000; ++i) {
        auto T = random_transform(rng);
        auto product = T * T.inverse();
        ASSERT_TRUE(product.is_identity(1e-10))
            << "Failed at i=" << i
            << "\n||I - T*T^-1|| = " << (product.matrix() - Mat4::Identity()).norm();
    }
}

TEST(RigidTransform, Associativity) {
    SeededRng rng(123);
    for (int i = 0; i < 200; ++i) {
        auto A = random_transform(rng);
        auto B = random_transform(rng);
        auto C = random_transform(rng);
        auto left  = (A * B) * C;
        auto right = A * (B * C);
        ASSERT_TRUE(left.is_approx(right, 1e-10))
            << "Associativity failed at i=" << i
            << "\ndiff = " << (left.matrix() - right.matrix()).norm();
    }
}

TEST(RigidTransform, QuaternionRoundTrip) {
    SeededRng rng(99);
    for (int i = 0; i < 200; ++i) {
        auto T = random_transform(rng);
        Mat3 R1 = T.rotation();
        Mat3 R2 = T.quaternion().toRotationMatrix();
        ASSERT_TRUE(R1.isApprox(R2, 1e-12))
            << "Quat round-trip failed at i=" << i;
    }
}

TEST(RigidTransform, FromQuaternionTranslation) {
    Quat q(Eigen::AngleAxisd(kPi / 4.0, Vec3::UnitZ()));
    Vec3 t(1.0, 2.0, 3.0);
    auto T = RigidTransform::from_quaternion_translation(q, t);
    EXPECT_TRUE(T.rotation().isApprox(q.toRotationMatrix(), 1e-14));
    EXPECT_TRUE(T.translation().isApprox(t, 1e-15));
}

TEST(RigidTransform, ExpLogRoundTrip) {
    SeededRng rng(77);
    for (int i = 0; i < 500; ++i) {
        auto T = random_transform(rng);
        auto twist = T.log();
        auto T2 = RigidTransform::exp(twist);
        ASSERT_TRUE(T.is_approx(T2, 1e-9))
            << "exp(log(T)) failed at i=" << i
            << "\ndiff = " << (T.matrix() - T2.matrix()).norm()
            << "\ntwist = " << twist.transpose();
    }
}

TEST(RigidTransform, LogIdentityIsZero) {
    Vec6 twist = RigidTransform().log();
    EXPECT_NEAR(twist.norm(), 0.0, 1e-15);
}

TEST(RigidTransform, ExpZeroIsIdentity) {
    auto T = RigidTransform::exp(Vec6::Zero());
    EXPECT_TRUE(T.is_identity(1e-15));
}

TEST(RigidTransform, ApplyToCloud) {
    Mat3 R;
    R << 0, -1, 0,
         1,  0, 0,
         0,  0, 1;
    Vec3 t(1.0, 0.0, 0.0);
    auto T = RigidTransform::from_rotation_translation(R, t);

    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, 3);
    pts.col(0) = Vec3(1, 0, 0);
    pts.col(1) = Vec3(0, 1, 0);
    pts.col(2) = Vec3(0, 0, 1);

    auto result = T.apply_cloud(pts);

    // (1,0,0) -> (0,1,0) + (1,0,0) = (1,1,0)
    EXPECT_NEAR(result(0, 0), 1.0, 1e-15);
    EXPECT_NEAR(result(1, 0), 1.0, 1e-15);
    EXPECT_NEAR(result(2, 0), 0.0, 1e-15);

    // (0,1,0) -> (-1,0,0) + (1,0,0) = (0,0,0)
    EXPECT_NEAR(result(0, 1), 0.0, 1e-15);
    EXPECT_NEAR(result(1, 1), 0.0, 1e-15);
    EXPECT_NEAR(result(2, 1), 0.0, 1e-15);

    // (0,0,1) -> (0,0,1) + (1,0,0) = (1,0,1)
    EXPECT_NEAR(result(0, 2), 1.0, 1e-15);
    EXPECT_NEAR(result(1, 2), 0.0, 1e-15);
    EXPECT_NEAR(result(2, 2), 1.0, 1e-15);
}

// ============================================================================
// Adversarial tests
// ============================================================================

TEST(RigidTransformAdversarial, NearIdentityRotation) {
    double tiny = 1e-15;
    Mat3 R = Eigen::AngleAxisd(tiny, Vec3::UnitZ()).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3(1, 2, 3));

    EXPECT_TRUE((T * T.inverse()).is_identity(1e-10));

    auto twist = T.log();
    auto T2 = RigidTransform::exp(twist);
    EXPECT_TRUE(T.is_approx(T2, 1e-10));
}

TEST(RigidTransformAdversarial, HalfTurnX) {
    Mat3 R = Eigen::AngleAxisd(kPi, Vec3::UnitX()).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3(5, 6, 7));

    EXPECT_TRUE((T * T.inverse()).is_identity(1e-10));

    // Quaternion round-trip
    Mat3 R2 = T.quaternion().toRotationMatrix();
    EXPECT_TRUE(R.isApprox(R2, 1e-10));

    // exp/log round-trip at theta = pi
    auto twist = T.log();
    auto T2 = RigidTransform::exp(twist);
    EXPECT_TRUE(T.is_approx(T2, 1e-8))
        << "Half-turn X exp/log diff = " << (T.matrix() - T2.matrix()).norm();
}

TEST(RigidTransformAdversarial, HalfTurnArbitrary) {
    Vec3 axis = Vec3(1, 1, 1).normalized();
    Mat3 R = Eigen::AngleAxisd(kPi, axis).toRotationMatrix();
    auto T = RigidTransform::from_rotation_translation(R, Vec3(-3, 4, -5));

    EXPECT_TRUE((T * T.inverse()).is_identity(1e-10));

    auto twist = T.log();
    auto T2 = RigidTransform::exp(twist);
    // Near-pi rotations have inherently reduced precision due to axis
    // extraction from a near-symmetric matrix. 1e-7 is achievable.
    EXPECT_TRUE(T.is_approx(T2, 1e-7))
        << "Half-turn arbitrary exp/log diff = " << (T.matrix() - T2.matrix()).norm();
}

TEST(RigidTransformAdversarial, HalfTurnAllAxes) {
    // 180 deg around each coordinate axis and a few diagonals.
    Vec3 axes[] = {
        Vec3::UnitX(), Vec3::UnitY(), Vec3::UnitZ(),
        Vec3(1, 1, 0).normalized(),
        Vec3(1, 0, 1).normalized(),
        Vec3(0, 1, 1).normalized(),
    };
    for (const auto& axis : axes) {
        Mat3 R = Eigen::AngleAxisd(kPi, axis).toRotationMatrix();
        auto T = RigidTransform::from_rotation_translation(R, Vec3(1, 2, 3));
        auto twist = T.log();
        auto T2 = RigidTransform::exp(twist);
        EXPECT_TRUE(T.is_approx(T2, 1e-8))
            << "Axis = " << axis.transpose()
            << "\ndiff = " << (T.matrix() - T2.matrix()).norm();
    }
}

TEST(RigidTransformAdversarial, LargeTranslation) {
    // Precision under very large coordinates.
    SeededRng rng(55);
    std::uniform_real_distribution<double> large(-1e8, 1e8);
    for (int i = 0; i < 100; ++i) {
        Vec3 t(large(rng), large(rng), large(rng));
        auto T = RigidTransform::from_rotation_translation(
            Eigen::AngleAxisd(0.5, Vec3::UnitZ()).toRotationMatrix(), t);
        double err = ((T * T.inverse()).matrix() - Mat4::Identity()).norm();
        ASSERT_LT(err, 1e-4)
            << "i=" << i << "  ||t||=" << t.norm() << "  err=" << err;
    }
}

TEST(RigidTransformAdversarial, VerySmallAngle) {
    for (int e = 5; e <= 16; ++e) {
        double angle = std::pow(10.0, -e);
        Vec3 axis = Vec3(0.3, 0.4, 0.5).normalized();
        Mat3 R = Eigen::AngleAxisd(angle, axis).toRotationMatrix();
        auto T = RigidTransform::from_rotation_translation(R, Vec3(1, 2, 3));

        auto twist = T.log();
        auto T2 = RigidTransform::exp(twist);
        double err = (T.matrix() - T2.matrix()).norm();
        ASSERT_LT(err, 1e-8)
            << "angle=1e-" << e << "  err=" << err;
    }
}

TEST(RigidTransformAdversarial, ExpLogNearPi) {
    // Near-pi precision degrades as the angle approaches pi.
    // Tolerance scales: closer to pi => looser bound.
    // The SE(3) exp/log round-trip accumulates error through:
    //   so3_log (axis/angle recovery) -> V^-1 (translation) -> so3_exp -> V
    // A uniform 1e-8 tolerance covers the full range of near-pi offsets.
    struct Case { double offset; double tol; };
    Case cases[] = {
        {1e-3, 1e-8},
        {1e-5, 1e-8},
        {1e-7, 1e-8},
        {1e-9, 1e-8},
    };
    for (auto [offset, tol] : cases) {
        double angle = kPi - offset;
        Vec3 axis = Vec3(0.6, 0.7, 0.1).normalized();
        Mat3 R = Eigen::AngleAxisd(angle, axis).toRotationMatrix();
        auto T = RigidTransform::from_rotation_translation(R, Vec3(2, -1, 4));

        auto twist = T.log();
        auto T2 = RigidTransform::exp(twist);
        ASSERT_TRUE(T.is_approx(T2, tol))
            << "offset=" << offset
            << "\ndiff = " << (T.matrix() - T2.matrix()).norm();
    }
}

TEST(RigidTransformAdversarial, HatVeeRoundTrip) {
    Vec3 omega(0.1, -0.2, 0.3);
    EXPECT_TRUE(vee(hat(omega)).isApprox(omega, 1e-15));
}
