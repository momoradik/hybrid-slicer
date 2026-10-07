#include "alignmesh/spatial/sampling.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <random>
#include <vector>

namespace alignmesh::spatial {

using geometry::Vec3;

// ============================================================================
// Normal-space sampling (Rusinkiewicz & Levoy 2001)
// ============================================================================

std::vector<Eigen::Index> normal_space_sampling(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
        int n_bins) {
    if (points.cols() == 0 || normals.cols() != points.cols()) return {};

    // Map each normal to a bin on a discretized sphere.
    // Use (theta, phi) angular coordinates quantized into n_bins x n_bins grid.
    // theta ∈ [0, π], phi ∈ [0, 2π).
    constexpr double kPi = std::numbers::pi;
    double bin_theta = kPi / n_bins;
    double bin_phi = 2.0 * kPi / n_bins;

    // key = (theta_bin, phi_bin)
    using BinKey = std::pair<int, int>;
    std::map<BinKey, Eigen::Index> bins;

    for (Eigen::Index i = 0; i < points.cols(); ++i) {
        Vec3 n = normals.col(i);
        // Canonicalize: ensure z >= 0 (hemisphere, since n and -n are equivalent).
        if (n(2) < 0) n = -n;

        double theta = std::acos(std::clamp(n(2), -1.0, 1.0));
        double phi = std::atan2(n(1), n(0));
        if (phi < 0) phi += 2.0 * kPi;

        int bt = static_cast<int>(theta / bin_theta);
        int bp = static_cast<int>(phi / bin_phi);
        bt = std::clamp(bt, 0, n_bins - 1);
        bp = std::clamp(bp, 0, n_bins - 1);

        BinKey key{bt, bp};
        // Deterministic: keep first encountered point per bin.
        if (bins.find(key) == bins.end()) {
            bins[key] = i;
        }
    }

    std::vector<Eigen::Index> result;
    result.reserve(bins.size());
    for (auto& [key, idx] : bins) {
        result.push_back(idx);
    }
    return result;
}

// ============================================================================
// Geometrically stable sampling (Gelfand et al. 2003)
// ============================================================================

std::vector<Eigen::Index> stable_sampling(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
        int n_select,
        numerics::SeededRng& rng) {
    auto n = points.cols();
    if (n == 0 || n_select <= 0) return {};
    int select = std::min(n_select, static_cast<int>(n));

    // Each point contributes a constraint row to the rigid alignment
    // Jacobian. The constraint is the point-to-plane distance:
    //   n_i^T * (R*p_i + t - q_i) = 0
    //
    // The Jacobian w.r.t. the 6-DOF pose [omega, t] has rows:
    //   J_i = [ (p_i × n_i)^T, n_i^T ]   (6-vector)
    //
    // We greedily select points that maximize the smallest singular value
    // of the accumulated Jacobian, ensuring all 6 DOFs are constrained.

    // Compute per-point 6-vectors.
    Eigen::Matrix<double, 6, Eigen::Dynamic> J(6, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 p = points.col(i);
        Vec3 ni = normals.col(i);
        Vec3 cross = p.cross(ni);
        J(0, i) = cross(0);
        J(1, i) = cross(1);
        J(2, i) = cross(2);
        J(3, i) = ni(0);
        J(4, i) = ni(1);
        J(5, i) = ni(2);
    }

    std::vector<Eigen::Index> selected;
    selected.reserve(static_cast<std::size_t>(select));
    std::vector<bool> used(static_cast<std::size_t>(n), false);

    // Accumulated JTJ (6×6 matrix).
    Eigen::Matrix<double, 6, 6> JTJ = Eigen::Matrix<double, 6, 6>::Zero();

    for (int s = 0; s < select; ++s) {
        Eigen::Index best_idx = -1;
        double best_sv_min = -1.0;

        // For efficiency with large clouds, evaluate a random subset of
        // candidates rather than all remaining points.
        std::vector<Eigen::Index> candidates;
        int max_candidates = std::min(static_cast<int>(n), 200);

        if (n <= 200 || s < 6) {
            // For the first 6 points (critical for DOF coverage) or small
            // clouds, evaluate all.
            for (Eigen::Index i = 0; i < n; ++i) {
                if (!used[static_cast<std::size_t>(i)])
                    candidates.push_back(i);
            }
        } else {
            // Random subset of candidates.
            std::vector<Eigen::Index> remaining;
            for (Eigen::Index i = 0; i < n; ++i) {
                if (!used[static_cast<std::size_t>(i)])
                    remaining.push_back(i);
            }
            std::shuffle(remaining.begin(), remaining.end(), rng);
            candidates.assign(remaining.begin(),
                              remaining.begin() + std::min(max_candidates,
                                  static_cast<int>(remaining.size())));
        }

        for (auto ci : candidates) {
            // Trial: add this point's constraint.
            auto ji = J.col(ci);
            auto trial_JTJ = JTJ + ji * ji.transpose();

            // Compute smallest singular value of the 6x6 matrix.
            Eigen::JacobiSVD<Eigen::Matrix<double, 6, 6>> svd(trial_JTJ);
            double sv_min = svd.singularValues()(5);

            if (sv_min > best_sv_min) {
                best_sv_min = sv_min;
                best_idx = ci;
            }
        }

        if (best_idx < 0) break;

        selected.push_back(best_idx);
        used[static_cast<std::size_t>(best_idx)] = true;
        auto ji = J.col(best_idx);
        JTJ += ji * ji.transpose();
    }

    return selected;
}

} // namespace alignmesh::spatial
