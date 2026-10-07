#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/registration/fine_registration.h"

#include <string>
#include <vector>

namespace alignmesh::assist {

// ============================================================================
// Heuristic assist layer (NON-AUTHORITATIVE)
// ============================================================================
//
// Deterministic, rule-based + geometry-based assistance.
// STRUCTURALLY non-authoritative:
//   - All outputs are typed as Suggestion, not official values.
//   - Suggestions CANNOT be consumed where official results are expected.
//   - Every suggestion must pass through deterministic validation before use.
//   - The deterministic solver always computes the final transform.
//   - Reports label these as "heuristics," not "AI."
//
// Suggestions are logged with outcomes for future learning.
// Learned models (if added) stay non-deterministic-isolated.
// ============================================================================

/// A suggestion — structurally NOT an official value.
/// Cannot be implicitly converted to any official type.
template <typename T>
struct Suggestion {
    T value;
    double confidence = 0;     // 0-1 heuristic confidence (NOT uncertainty)
    std::string rationale;
    std::string category;      // "landmark", "parameter", "warning"
    bool validated = false;    // true only after deterministic validation

    /// Explicitly extract the value after validation.
    /// This is the ONLY way to use the suggestion downstream.
    const T& validated_value() const {
        if (!validated) {
            throw std::logic_error(
                "FIREWALL: suggestion used without validation. "
                "Suggestions must pass deterministic validation first.");
        }
        return value;
    }
};

/// Landmark suggestion: a point with high geometric distinctiveness.
struct LandmarkCandidate {
    Eigen::Index point_index = -1;
    geometry::Vec3 position = geometry::Vec3::Zero();
    double curvature = 0;
    std::string feature_type;  // "edge", "corner", "hole_edge", "boss", "flat"
};

/// Parameter recommendation for registration.
struct RegistrationParams {
    registration::ICPMethod method = registration::ICPMethod::POINT_TO_PLANE;
    double max_correspondence_distance = 1.0;
    int max_iterations = 100;
    double voxel_size = 0;
    std::vector<double> voxel_schedule;
};

/// Failure-risk warning.
struct RiskWarning {
    enum class Level { INFO, CAUTION, HIGH_RISK };
    Level level = Level::INFO;
    std::string risk;
    std::string mitigation;
};

/// A log entry for a suggestion + outcome.
struct SuggestionLog {
    std::string timestamp;
    std::string category;
    std::string suggestion_summary;
    bool was_accepted = false;
    bool was_validated = false;
    std::string outcome;
};

/// Full assist result.
struct AssistResult {
    /// Suggested landmarks (high-curvature, edges, features).
    std::vector<Suggestion<LandmarkCandidate>> landmarks;

    /// Overlap region estimate (indices of points likely to overlap).
    Suggestion<std::vector<Eigen::Index>> overlap_region;

    /// Recommended registration parameters.
    Suggestion<RegistrationParams> recommended_params;

    /// Risk warnings.
    std::vector<RiskWarning> risk_warnings;

    /// Log of all suggestions.
    std::vector<SuggestionLog> log;
};

/// Analyze a point cloud and generate heuristic suggestions.
/// All suggestions are deterministic (same input → same output).
AssistResult analyze_and_suggest(
    const geometry::PointCloud& source,
    const geometry::PointCloud& target,
    double tolerance_mm = 0.1);

/// Validate a parameter suggestion against deterministic rules.
/// Returns true if the parameters are safe to use.
bool validate_params(
    const Suggestion<RegistrationParams>& params,
    const geometry::PointCloud& source,
    const geometry::PointCloud& target,
    std::vector<std::string>& validation_errors);

} // namespace alignmesh::assist
