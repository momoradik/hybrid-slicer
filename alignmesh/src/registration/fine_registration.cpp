#define _USE_MATH_DEFINES
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/spatial/voxel_downsample.h"

#include <small_gicp/points/point_cloud.hpp>
#include <small_gicp/ann/kdtree.hpp>
#include <small_gicp/factors/icp_factor.hpp>
#include <small_gicp/factors/plane_icp_factor.hpp>
#include <small_gicp/factors/gicp_factor.hpp>
#include <small_gicp/registration/registration.hpp>
#include <small_gicp/registration/reduction.hpp>

#include <algorithm>
#include <cmath>

namespace alignmesh::registration {

using namespace geometry;

// ---- convert our PointCloud to small_gicp::PointCloud ---------------------

static std::shared_ptr<small_gicp::PointCloud> to_sgicp(
        const PointCloud& pc, bool need_normals, bool need_covs, int normal_k) {
    auto n = pc.size();
    auto spc = std::make_shared<small_gicp::PointCloud>();
    spc->resize(static_cast<std::size_t>(n));

    for (Eigen::Index i = 0; i < n; ++i) {
        spc->point(static_cast<std::size_t>(i)) << pc.points().col(i), 1.0;
    }

    // Normals (for point-to-plane and GICP)
    Eigen::Matrix<double, 3, Eigen::Dynamic> norms_3;
    if (need_normals || need_covs) {
        if (pc.has_normals()) {
            norms_3 = pc.normals();
        } else if (n >= 3) {
            auto est = spatial::estimate_normals_knn(pc.points(), normal_k);
            norms_3 = est.normals;
        }
        for (Eigen::Index i = 0; i < n; ++i) {
            spc->normal(static_cast<std::size_t>(i)) << norms_3.col(i), 0.0;
        }
    }

    // Covariances (for GICP)
    if (need_covs && norms_3.cols() == n) {
        for (Eigen::Index i = 0; i < n; ++i) {
            Vec3 nm = norms_3.col(i);
            // GICP covariance: large in tangent plane, small along normal.
            Mat3 cov3 = Mat3::Identity() - 0.999 * nm * nm.transpose();
            auto ui = static_cast<std::size_t>(i);
            spc->cov(ui).setZero();
            spc->cov(ui).block<3, 3>(0, 0) = cov3;
        }
    }

    return spc;
}

// ---- subsample helper ------------------------------------------------------

static PointCloud subsample(const PointCloud& pc, double voxel_size) {
    auto indices = spatial::voxel_downsample(pc.points(), voxel_size);
    if (indices.empty()) return pc;

    auto ni = static_cast<Eigen::Index>(indices.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> pts(3, ni);
    for (Eigen::Index i = 0; i < ni; ++i)
        pts.col(i) = pc.points().col(indices[static_cast<std::size_t>(i)]);

    std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> nrm;
    if (pc.has_normals()) {
        Eigen::Matrix<double, 3, Eigen::Dynamic> n(3, ni);
        for (Eigen::Index i = 0; i < ni; ++i)
            n.col(i) = pc.normals().col(indices[static_cast<std::size_t>(i)]);
        nrm = std::move(n);
    }
    return PointCloud(std::move(pts), std::move(nrm), std::nullopt);
}

// ---- run one level of registration -----------------------------------------

struct LevelResult {
    Eigen::Isometry3d T;
    double error = 0;
    int iterations = 0;
    int num_inliers = 0;
    bool converged = false;
};

template <typename Factor>
static LevelResult run_typed(
        const small_gicp::PointCloud& target,
        const small_gicp::PointCloud& source,
        const small_gicp::KdTree<small_gicp::PointCloud>& tree,
        const Eigen::Isometry3d& init_T,
        double max_dist, int max_iters,
        double rot_tol, double trans_tol) {
    small_gicp::Registration<Factor, small_gicp::SerialReduction> reg;
    reg.rejector.max_dist_sq = max_dist * max_dist;
    reg.criteria.rotation_eps = rot_tol;
    reg.criteria.translation_eps = trans_tol;
    reg.optimizer.max_iterations = max_iters;

    auto r = reg.align(target, source, tree, init_T);

    LevelResult out;
    out.T = r.T_target_source;
    out.error = r.error;
    out.iterations = static_cast<int>(r.iterations) + 1;
    out.num_inliers = static_cast<int>(r.num_inliers);
    out.converged = r.converged;
    return out;
}

static LevelResult run_level(
        const PointCloud& moving,
        const PointCloud& reference,
        const Eigen::Isometry3d& init_T,
        ICPMethod method,
        double max_dist, int max_iters,
        double rot_tol, double trans_tol,
        int normal_k) {
    bool need_normals = (method != ICPMethod::POINT_TO_POINT);
    bool need_covs = (method == ICPMethod::GICP || method == ICPMethod::VGICP);

    auto spc_source = to_sgicp(moving, need_normals, need_covs, normal_k);
    auto spc_target = to_sgicp(reference, need_normals, need_covs, normal_k);

    auto tree = std::make_shared<small_gicp::KdTree<small_gicp::PointCloud>>(spc_target);

    switch (method) {
        case ICPMethod::POINT_TO_POINT:
            return run_typed<small_gicp::ICPFactor>(
                *spc_target, *spc_source, *tree, init_T,
                max_dist, max_iters, rot_tol, trans_tol);
        case ICPMethod::POINT_TO_PLANE:
            return run_typed<small_gicp::PointToPlaneICPFactor>(
                *spc_target, *spc_source, *tree, init_T,
                max_dist, max_iters, rot_tol, trans_tol);
        case ICPMethod::GICP:
        case ICPMethod::VGICP:
            return run_typed<small_gicp::GICPFactor>(
                *spc_target, *spc_source, *tree, init_T,
                max_dist, max_iters, rot_tol, trans_tol);
    }
    return {};  // unreachable
}

// ---- public API ------------------------------------------------------------

FineRegistrationResult fine_register(
        const PointCloud& moving,
        const PointCloud& reference,
        const RigidTransform& initial_guess,
        const FineRegistrationSettings& settings) {
    FineRegistrationResult result;

    if (moving.size() < 3) {
        result.errors.push_back("Moving cloud too small (" +
            std::to_string(moving.size()) + " points)");
        return result;
    }
    if (reference.size() < 3) {
        result.errors.push_back("Reference cloud too small (" +
            std::to_string(reference.size()) + " points)");
        return result;
    }

    Eigen::Isometry3d current_T = Eigen::Isometry3d::Identity();
    current_T.linear() = initial_guess.rotation();
    current_T.translation() = initial_guess.translation();

    // Build multi-resolution schedule.
    std::vector<double> voxels = settings.voxel_schedule;
    std::vector<double> dists = settings.distance_schedule;
    if (voxels.empty()) {
        voxels = {0.0};
        dists = {settings.max_correspondence_distance};
    }
    while (dists.size() < voxels.size())
        dists.push_back(settings.max_correspondence_distance);

    int total_iters = 0;

    for (std::size_t level = 0; level < voxels.size(); ++level) {
        PointCloud mov = (voxels[level] > 0) ? subsample(moving, voxels[level]) : moving;
        PointCloud ref = (voxels[level] > 0) ? subsample(reference, voxels[level]) : reference;

        if (mov.size() < 3 || ref.size() < 3) {
            result.warnings.push_back("Level " + std::to_string(level) +
                " too sparse after downsampling, skipped");
            continue;
        }

        auto lr = run_level(mov, ref, current_T,
            settings.method, dists[level], settings.max_iterations,
            settings.rotation_tolerance, settings.translation_tolerance,
            settings.normal_estimation_k);

        current_T = lr.T;
        total_iters += lr.iterations;

        PerIterationRecord rec;
        rec.error = lr.error;
        rec.num_inliers = lr.num_inliers;
        result.iterations.push_back(rec);
    }

    // Convert to our types.
    Mat3 R = current_T.rotation();
    Vec3 t = current_T.translation();
    result.transform = RigidTransform::from_rotation_translation(R, t);
    result.total_iterations = total_iters;

    // Final evaluation: run 1 iteration on full-res to count correspondences.
    auto final_eval = run_level(moving, reference, current_T,
        settings.method, settings.max_correspondence_distance, 1,
        settings.rotation_tolerance, settings.translation_tolerance,
        settings.normal_estimation_k);

    result.final_rms = (final_eval.num_inliers > 0) ?
        std::sqrt(final_eval.error / std::max(final_eval.num_inliers, 1)) : 0;
    result.num_correspondences = final_eval.num_inliers;
    result.overlap_ratio = static_cast<double>(final_eval.num_inliers) /
                           static_cast<double>(std::max(moving.size(), Eigen::Index(1)));

    result.converged = !result.iterations.empty();
    if (result.overlap_ratio < 0.3) {
        result.warnings.push_back("Low overlap (" +
            std::to_string(result.overlap_ratio) + ")");
    }

    result.success = (result.num_correspondences >= 3);
    return result;
}

} // namespace alignmesh::registration
