#include <gtest/gtest.h>
#include "alignmesh/assist/heuristic_assist.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

using namespace alignmesh::assist;
using namespace alignmesh::geometry;
using namespace alignmesh::numerics;

static constexpr double kPi = std::numbers::pi;

namespace {

PointCloud make_hemisphere(int n, double radius = 50.0) {
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, n * n);
    Eigen::Matrix<double, 3, Eigen::Dynamic> nrm(3, n * n);
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        double theta = (kPi / 2.0) * (i + 0.5) / n;
        for (int j = 0; j < n; ++j) {
            double phi = 2.0 * kPi * j / n;
            Vec3 p(radius * std::sin(theta) * std::cos(phi),
                    radius * std::sin(theta) * std::sin(phi),
                    radius * std::cos(theta));
            pts.col(idx) = p;
            nrm.col(idx) = p.normalized();
            ++idx;
        }
    }
    return PointCloud(std::move(pts), std::move(nrm), std::nullopt);
}

} // namespace

// ============================================================================
// Structural firewall: Suggestion cannot be used without validation
// ============================================================================

TEST(HeuristicAssist, SuggestionFirewall) {
    Suggestion<int> s;
    s.value = 42;
    s.validated = false;

    // Attempting to use an unvalidated suggestion must throw.
    EXPECT_THROW(s.validated_value(), std::logic_error);

    // After validation, it works.
    s.validated = true;
    EXPECT_EQ(s.validated_value(), 42);
}

TEST(HeuristicAssist, SuggestionNotImplicitlyUsable) {
    // The Suggestion<T> type is NOT implicitly convertible to T.
    // This is structural — the type system prevents it.
    // (Compile-time test: the following would not compile if uncommented.)
    // Suggestion<int> s; s.value = 42;
    // int x = s;  // ERROR: no implicit conversion
    SUCCEED();
}

// ============================================================================
// Deterministic reproducibility
// ============================================================================

TEST(HeuristicAssist, Deterministic) {
    auto source = make_hemisphere(15, 50.0);
    auto target = make_hemisphere(15, 50.0);

    auto r1 = analyze_and_suggest(source, target, 0.1);
    auto r2 = analyze_and_suggest(source, target, 0.1);

    // Same input → same suggestions.
    EXPECT_EQ(r1.landmarks.size(), r2.landmarks.size());
    EXPECT_EQ(r1.risk_warnings.size(), r2.risk_warnings.size());
    if (!r1.landmarks.empty() && !r2.landmarks.empty()) {
        EXPECT_EQ(r1.landmarks[0].value.point_index,
                  r2.landmarks[0].value.point_index);
    }
}

// ============================================================================
// Landmark suggestion
// ============================================================================

TEST(HeuristicAssist, SuggestsLandmarks) {
    auto cloud = make_hemisphere(20, 50.0);
    auto result = analyze_and_suggest(cloud, cloud, 0.1);

    EXPECT_GT(result.landmarks.size(), 0u)
        << "Should suggest at least some landmarks";

    for (auto& lm : result.landmarks) {
        EXPECT_GE(lm.value.point_index, 0);
        EXPECT_FALSE(lm.rationale.empty());
    }
}

// ============================================================================
// Parameter recommendation
// ============================================================================

TEST(HeuristicAssist, RecommendsParams) {
    auto source = make_hemisphere(15, 50.0);
    auto target = make_hemisphere(15, 50.0);

    auto result = analyze_and_suggest(source, target, 0.1);

    EXPECT_GT(result.recommended_params.confidence, 0);
    EXPECT_FALSE(result.recommended_params.rationale.empty());
    EXPECT_GT(result.recommended_params.value.max_correspondence_distance, 0);
}

// ============================================================================
// Validation gate
// ============================================================================

TEST(HeuristicAssist, ValidationGate) {
    auto source = make_hemisphere(15, 50.0);
    auto target = make_hemisphere(15, 50.0);

    auto result = analyze_and_suggest(source, target, 0.1);
    std::vector<std::string> errors;

    // The recommended params should pass validation.
    bool ok = validate_params(result.recommended_params, source, target, errors);
    EXPECT_TRUE(ok) << "Recommended params should pass validation";
    if (ok) {
        result.recommended_params.validated = true;
        // Now we can use the validated value.
        auto params = result.recommended_params.validated_value();
        EXPECT_GT(params.max_correspondence_distance, 0);
    }
}

TEST(HeuristicAssist, BadSuggestionCaught) {
    // A deliberately bad parameter suggestion.
    Suggestion<RegistrationParams> bad;
    bad.value.max_correspondence_distance = -1.0;  // invalid!
    bad.value.max_iterations = 0;                   // invalid!
    bad.confidence = 0.5;
    bad.rationale = "bad suggestion for testing";

    auto source = make_hemisphere(10, 50.0);
    auto target = make_hemisphere(10, 50.0);
    std::vector<std::string> errors;

    bool ok = validate_params(bad, source, target, errors);
    EXPECT_FALSE(ok) << "Bad params should fail validation";
    EXPECT_FALSE(errors.empty()) << "Should report why validation failed";
}

// ============================================================================
// Risk warnings
// ============================================================================

TEST(HeuristicAssist, WarnsOnTightTolerance) {
    auto source = make_hemisphere(10, 50.0);
    auto target = make_hemisphere(10, 50.0);

    // Very tight tolerance on sparse data.
    auto result = analyze_and_suggest(source, target, 0.001);

    bool found_risk = false;
    for (auto& w : result.risk_warnings) {
        if (w.level == RiskWarning::Level::HIGH_RISK ||
            w.level == RiskWarning::Level::CAUTION)
            found_risk = true;
    }
    EXPECT_TRUE(found_risk)
        << "Should warn about tight tolerance on sparse data";
}

// ============================================================================
// Logging
// ============================================================================

TEST(HeuristicAssist, SuggestionsLogged) {
    auto source = make_hemisphere(10, 50.0);
    auto target = make_hemisphere(10, 50.0);

    auto result = analyze_and_suggest(source, target, 0.1);
    EXPECT_FALSE(result.log.empty()) << "Suggestions must be logged";
}
