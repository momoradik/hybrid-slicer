#include <gtest/gtest.h>
#include "alignmesh/numerics/seeded_rng.h"

#include <random>

using namespace alignmesh::numerics;

TEST(SeededRng, SameSeedSameSequence) {
    SeededRng a(42);
    SeededRng b(42);

    for (int i = 0; i < 1000; ++i) {
        ASSERT_EQ(a(), b()) << "diverged at step " << i;
    }
}

TEST(SeededRng, DifferentSeedDifferentSequence) {
    SeededRng a(42);
    SeededRng b(99);

    // Extremely unlikely (but not impossible) for two MT sequences to
    // agree for 100 consecutive outputs. Check that at least one differs.
    bool any_differ = false;
    for (int i = 0; i < 100; ++i) {
        if (a() != b()) {
            any_differ = true;
            break;
        }
    }
    EXPECT_TRUE(any_differ);
}

TEST(SeededRng, ReseedResets) {
    SeededRng rng(42);
    auto first_val = rng();

    rng.seed(42);
    EXPECT_EQ(rng(), first_val);
}

TEST(SeededRng, DiscardAdvancesState) {
    SeededRng a(42);
    SeededRng b(42);

    a.discard(100);
    for (int i = 0; i < 100; ++i) b();

    // After discarding 100 values, both should be in the same state.
    EXPECT_EQ(a(), b());
}

TEST(SeededRng, MinMax) {
    EXPECT_EQ(SeededRng::min(), std::mt19937_64::min());
    EXPECT_EQ(SeededRng::max(), std::mt19937_64::max());
}

TEST(SeededRng, WorksWithDistributions) {
    SeededRng rng(7);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    double val = dist(rng);
    EXPECT_GE(val, 0.0);
    EXPECT_LE(val, 1.0);
}

TEST(SeededRng, DistributionDeterminism) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    SeededRng a(123);
    SeededRng b(123);

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(dist(a), dist(b)) << "distribution diverged at step " << i;
    }
}
