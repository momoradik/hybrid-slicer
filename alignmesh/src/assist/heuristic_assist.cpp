#define _CRT_SECURE_NO_WARNINGS
#include "alignmesh/assist/heuristic_assist.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/normals.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace alignmesh::assist {

using namespace geometry;

static std::string now_ts() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// ============================================================================
// Landmark suggestion (curvature-based)
// ============================================================================

static std::vector<Suggestion<LandmarkCandidate>> suggest_landmarks(
        const PointCloud& cloud, const spatial::NormalEstimation& norms) {
    std::vector<Suggestion<LandmarkCandidate>> landmarks;
    auto n = cloud.size();
    if (n < 10) return landmarks;

    // Rank points by curvature and select top candidates.
    struct Candidate { Eigen::Index idx; double curv; };
    std::vector<Candidate> ranked;
    for (Eigen::Index i = 0; i < n; ++i) {
        ranked.push_back({i, norms.curvatures(i)});
    }
    std::sort(ranked.begin(), ranked.end(),
        [](const Candidate& a, const Candidate& b) { return a.curv > b.curv; });

    // Take top 5% as landmarks (min 3, max 50).
    int count = std::clamp(static_cast<int>(n * 0.05), 3, 50);
    count = std::min(count, static_cast<int>(ranked.size()));

    for (int i = 0; i < count; ++i) {
        LandmarkCandidate lm;
        lm.point_index = ranked[static_cast<std::size_t>(i)].idx;
        lm.position = cloud.points().col(lm.point_index);
        lm.curvature = ranked[static_cast<std::size_t>(i)].curv;

        if (lm.curvature > 0.1) lm.feature_type = "edge";
        else if (lm.curvature > 0.05) lm.feature_type = "corner";
        else lm.feature_type = "curved_region";

        Suggestion<LandmarkCandidate> s;
        s.value = lm;
        s.confidence = std::min(lm.curvature * 10.0, 1.0);
        s.rationale = "High curvature (" + std::to_string(lm.curvature) +
                      ") at " + lm.feature_type;
        s.category = "landmark";
        landmarks.push_back(std::move(s));
    }

    return landmarks;
}

// ============================================================================
// Parameter recommendation
// ============================================================================

static Suggestion<RegistrationParams> recommend_params(
        const PointCloud& source, const PointCloud& target,
        double tolerance_mm) {
    Suggestion<RegistrationParams> s;
    s.category = "parameter";
    auto& p = s.value;

    auto ns = source.size();
    auto nt = target.size();

    // Estimate point spacing from bounding box.
    auto bbox_extent = [](const PointCloud& pc) -> double {
        if (pc.size() < 2) return 1.0;
        Vec3 mn = pc.points().rowwise().minCoeff();
        Vec3 mx = pc.points().rowwise().maxCoeff();
        return (mx - mn).norm();
    };

    double extent = std::max(bbox_extent(source), bbox_extent(target));
    double density = std::max(static_cast<double>(ns + nt) / 2.0, 1.0);
    double spacing = extent / std::pow(density, 1.0 / 3.0);

    // Method: prefer point-to-plane for curved surfaces, GICP for noisy.
    p.method = registration::ICPMethod::POINT_TO_PLANE;

    // Max correspondence distance: 5× spacing or 10× tolerance, whichever larger.
    p.max_correspondence_distance = std::max(5.0 * spacing, 10.0 * tolerance_mm);

    // Iterations: more for tight tolerances.
    p.max_iterations = (tolerance_mm < 0.01) ? 200 : 100;

    // Multi-resolution for large clouds.
    if (ns > 5000 || nt > 5000) {
        double coarse = spacing * 4;
        double fine = spacing * 2;
        p.voxel_schedule = {coarse, fine};
    }

    s.confidence = 0.7;
    s.rationale = "Based on extent=" + std::to_string(extent) +
                  "mm, spacing~" + std::to_string(spacing) +
                  "mm, tolerance=" + std::to_string(tolerance_mm) + "mm";
    return s;
}

// ============================================================================
// Risk warnings
// ============================================================================

static std::vector<RiskWarning> assess_risks(
        const PointCloud& source, const PointCloud& target,
        double tolerance_mm) {
    std::vector<RiskWarning> warnings;

    // Sparse data.
    if (source.size() < 100 || target.size() < 100) {
        warnings.push_back({RiskWarning::Level::CAUTION,
            "Sparse point cloud (<100 points)",
            "Consider denser scanning for reliable alignment"});
    }

    // Tight tolerance on large data.
    auto bbox = [](const PointCloud& pc) -> double {
        if (pc.size() < 2) return 0;
        return (pc.points().rowwise().maxCoeff() - pc.points().rowwise().minCoeff()).norm();
    };
    double extent = std::max(bbox(source), bbox(target));
    double ratio = (tolerance_mm > 0) ? extent / tolerance_mm : 0;

    if (ratio > 100000) {
        warnings.push_back({RiskWarning::Level::HIGH_RISK,
            "Tolerance (" + std::to_string(tolerance_mm * 1000) +
            " um) is extremely tight relative to part size (" +
            std::to_string(extent) + " mm)",
            "Verify scanner capability and thermal control"});
    } else if (ratio > 10000) {
        warnings.push_back({RiskWarning::Level::CAUTION,
            "Tight tolerance-to-part-size ratio (" + std::to_string(ratio) + ")",
            "Consider precision-tier gate requirements"});
    }

    // No normals.
    if (!source.has_normals() || !target.has_normals()) {
        warnings.push_back({RiskWarning::Level::INFO,
            "Point clouds without normals — point-to-plane/GICP unavailable",
            "Normals will be estimated; provide pre-computed normals for better results"});
    }

    return warnings;
}

// ============================================================================
// Main analysis
// ============================================================================

AssistResult analyze_and_suggest(
        const PointCloud& source,
        const PointCloud& target,
        double tolerance_mm) {
    AssistResult result;
    std::string ts = now_ts();

    // Normal estimation for landmarks.
    spatial::NormalEstimation src_norms;
    if (source.size() >= 3) {
        if (source.has_normals()) {
            src_norms.normals = source.normals();
            src_norms.curvatures = Eigen::VectorXd::Zero(source.size());
            // Re-estimate curvature.
            spatial::KdTree tree(source.points());
            for (Eigen::Index i = 0; i < source.size(); ++i) {
                std::vector<int> nbrs;
                std::vector<double> dsq;
                tree.knn(source.points().col(i), 10, nbrs, dsq);
                // Simple curvature: variance of normal dots.
                double sum_dot = 0;
                for (int ni : nbrs) {
                    if (ni != static_cast<int>(i))
                        sum_dot += std::abs(1.0 - std::abs(
                            source.normals().col(i).dot(source.normals().col(ni))));
                }
                src_norms.curvatures(i) = sum_dot / std::max(1, static_cast<int>(nbrs.size()) - 1);
            }
        } else {
            src_norms = spatial::estimate_normals_knn(source.points(), 15);
        }
    }

    // Landmarks.
    if (source.size() >= 10) {
        result.landmarks = suggest_landmarks(source, src_norms);
        result.log.push_back({ts, "landmark",
            std::to_string(result.landmarks.size()) + " landmarks suggested",
            false, false, ""});
    }

    // Parameters.
    result.recommended_params = recommend_params(source, target, tolerance_mm);
    result.log.push_back({ts, "parameter",
        "Registration params recommended (method=" +
        std::to_string(static_cast<int>(result.recommended_params.value.method)) + ")",
        false, false, ""});

    // Risk warnings.
    result.risk_warnings = assess_risks(source, target, tolerance_mm);
    for (auto& w : result.risk_warnings) {
        result.log.push_back({ts, "warning", w.risk, false, false, ""});
    }

    // Overlap region: use all points as a simple estimate.
    result.overlap_region.value.resize(static_cast<std::size_t>(source.size()));
    for (Eigen::Index i = 0; i < source.size(); ++i)
        result.overlap_region.value[static_cast<std::size_t>(i)] = i;
    result.overlap_region.confidence = 0.5;
    result.overlap_region.rationale = "Full cloud (overlap estimation requires alignment)";
    result.overlap_region.category = "overlap";

    return result;
}

// ============================================================================
// Validation gate
// ============================================================================

bool validate_params(
        const Suggestion<RegistrationParams>& params,
        const PointCloud& source,
        const PointCloud& target,
        std::vector<std::string>& errors) {
    errors.clear();
    const auto& p = params.value;

    if (p.max_correspondence_distance <= 0) {
        errors.push_back("max_correspondence_distance must be positive (" +
            std::to_string(p.max_correspondence_distance) + ")");
    }
    if (p.max_iterations <= 0) {
        errors.push_back("max_iterations must be positive (" +
            std::to_string(p.max_iterations) + ")");
    }
    if (source.size() < 3) {
        errors.push_back("Source cloud too small (" +
            std::to_string(source.size()) + " points)");
    }
    if (target.size() < 3) {
        errors.push_back("Target cloud too small (" +
            std::to_string(target.size()) + " points)");
    }

    return errors.empty();
}

} // namespace alignmesh::assist
