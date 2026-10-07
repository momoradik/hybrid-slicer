#pragma once

#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/reference_geometry.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// Deviation analysis (mesh tier)
// ============================================================================
//
// Computes point-to-mesh deviations with full statistics, confidence
// intervals, spatial-autocorrelation caveats, and sampling-adequacy checks.
//
// NOTE: this is the MESH tier. The CAD/true-surface tier (Session 15)
// will add analytic surface deviation.
// ============================================================================

/// Full deviation statistics with honesty features.
struct DeviationStatistics {
    double min = 0;
    double max = 0;
    double mean = 0;
    double median = 0;
    double rms = 0;
    double std_dev = 0;
    double p90 = 0;   // 90th percentile
    double p95 = 0;   // 95th percentile
    double p99 = 0;   // 99th percentile

    /// Percent of points within the specified tolerance.
    double percent_within_tolerance = 0;
    double tolerance_used = 0;

    /// Outlier and overlap statistics.
    double outlier_percent = 0;
    double overlap_percent = 0;

    int n_points = 0;
    int n_outliers = 0;
    int n_within_tolerance = 0;

    /// Confidence interval on the mean (95% CI), accounting for
    /// effective sample size under spatial autocorrelation.
    double ci_mean_lower = 0;
    double ci_mean_upper = 0;
    double effective_sample_size = 0;

    /// Sampling adequacy flag.
    bool sampling_adequate = true;
    double point_density = 0;          // points per unit area
    double min_feature_resolution = 0; // smallest resolvable feature size
    std::string sampling_warning;
};

/// Per-point deviation result.
struct PointDeviation {
    double distance = 0;       // unsigned distance to mesh
    double signed_distance = 0; // signed (positive = outside, negative = inside)
    bool sign_reliable = false; // true if normal was available for sign
    int nearest_triangle = -1;  // index of the closest triangle
    geometry::Vec3 closest_point = geometry::Vec3::Zero();
};

/// Full deviation analysis result.
struct DeviationResult {
    bool success = false;

    /// Per-point deviations.
    std::vector<PointDeviation> deviations;

    /// Global statistics (unsigned distances).
    DeviationStatistics unsigned_stats;

    /// Global statistics (signed distances, only meaningful if normals available).
    DeviationStatistics signed_stats;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Settings for deviation analysis.
struct DeviationSettings {
    /// Tolerance for percent-within-tolerance.
    double tolerance = 0.1;

    /// Max distance: points beyond this are considered outliers.
    double max_distance = 10.0;

    /// Spatial autocorrelation range estimate (typical point spacing).
    /// Points within this range are considered correlated.
    /// Set to 0 for no autocorrelation correction.
    double autocorrelation_range = 0;

    /// Minimum point density for sampling adequacy (points per unit area).
    /// If actual density is below this, sampling is flagged as inadequate.
    double min_density = 0;

    /// Feature size to resolve (for Nyquist check).
    /// Density must be at least 2× the inverse of this.
    double feature_size = 0;
};

/// Compute point-to-mesh deviation analysis.
///
/// @param points     Query points (3×N).
/// @param mesh       Reference mesh.
/// @param settings   Analysis parameters.
DeviationResult compute_deviation(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const geometry::TriangleMesh& mesh,
    const DeviationSettings& settings = {});

/// Compute deviation against any ReferenceGeometry (mesh or CAD B-rep).
///
/// This is the unified conformance path. For MeshReference it delegates
/// to AABBTree closest-triangle (same as the TriangleMesh overload).
/// For CadReference it uses BVH + analytic refine to the true trimmed face.
///
/// @param points     Query points (3xN).
/// @param ref        Reference geometry (mesh or CAD).
/// @param settings   Analysis parameters.
DeviationResult compute_deviation(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const geometry::ReferenceGeometry& ref,
    const DeviationSettings& settings = {});

/// Closest point on a triangle to a query point.
/// Returns the closest point and the squared distance.
geometry::Vec3 closest_point_on_triangle(
    const geometry::Vec3& query,
    const geometry::Vec3& v0,
    const geometry::Vec3& v1,
    const geometry::Vec3& v2,
    double& dist_sq);

} // namespace alignmesh::analysis
