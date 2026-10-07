#include "alignmesh/analysis/deviation.h"
#include "alignmesh/spatial/aabb_tree.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace alignmesh::analysis {

using namespace geometry;

// ============================================================================
// Closest point on triangle (Ericson, "Real-Time Collision Detection")
// ============================================================================

Vec3 closest_point_on_triangle(
        const Vec3& query,
        const Vec3& v0, const Vec3& v1, const Vec3& v2,
        double& dist_sq) {
    Vec3 ab = v1 - v0;
    Vec3 ac = v2 - v0;
    Vec3 ap = query - v0;

    double d1 = ab.dot(ap);
    double d2 = ac.dot(ap);
    if (d1 <= 0 && d2 <= 0) { dist_sq = ap.squaredNorm(); return v0; }

    Vec3 bp = query - v1;
    double d3 = ab.dot(bp);
    double d4 = ac.dot(bp);
    if (d3 >= 0 && d4 <= d3) { dist_sq = bp.squaredNorm(); return v1; }

    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        double v = d1 / (d1 - d3);
        Vec3 p = v0 + v * ab;
        dist_sq = (query - p).squaredNorm();
        return p;
    }

    Vec3 cp = query - v2;
    double d5 = ab.dot(cp);
    double d6 = ac.dot(cp);
    if (d6 >= 0 && d5 <= d6) { dist_sq = cp.squaredNorm(); return v2; }

    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        double w = d2 / (d2 - d6);
        Vec3 p = v0 + w * ac;
        dist_sq = (query - p).squaredNorm();
        return p;
    }

    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        Vec3 p = v1 + w * (v2 - v1);
        dist_sq = (query - p).squaredNorm();
        return p;
    }

    double denom = 1.0 / (va + vb + vc);
    double v = vb * denom;
    double w = vc * denom;
    Vec3 p = v0 + v * ab + w * ac;
    dist_sq = (query - p).squaredNorm();
    return p;
}

// ============================================================================
// Statistics computation
// ============================================================================

static DeviationStatistics compute_stats(
        const std::vector<double>& values,
        double tolerance,
        double autocorrelation_range,
        double min_density,
        double feature_size,
        double mesh_area) {
    DeviationStatistics stats;
    auto n = static_cast<int>(values.size());
    stats.n_points = n;
    stats.tolerance_used = tolerance;

    if (n == 0) return stats;

    // Sort for order statistics.
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    stats.min = sorted.front();
    stats.max = sorted.back();

    // Mean via compensated summation.
    stats.mean = numerics::neumaier_sum(sorted.begin(), sorted.end()) / n;

    // Median.
    if (n % 2 == 0) {
        stats.median = (sorted[static_cast<std::size_t>(n / 2 - 1)] +
                        sorted[static_cast<std::size_t>(n / 2)]) / 2.0;
    } else {
        stats.median = sorted[static_cast<std::size_t>(n / 2)];
    }

    // RMS.
    std::vector<double> sq(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        sq[static_cast<std::size_t>(i)] = values[static_cast<std::size_t>(i)] *
                                          values[static_cast<std::size_t>(i)];
    stats.rms = std::sqrt(numerics::neumaier_sum(sq.begin(), sq.end()) / n);

    // Std dev.
    std::vector<double> dev_sq(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        double d = values[static_cast<std::size_t>(i)] - stats.mean;
        dev_sq[static_cast<std::size_t>(i)] = d * d;
    }
    double variance = numerics::neumaier_sum(dev_sq.begin(), dev_sq.end()) /
                      std::max(n - 1, 1);
    stats.std_dev = std::sqrt(variance);

    // Percentiles.
    auto percentile = [&](double p) -> double {
        double idx = p / 100.0 * (n - 1);
        int lo = static_cast<int>(std::floor(idx));
        int hi = std::min(lo + 1, n - 1);
        double frac = idx - lo;
        return sorted[static_cast<std::size_t>(lo)] * (1.0 - frac) +
               sorted[static_cast<std::size_t>(hi)] * frac;
    };
    stats.p90 = percentile(90);
    stats.p95 = percentile(95);
    stats.p99 = percentile(99);

    // Within tolerance.
    stats.n_within_tolerance = 0;
    for (int i = 0; i < n; ++i) {
        if (std::abs(values[static_cast<std::size_t>(i)]) <= tolerance)
            ++stats.n_within_tolerance;
    }
    stats.percent_within_tolerance = 100.0 * stats.n_within_tolerance / n;

    // ---- Confidence interval with spatial-autocorrelation correction --------
    // Effective sample size: n_eff = n * (spacing / autocorrelation_range)²
    // clamped to [1, n]. When autocorrelation_range = 0, n_eff = n.
    stats.effective_sample_size = static_cast<double>(n);
    if (autocorrelation_range > 0 && mesh_area > 0) {
        double point_spacing = std::sqrt(mesh_area / n);
        double ratio = point_spacing / autocorrelation_range;
        stats.effective_sample_size = std::max(1.0,
            std::min(static_cast<double>(n), n * ratio * ratio));
    }

    double se = stats.std_dev / std::sqrt(stats.effective_sample_size);
    stats.ci_mean_lower = stats.mean - 1.96 * se;
    stats.ci_mean_upper = stats.mean + 1.96 * se;

    // ---- Sampling adequacy (Nyquist-style) ----------------------------------
    if (mesh_area > 0) {
        stats.point_density = n / mesh_area;
    }
    if (feature_size > 0) {
        stats.min_feature_resolution = 2.0 / std::sqrt(stats.point_density + 1e-20);
        if (stats.min_feature_resolution > feature_size) {
            stats.sampling_adequate = false;
            stats.sampling_warning =
                "Point density insufficient to resolve features of size " +
                std::to_string(feature_size) +
                "; minimum resolvable = " +
                std::to_string(stats.min_feature_resolution);
        }
    }
    if (min_density > 0 && stats.point_density < min_density) {
        stats.sampling_adequate = false;
        if (stats.sampling_warning.empty()) {
            stats.sampling_warning =
                "Point density (" + std::to_string(stats.point_density) +
                ") below required minimum (" + std::to_string(min_density) + ")";
        }
    }

    return stats;
}

// ============================================================================
// Deviation analysis
// ============================================================================

DeviationResult compute_deviation(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const TriangleMesh& mesh,
        const DeviationSettings& settings) {
    DeviationResult result;

    if (points.cols() == 0) {
        result.errors.push_back("No query points");
        return result;
    }
    if (mesh.num_triangles() == 0) {
        result.errors.push_back("Empty reference mesh");
        return result;
    }

    auto np = points.cols();
    auto nf = mesh.num_triangles();
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();

    // Build AABB tree over mesh triangles for exact closest-point queries.
    // O(N log N) build, O(log N) per query, exact (never misses the closest
    // triangle). Deterministic traversal order (nearer child first).
    spatial::AABBTree aabb(mesh);

    // Estimate mesh area (for density calculations).
    double mesh_area = 0;
    for (Eigen::Index f = 0; f < nf; ++f) {
        Vec3 a = verts.col(tris(0, f));
        Vec3 b = verts.col(tris(1, f));
        Vec3 c = verts.col(tris(2, f));
        mesh_area += 0.5 * (b - a).cross(c - a).norm();
    }

    result.deviations.resize(static_cast<std::size_t>(np));
    std::vector<double> unsigned_dists;
    std::vector<double> signed_dists;
    unsigned_dists.reserve(static_cast<std::size_t>(np));
    signed_dists.reserve(static_cast<std::size_t>(np));

    int n_outliers = 0;

    // Point-to-surface queries — each finds the exact closest point on
    // the closest triangle via the AABB tree (not nearest vertex).
    for (Eigen::Index i = 0; i < np; ++i) {
        Vec3 query = points.col(i);

        auto hit = aabb.closest_point(query);

        double dist = std::sqrt(hit.distance_sq);
        auto ui = static_cast<std::size_t>(i);
        result.deviations[ui].distance = dist;
        result.deviations[ui].closest_point = hit.closest_point;
        result.deviations[ui].nearest_triangle = hit.triangle_index;

        // Signed distance using triangle normal.
        if (hit.triangle_index >= 0) {
            Vec3 a = verts.col(tris(0, hit.triangle_index));
            Vec3 b = verts.col(tris(1, hit.triangle_index));
            Vec3 c = verts.col(tris(2, hit.triangle_index));
            Vec3 tri_normal = (b - a).cross(c - a);
            double nlen = tri_normal.norm();
            if (nlen > 1e-15) {
                tri_normal /= nlen;
                double signed_d = tri_normal.dot(query - hit.closest_point);
                result.deviations[ui].signed_distance = signed_d;
                result.deviations[ui].sign_reliable = true;
            } else {
                result.deviations[ui].signed_distance = dist;
                result.deviations[ui].sign_reliable = false;
            }
        }

        // Outlier check.
        if (dist > settings.max_distance) {
            ++n_outliers;
        }

        unsigned_dists.push_back(dist);
        signed_dists.push_back(result.deviations[ui].signed_distance);
    }

    // Statistics.
    result.unsigned_stats = compute_stats(
        unsigned_dists, settings.tolerance,
        settings.autocorrelation_range,
        settings.min_density, settings.feature_size, mesh_area);

    result.signed_stats = compute_stats(
        signed_dists, settings.tolerance,
        settings.autocorrelation_range,
        settings.min_density, settings.feature_size, mesh_area);

    result.unsigned_stats.n_outliers = n_outliers;
    result.unsigned_stats.outlier_percent = 100.0 * n_outliers / np;
    result.unsigned_stats.overlap_percent = 100.0 * (static_cast<int>(np) - n_outliers) /
                                            static_cast<double>(np);

    result.success = true;
    return result;
}

// ============================================================================
// Deviation analysis via ReferenceGeometry abstraction
// ============================================================================
//
// Unified conformance path: delegates closest-point queries to the
// ReferenceGeometry implementation (MeshReference or CadReference).
// Statistics computation is shared with the mesh-only overload above.

DeviationResult compute_deviation(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const geometry::ReferenceGeometry& ref,
        const DeviationSettings& settings) {
    DeviationResult result;

    if (points.cols() == 0) {
        result.errors.push_back("No query points");
        return result;
    }

    auto np = points.cols();
    result.deviations.resize(static_cast<std::size_t>(np));
    std::vector<double> unsigned_dists;
    std::vector<double> signed_dists;
    unsigned_dists.reserve(static_cast<std::size_t>(np));
    signed_dists.reserve(static_cast<std::size_t>(np));

    int n_outliers = 0;

    for (Eigen::Index i = 0; i < np; ++i) {
        Vec3 query = points.col(i);
        auto hit = ref.closest_point_on_surface(query);

        auto ui = static_cast<std::size_t>(i);
        result.deviations[ui].distance = hit.unsigned_distance;
        result.deviations[ui].closest_point = hit.point;
        result.deviations[ui].nearest_triangle = hit.face_index;

        if (hit.status == geometry::SurfaceQueryStatus::OK) {
            result.deviations[ui].signed_distance = hit.signed_distance;
            result.deviations[ui].sign_reliable = true;
        } else if (hit.status == geometry::SurfaceQueryStatus::DEGENERATE_NORMAL) {
            result.deviations[ui].signed_distance = hit.unsigned_distance;
            result.deviations[ui].sign_reliable = false;
        } else {
            result.deviations[ui].signed_distance = hit.unsigned_distance;
            result.deviations[ui].sign_reliable = false;
        }

        if (hit.unsigned_distance > settings.max_distance) {
            ++n_outliers;
        }

        unsigned_dists.push_back(hit.unsigned_distance);
        signed_dists.push_back(result.deviations[ui].signed_distance);
    }

    // Mesh area is not available through the abstraction — use 0 (disables
    // density-based sampling adequacy, which requires mesh-specific area
    // computation). The CAD tier can override this if needed.
    double mesh_area = 0;

    result.unsigned_stats = compute_stats(
        unsigned_dists, settings.tolerance,
        settings.autocorrelation_range,
        settings.min_density, settings.feature_size, mesh_area);

    result.signed_stats = compute_stats(
        signed_dists, settings.tolerance,
        settings.autocorrelation_range,
        settings.min_density, settings.feature_size, mesh_area);

    result.unsigned_stats.n_outliers = n_outliers;
    result.unsigned_stats.outlier_percent = 100.0 * n_outliers / np;
    result.unsigned_stats.overlap_percent = 100.0 * (static_cast<int>(np) - n_outliers) /
                                            static_cast<double>(np);

    result.success = true;
    return result;
}

} // namespace alignmesh::analysis
