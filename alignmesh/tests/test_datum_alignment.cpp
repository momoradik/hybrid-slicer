#include <gtest/gtest.h>
#include "alignmesh/features/datum_alignment.h"
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>
#include <random>

using namespace alignmesh::features;
using namespace alignmesh::geometry;
using namespace alignmesh::alignment;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

// ============================================================================
// 3-2-1 DOF locking
// ============================================================================

TEST(DatumAlignment, PrimaryPlaneLocks3DOF) {
    // A primary plane locks 3 DOFs: 1 translation (normal) + 2 rotations.
    DatumFeatureSpec primary;
    primary.label = "A";
    primary.type = DatumFeatureSpec::Type::PLANE;
    primary.points.resize(3, 100);
    SeededRng rng(42);
    std::uniform_real_distribution<double> xy(-50, 50);
    for (int i = 0; i < 100; ++i)
        primary.points.col(i) = Vec3(xy(rng), xy(rng), 0);

    auto result = establish_datum_frame({primary});
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.datums.size(), 1u);
    EXPECT_EQ(result.datums[0].cumulative_dofs.total_locked(), 3);
}

TEST(DatumAlignment, FullThreeTwoOne) {
    // Primary plane + secondary line/cylinder + tertiary point → 6 DOFs.
    DatumFeatureSpec primary;
    primary.label = "A";
    primary.type = DatumFeatureSpec::Type::PLANE;
    primary.points.resize(3, 100);
    SeededRng rng(55);
    std::uniform_real_distribution<double> xy(-50, 50);
    for (int i = 0; i < 100; ++i)
        primary.points.col(i) = Vec3(xy(rng), xy(rng), 0);

    DatumFeatureSpec secondary;
    secondary.label = "B";
    secondary.type = DatumFeatureSpec::Type::CYLINDER;
    secondary.is_internal = true;
    secondary.points.resize(3, 200);
    for (int i = 0; i < 200; ++i) {
        double theta = 2.0 * kPi * (i % 20) / 20;
        double z = (i / 20) * 2.0;
        secondary.points.col(i) = Vec3(5.0 * std::cos(theta) + 30,
                                        5.0 * std::sin(theta), z);
    }

    DatumFeatureSpec tertiary;
    tertiary.label = "C";
    tertiary.type = DatumFeatureSpec::Type::POINT;
    tertiary.points.resize(3, 1);
    tertiary.points.col(0) = Vec3(0, 40, 0);

    auto result = establish_datum_frame({primary, secondary, tertiary});
    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.final_dofs.total_locked(), 6);
    EXPECT_EQ(result.datums.size(), 3u);
}

// ============================================================================
// CORE TEST: datum exposes defect that best-fit hides
// ============================================================================

TEST(DatumAlignment, DatumExposesHiddenDefect) {
    // A part with datum A (bottom plane) and a tilted top surface.
    // The top surface has a localized 0.5mm bump.
    //
    // Best-fit smears the bump across the whole surface → low max deviation.
    // Datum-constrained locks to the bottom plane → the bump stands out.

    // Datum A: flat bottom plane at z=0.
    DatumFeatureSpec datum_a;
    datum_a.label = "A";
    datum_a.type = DatumFeatureSpec::Type::PLANE;
    datum_a.points.resize(3, 50);
    SeededRng rng(42);
    std::uniform_real_distribution<double> xy(-40, 40);
    for (int i = 0; i < 50; ++i)
        datum_a.points.col(i) = Vec3(xy(rng), xy(rng), 0);

    // Inspection points: top surface at z=10, with a 0.5mm bump at one spot.
    int n_inspect = 100;
    Eigen::Matrix<double, 3, Eigen::Dynamic> inspect(3, n_inspect);
    Eigen::Matrix<double, 3, Eigen::Dynamic> nominal(3, n_inspect);
    for (int i = 0; i < n_inspect; ++i) {
        double x = xy(rng), y = xy(rng);
        double z_nom = 10.0;
        double z_actual = z_nom;
        if (i < 5) z_actual += 0.5;  // bump on 5 points
        inspect.col(i) = Vec3(x, y, z_actual);
        nominal.col(i) = Vec3(x, y, z_nom);
    }

    // Datum-constrained alignment.
    auto datum_result = evaluate_datum_deviation({datum_a}, inspect, nominal);
    ASSERT_TRUE(datum_result.frame.success);
    EXPECT_EQ(datum_result.mode, AlignmentMode::DATUM_CONSTRAINED);

    // The datum-constrained max deviation should reveal the bump (~0.5mm).
    EXPECT_GT(datum_result.max_deviation, 0.3)
        << "Datum alignment should expose the 0.5mm bump";

    // Best-fit comparison: align all points with Kabsch (smears the bump).
    auto best_fit = align_landmarks(inspect, nominal);
    double bf_max = 0;
    if (best_fit.success) {
        for (int i = 0; i < n_inspect; ++i) {
            double d = (best_fit.transform.apply(inspect.col(i)) - nominal.col(i)).norm();
            bf_max = std::max(bf_max, d);
        }
    }

    // Best-fit should show SMALLER max deviation (it hides the bump).
    if (best_fit.success) {
        EXPECT_LT(bf_max, datum_result.max_deviation)
            << "Best-fit hides the bump; datum exposes it. "
            << "BF max=" << bf_max << " vs datum max=" << datum_result.max_deviation;
    }
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(DatumAlignment, NoDatums) {
    auto result = establish_datum_frame({});
    EXPECT_FALSE(result.success);
}

TEST(DatumAlignment, InsufficientPoints) {
    DatumFeatureSpec d;
    d.label = "A";
    d.type = DatumFeatureSpec::Type::PLANE;
    d.points.resize(3, 2);
    d.points.col(0) = Vec3(0, 0, 0);
    d.points.col(1) = Vec3(1, 0, 0);

    auto result = establish_datum_frame({d});
    EXPECT_FALSE(result.success);
}
