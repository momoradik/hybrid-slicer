#define _USE_MATH_DEFINES
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/robustness.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

namespace alignmesh::registration {

using namespace geometry;

// ============================================================================
// FPFH feature computation
// ============================================================================
// Following Rusu et al., "Fast Point Feature Histograms (FPFH) for 3D
// Registration" (ICRA 2009). Tuned for metrology-scale (mm, not metres).

static constexpr int kFPFH_BINS = 11;
static constexpr int kFPFH_DIM = kFPFH_BINS * 3;  // 33

using FPFHDescriptor = Eigen::Matrix<double, kFPFH_DIM, 1>;

static void compute_darboux_features(
        const Vec3& p_s, const Vec3& n_s,
        const Vec3& p_t, const Vec3& n_t,
        double& alpha, double& phi, double& theta) {
    Vec3 d = p_t - p_s;
    double dist = d.norm();
    if (dist < 1e-15) { alpha = phi = theta = 0; return; }
    d /= dist;

    Vec3 u = n_s;
    Vec3 v = d.cross(u);
    double v_norm = v.norm();
    if (v_norm < 1e-15) { alpha = phi = theta = 0; return; }
    v /= v_norm;
    Vec3 w = u.cross(v);

    alpha = v.dot(n_t);
    phi = u.dot(d);
    theta = std::atan2(w.dot(n_t), u.dot(n_t));
}

static void bin_feature(double val, double min_val, double max_val,
                        int n_bins, FPFHDescriptor& hist, int offset) {
    double range = max_val - min_val;
    if (range < 1e-15) return;
    int bin = static_cast<int>((val - min_val) / range * n_bins);
    bin = std::clamp(bin, 0, n_bins - 1);
    hist(offset + bin) += 1.0;
}

struct FPFHCloud {
    std::vector<FPFHDescriptor> descriptors;
    Eigen::Matrix<double, 3, Eigen::Dynamic> points;
    Eigen::Matrix<double, 3, Eigen::Dynamic> normals;
};

static FPFHCloud compute_fpfh(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
        const spatial::KdTree& tree,
        double radius) {
    auto n = pts.cols();
    FPFHCloud result;
    result.points = pts;
    result.normals = normals;
    result.descriptors.resize(static_cast<std::size_t>(n));

    // Step 1: Compute SPFH for each point.
    std::vector<FPFHDescriptor> spfh(static_cast<std::size_t>(n),
                                      FPFHDescriptor::Zero());

    for (Eigen::Index i = 0; i < n; ++i) {
        std::vector<int> nbrs;
        std::vector<double> dsq;
        tree.radius_search(pts.col(i), radius, nbrs, dsq);

        for (int ni : nbrs) {
            if (ni == static_cast<int>(i)) continue;
            double alpha, phi, theta;
            compute_darboux_features(
                pts.col(i), normals.col(i),
                pts.col(static_cast<Eigen::Index>(ni)),
                normals.col(static_cast<Eigen::Index>(ni)),
                alpha, phi, theta);

            auto ui = static_cast<std::size_t>(i);
            bin_feature(alpha, -1.0, 1.0, kFPFH_BINS, spfh[ui], 0);
            bin_feature(phi, -1.0, 1.0, kFPFH_BINS, spfh[ui], kFPFH_BINS);
            bin_feature(theta, -M_PI, M_PI, kFPFH_BINS, spfh[ui], 2 * kFPFH_BINS);
        }

        // Normalize SPFH
        double sum = spfh[static_cast<std::size_t>(i)].sum();
        if (sum > 0) spfh[static_cast<std::size_t>(i)] /= sum;
    }

    // Step 2: Compute FPFH = SPFH(p) + 1/k * sum(1/d_k * SPFH(p_k))
    for (Eigen::Index i = 0; i < n; ++i) {
        auto ui = static_cast<std::size_t>(i);
        result.descriptors[ui] = spfh[ui];

        std::vector<int> nbrs;
        std::vector<double> dsq;
        tree.radius_search(pts.col(i), radius, nbrs, dsq);

        int count = 0;
        for (std::size_t j = 0; j < nbrs.size(); ++j) {
            if (nbrs[j] == static_cast<int>(i)) continue;
            double dist = std::sqrt(dsq[j]);
            if (dist < 1e-15) continue;
            result.descriptors[ui] += (1.0 / dist) * spfh[static_cast<std::size_t>(nbrs[j])];
            ++count;
        }
        if (count > 0) {
            result.descriptors[ui] /= static_cast<double>(count);
        }
    }

    return result;
}

// ============================================================================
// Feature matching
// ============================================================================

struct Correspondence {
    int source_idx;
    int target_idx;
    double feature_distance;
};

static std::vector<Correspondence> match_features(
        const FPFHCloud& source_fpfh,
        const FPFHCloud& target_fpfh,
        double ratio_threshold) {
    std::vector<Correspondence> correspondences;

    auto ns = source_fpfh.descriptors.size();

    for (std::size_t i = 0; i < ns; ++i) {
        double best_dist = std::numeric_limits<double>::max();
        double second_dist = std::numeric_limits<double>::max();
        int best_idx = -1;

        for (std::size_t j = 0; j < target_fpfh.descriptors.size(); ++j) {
            double dist = (source_fpfh.descriptors[i] - target_fpfh.descriptors[j])
                              .squaredNorm();
            if (dist < best_dist) {
                second_dist = best_dist;
                best_dist = dist;
                best_idx = static_cast<int>(j);
            } else if (dist < second_dist) {
                second_dist = dist;
            }
        }

        // Lowe's ratio test
        if (best_idx >= 0 && second_dist > 0 &&
            best_dist / second_dist < ratio_threshold * ratio_threshold) {
            correspondences.push_back({static_cast<int>(i), best_idx,
                                       std::sqrt(best_dist)});
        }
    }

    return correspondences;
}

// ============================================================================
// Global registration
// ============================================================================

CoarsePoseGuess global_register(
        const PointCloud& source,
        const PointCloud& target,
        const GlobalRegistrationSettings& settings) {
    CoarsePoseGuess result;
    result.seed = settings.seed;
    result.method = "FPFH+GNC-TLS";

    if (source.size() < 10 || target.size() < 10) {
        result.errors.push_back("Insufficient points (need >= 10 per cloud)");
        return result;
    }

    // ---- Downsample --------------------------------------------------------
    auto src_idx = spatial::voxel_downsample(source.points(), settings.voxel_size);
    auto tgt_idx = spatial::voxel_downsample(target.points(), settings.voxel_size);

    if (static_cast<int>(src_idx.size()) < 10 ||
        static_cast<int>(tgt_idx.size()) < 10) {
        result.errors.push_back("Too few points after voxel downsampling");
        return result;
    }

    auto extract = [](const PointCloud& pc, const std::vector<Eigen::Index>& idx) {
        auto ni = static_cast<Eigen::Index>(idx.size());
        Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, ni);
        for (Eigen::Index i = 0; i < ni; ++i)
            pts.col(i) = pc.points().col(idx[static_cast<std::size_t>(i)]);
        return pts;
    };

    auto src_pts = extract(source, src_idx);
    auto tgt_pts = extract(target, tgt_idx);

    // ---- Estimate normals --------------------------------------------------
    auto src_normals = spatial::estimate_normals_knn(src_pts, settings.normal_k);
    auto tgt_normals = spatial::estimate_normals_knn(tgt_pts, settings.normal_k);

    // ---- FPFH features -----------------------------------------------------
    double fpfh_radius = settings.voxel_size * settings.fpfh_radius_multiplier;
    spatial::KdTree src_tree(src_pts);
    spatial::KdTree tgt_tree(tgt_pts);

    auto src_fpfh = compute_fpfh(src_pts, src_normals.normals, src_tree, fpfh_radius);
    auto tgt_fpfh = compute_fpfh(tgt_pts, tgt_normals.normals, tgt_tree, fpfh_radius);

    // ---- Match features ----------------------------------------------------
    auto correspondences = match_features(src_fpfh, tgt_fpfh,
                                          settings.feature_match_ratio);

    result.num_putative_correspondences = static_cast<int>(correspondences.size());

    if (result.num_putative_correspondences < 3) {
        result.errors.push_back("Too few feature correspondences (" +
            std::to_string(result.num_putative_correspondences) + ")");
        return result;
    }

    // ---- Build correspondence matrices ------------------------------------
    auto nc = static_cast<Eigen::Index>(correspondences.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> corr_src(3, nc);
    Eigen::Matrix<double, 3, Eigen::Dynamic> corr_tgt(3, nc);
    for (Eigen::Index i = 0; i < nc; ++i) {
        auto& c = correspondences[static_cast<std::size_t>(i)];
        corr_src.col(i) = src_pts.col(c.source_idx);
        corr_tgt.col(i) = tgt_pts.col(c.target_idx);
    }

    // ---- GNC-TLS robust alignment ------------------------------------------
    GNCSettings gnc_settings;
    gnc_settings.kernel_param = settings.gnc_noise_bound;
    gnc_settings.initial_mu = settings.gnc_initial_mu;
    gnc_settings.mu_factor = settings.gnc_mu_factor;

    auto gnc_result = gnc_align(corr_src, corr_tgt, gnc_settings);

    if (!gnc_result.success) {
        result.errors.push_back("GNC alignment failed");
        for (auto& e : gnc_result.errors) result.errors.push_back(e);
        return result;
    }

    result.set_transform(gnc_result.transform);
    result.num_inlier_correspondences = gnc_result.num_inliers;
    result.inlier_ratio = static_cast<double>(gnc_result.num_inliers) /
                          static_cast<double>(correspondences.size());
    result.confidence = result.inlier_ratio;

    // Flag low confidence.
    if (result.inlier_ratio < 0.1) {
        result.warnings.push_back("Very low inlier ratio (" +
            std::to_string(result.inlier_ratio) +
            ") — coarse guess may be unreliable");
    }

    // Flag ambiguity heuristic: if many inliers have similar residuals,
    // there may be multiple valid poses (symmetric object).
    if (gnc_result.num_inliers > 0 && gnc_result.num_outliers > gnc_result.num_inliers) {
        result.is_ambiguous = true;
        result.warnings.push_back(
            "Possible ambiguity: more outliers than inliers — "
            "symmetric object or poor feature distinctiveness");
    }

    result.success = true;
    return result;
}

} // namespace alignmesh::registration
