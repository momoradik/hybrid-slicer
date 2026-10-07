#include <gtest/gtest.h>
#include "alignmesh/validation/validation_harness.h"

#include <cmath>

using namespace alignmesh::validation;

// ============================================================================
// MSA / Gauge R&R
// ============================================================================

TEST(GaugeRR, KnownDataSet) {
    // Hand-computed Gauge R&R on a small dataset:
    // 3 parts × 2 operators × 3 trials.
    std::vector<MSAMeasurement> data;

    // Part 1, Operator 1: 10.1, 10.2, 10.1
    data.push_back({1, 1, 1, 10.1});
    data.push_back({1, 1, 2, 10.2});
    data.push_back({1, 1, 3, 10.1});
    // Part 1, Operator 2: 10.0, 10.1, 10.2
    data.push_back({1, 2, 1, 10.0});
    data.push_back({1, 2, 2, 10.1});
    data.push_back({1, 2, 3, 10.2});
    // Part 2, Operator 1: 10.5, 10.4, 10.5
    data.push_back({2, 1, 1, 10.5});
    data.push_back({2, 1, 2, 10.4});
    data.push_back({2, 1, 3, 10.5});
    // Part 2, Operator 2: 10.4, 10.5, 10.4
    data.push_back({2, 2, 1, 10.4});
    data.push_back({2, 2, 2, 10.5});
    data.push_back({2, 2, 3, 10.4});
    // Part 3, Operator 1: 11.0, 11.1, 11.0
    data.push_back({3, 1, 1, 11.0});
    data.push_back({3, 1, 2, 11.1});
    data.push_back({3, 1, 3, 11.0});
    // Part 3, Operator 2: 10.9, 11.0, 11.0
    data.push_back({3, 2, 1, 10.9});
    data.push_back({3, 2, 2, 11.0});
    data.push_back({3, 2, 3, 11.0});

    auto result = compute_gauge_rr(data, 1.0);

    ASSERT_TRUE(result.computed);
    EXPECT_EQ(result.n_parts, 3);
    EXPECT_EQ(result.n_operators, 2);
    EXPECT_EQ(result.n_trials, 3);

    // EV (repeatability) should be small (measurements are consistent).
    EXPECT_GT(result.ev, 0);
    EXPECT_LT(result.ev, 0.2);

    // GRR should be small relative to part variation.
    EXPECT_GT(result.grr, 0);
    EXPECT_GT(result.pv, result.grr)
        << "Part variation should dominate gauge variation";

    // %GRR classification.
    EXPECT_FALSE(result.classification.empty());

    // P/T ratio.
    EXPECT_GT(result.p_to_t, 0);
}

TEST(GaugeRR, PerfectRepeatability) {
    // All trials identical → EV = 0.
    std::vector<MSAMeasurement> data;
    for (int p = 1; p <= 3; ++p)
        for (int o = 1; o <= 2; ++o)
            for (int t = 1; t <= 3; ++t)
                data.push_back({p, o, t, 10.0 + p * 0.5});

    auto result = compute_gauge_rr(data, 1.0);
    ASSERT_TRUE(result.computed);
    EXPECT_NEAR(result.ev, 0, 1e-10);
}

TEST(GaugeRR, Classification) {
    // Construct a case with known %GRR.
    // Small EV+AV, large PV → low %GRR → acceptable.
    std::vector<MSAMeasurement> data;
    for (int p = 1; p <= 5; ++p)
        for (int o = 1; o <= 2; ++o)
            for (int t = 1; t <= 3; ++t)
                data.push_back({p, o, t, p * 1.0 + o * 0.001 + t * 0.001});

    auto result = compute_gauge_rr(data, 10.0);
    ASSERT_TRUE(result.computed);
    // Parts vary by ~1.0 each, gauge varies by ~0.002 → %GRR very small.
    EXPECT_EQ(result.classification, "acceptable");
}

// ============================================================================
// Determinism manifest
// ============================================================================

TEST(DeterminismManifest, GeneratesValues) {
    auto manifest = generate_determinism_manifest();
    EXPECT_FALSE(manifest.machine_fingerprint.empty());
    EXPECT_GT(manifest.values.size(), 0u);
}

TEST(DeterminismManifest, Reproducible) {
    auto m1 = generate_determinism_manifest();
    auto m2 = generate_determinism_manifest();
    EXPECT_EQ(m1.values.size(), m2.values.size());
    for (std::size_t i = 0; i < m1.values.size(); ++i) {
        EXPECT_EQ(m1.values[i].first, m2.values[i].first);
        EXPECT_EQ(m1.values[i].second, m2.values[i].second)
            << "Manifest value '" << m1.values[i].first << "' not reproducible";
    }
}

TEST(DeterminismManifest, SelfComparison) {
    auto m = generate_determinism_manifest();
    auto result = compare_manifests(m, m);
    EXPECT_TRUE(result.all_match);
    EXPECT_TRUE(result.divergences.empty());
}

TEST(DeterminismManifest, DetectsDivergence) {
    auto m1 = generate_determinism_manifest();
    auto m2 = m1;
    if (!m2.values.empty()) {
        m2.values[0].second += 1.0;  // inject divergence
    }
    auto result = compare_manifests(m1, m2);
    EXPECT_FALSE(result.all_match);
    EXPECT_FALSE(result.divergences.empty());
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(GaugeRR, EmptyData) {
    auto result = compute_gauge_rr({}, 1.0);
    EXPECT_FALSE(result.computed);
}

TEST(GaugeRR, SingleMeasurement) {
    std::vector<MSAMeasurement> data = {{1, 1, 1, 10.0}};
    auto result = compute_gauge_rr(data, 1.0);
    EXPECT_FALSE(result.computed);
}
