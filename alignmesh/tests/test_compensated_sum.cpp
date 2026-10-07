#include <gtest/gtest.h>
#include "alignmesh/numerics/compensated_sum.h"

#include <numeric>
#include <span>
#include <vector>

using namespace alignmesh::numerics;

// --- neumaier_sum -----------------------------------------------------------

TEST(NeumaierSum, EmptyRange) {
    std::vector<double> v;
    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), 0.0);
}

TEST(NeumaierSum, SingleElement) {
    std::vector<double> v = {3.14};
    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), 3.14);
}

TEST(NeumaierSum, ExactIntegerSum) {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), 15.0);
}

TEST(NeumaierSum, CatastrophicCancellation) {
    // Classic test case: naive accumulate loses the small terms entirely
    // because 1.0 + 1e100 == 1e100 in double precision.
    std::vector<double> v = {1.0, 1e100, 1.0, -1e100};

    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), 2.0);

    // Verify that std::accumulate does NOT get this right.
    double naive = std::accumulate(v.begin(), v.end(), 0.0);
    EXPECT_NE(naive, 2.0);
}

TEST(NeumaierSum, NegativeValues) {
    std::vector<double> v = {-1.0, -2.0, -3.0};
    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), -6.0);
}

TEST(NeumaierSum, SpanOverload) {
    std::vector<double> v = {1.0, 1e100, 1.0, -1e100};
    EXPECT_EQ(neumaier_sum(std::span<const double>(v)), 2.0);
}

TEST(NeumaierSum, FloatType) {
    std::vector<float> v = {1.0f, 2.0f, 3.0f};
    EXPECT_EQ(neumaier_sum(v.begin(), v.end()), 6.0f);
}

// --- pairwise_sum -----------------------------------------------------------

TEST(PairwiseSum, EmptyRange) {
    std::vector<double> v;
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()), 0.0);
}

TEST(PairwiseSum, SingleElement) {
    std::vector<double> v = {42.0};
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()), 42.0);
}

TEST(PairwiseSum, ExactIntegerSum) {
    std::vector<double> v = {1.0, 2.0, 3.0, 4.0, 5.0};
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()), 15.0);
}

TEST(PairwiseSum, CatastrophicCancellation) {
    std::vector<double> v = {1.0, 1e100, 1.0, -1e100};
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()), 2.0);
}

TEST(PairwiseSum, LargeRangeExceedsThreshold) {
    // Exercise the recursive split (> kPairwiseThreshold elements).
    constexpr std::size_t n = 1000;
    std::vector<double> v(n, 1.0);
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()), static_cast<double>(n));
}

TEST(PairwiseSum, MatchesNeumaierBelowThreshold) {
    // Below kPairwiseThreshold, pairwise_sum delegates to neumaier_sum,
    // so results must be bit-identical.
    std::vector<double> v = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
    EXPECT_EQ(pairwise_sum(v.begin(), v.end()),
              neumaier_sum(v.begin(), v.end()));
}

TEST(PairwiseSum, SpanOverload) {
    std::vector<double> v = {1.0, 2.0, 3.0};
    EXPECT_EQ(pairwise_sum(std::span<const double>(v)), 6.0);
}

// --- determinism (same input → same output, always) -------------------------

TEST(Determinism, RepeatedCallsIdentical) {
    // The whole point: running the same reduction twice must give
    // bit-identical results on the same machine.
    std::vector<double> v;
    v.reserve(500);
    for (int i = 1; i <= 500; ++i) {
        v.push_back(1.0 / static_cast<double>(i));
    }

    double n1 = neumaier_sum(v.begin(), v.end());
    double n2 = neumaier_sum(v.begin(), v.end());
    EXPECT_EQ(n1, n2);

    double p1 = pairwise_sum(v.begin(), v.end());
    double p2 = pairwise_sum(v.begin(), v.end());
    EXPECT_EQ(p1, p2);
}
