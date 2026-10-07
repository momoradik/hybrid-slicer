#include "alignmesh/spatial/normals.h"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <vector>

namespace alignmesh::spatial {

using namespace geometry;

// ---- PCA normal estimation for a single point -----------------------------

struct LocalPCA {
    Vec3 normal;
    double curvature = 0;
    double reliability = 0;
    int neighbour_count = 0;
};

static LocalPCA compute_pca_normal(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        const std::vector<int>& neighbours) {
    LocalPCA result;
    result.neighbour_count = static_cast<int>(neighbours.size());

    if (neighbours.size() < 3) {
        result.normal = Vec3::UnitZ();
        result.reliability = 0;
        return result;
    }

    // Compute centroid.
    Vec3 centroid = Vec3::Zero();
    for (int idx : neighbours) centroid += pts.col(idx);
    centroid /= static_cast<double>(neighbours.size());

    // Covariance matrix.
    Mat3 cov = Mat3::Zero();
    for (int idx : neighbours) {
        Vec3 d = pts.col(idx) - centroid;
        cov += d * d.transpose();
    }
    cov /= static_cast<double>(neighbours.size());

    // Eigendecomposition (self-adjoint → sorted ascending).
    Eigen::SelfAdjointEigenSolver<Mat3> eig(cov);
    Vec3 eigenvalues = eig.eigenvalues();

    // Normal = eigenvector for smallest eigenvalue (index 0, ascending).
    result.normal = eig.eigenvectors().col(0);

    double lambda_sum = eigenvalues.sum();
    if (lambda_sum > 0) {
        result.curvature = eigenvalues(0) / lambda_sum;
    }

    // Reliability: based on eigenvalue spread and neighbour count.
    // High spread + many neighbours = reliable.
    if (eigenvalues(0) > 0) {
        double spread = eigenvalues(1) / eigenvalues(0);
        result.reliability = std::min(spread, 1e6) / 1e6;
    } else {
        result.reliability = 1.0;  // zero smallest eigenvalue = perfectly planar
    }
    if (result.neighbour_count >= 10) result.reliability = std::min(result.reliability + 0.5, 1.0);
    else result.reliability *= static_cast<double>(result.neighbour_count) / 10.0;

    return result;
}

// ---- MST-based orientation propagation ------------------------------------
//
// Propagates normal orientation from a seed point via BFS over the k-NN
// graph. At each step, child normal is flipped to be consistent with its
// parent's already-oriented normal. This ensures global consistency on
// connected, orientable surfaces.

static int orient_normals_bfs(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
        const KdTree& tree,
        int k_orient) {
    auto n = pts.cols();
    if (n <= 1) return 0;

    int flip_count = 0;
    int visit_count = 0;
    std::vector<bool> visited(static_cast<std::size_t>(n), false);
    std::queue<Eigen::Index> bfs;

    visited[0] = true;
    bfs.push(0);

    while (!bfs.empty()) {
        Eigen::Index src = bfs.front();
        bfs.pop();

        std::vector<int> nbrs;
        std::vector<double> dsq;
        tree.knn(pts.col(src), k_orient, nbrs, dsq);

        for (int ni : nbrs) {
            auto ui = static_cast<std::size_t>(ni);
            if (visited[ui]) continue;
            visited[ui] = true;
            ++visit_count;

            auto ci = static_cast<Eigen::Index>(ni);
            // Flip child if its normal opposes the parent's oriented normal.
            Vec3 src_normal = normals.col(src);
            Vec3 child_normal = normals.col(ci);
            double dot = src_normal.dot(child_normal);
            if (dot < 0.0) {
                normals(0, ci) = -normals(0, ci);
                normals(1, ci) = -normals(1, ci);
                normals(2, ci) = -normals(2, ci);
                ++flip_count;
            }
            bfs.push(ci);
        }
    }
    return flip_count;
}

// ---- public API -----------------------------------------------------------

NormalEstimation estimate_normals_knn(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        int k, const KdTree* tree_ptr) {
    auto n = points.cols();
    NormalEstimation result;
    result.normals.resize(3, n);
    result.curvatures.resize(n);
    result.reliabilities.resize(n);

    // Build tree if not provided.
    std::unique_ptr<KdTree> owned_tree;
    const KdTree* tree = tree_ptr;
    if (!tree) {
        owned_tree = std::make_unique<KdTree>(points);
        tree = owned_tree.get();
    }

    int actual_k = std::min(k, static_cast<int>(n));

    for (Eigen::Index i = 0; i < n; ++i) {
        std::vector<int> indices;
        std::vector<double> dists;
        tree->knn(points.col(i), actual_k, indices, dists);

        auto pca = compute_pca_normal(points, indices);
        result.normals.col(i) = pca.normal;
        result.curvatures(i) = pca.curvature;
        result.reliabilities(i) = pca.reliability;

        if (pca.reliability < 0.1) {
            result.unreliable_indices.push_back(i);
        }
    }

    // Step 1: Initial orientation via centroid heuristic — point normals
    // outward from the cloud centroid. This provides a good global seed
    // for smooth/convex surfaces.
    {
        Vec3 centroid = Vec3::Zero();
        for (Eigen::Index i = 0; i < n; ++i) centroid += points.col(i);
        centroid /= static_cast<double>(n);
        for (Eigen::Index i = 0; i < n; ++i) {
            Vec3 outward = points.col(i) - centroid;
            if (result.normals.col(i).dot(outward) < 0.0) {
                result.normals.col(i) = -result.normals.col(i);
            }
        }
    }

    // Step 2: BFS propagation to fix local inconsistencies.
    int k_orient = std::min(std::max(k, 10), static_cast<int>(n));
    orient_normals_bfs(points, result.normals, *tree, k_orient);

    // Iterative repair: for each point, check if it agrees with the
    // majority of its neighbours. If not, flip it. Repeat until stable.
    for (int pass = 0; pass < 5; ++pass) {
        int flips = 0;
        for (Eigen::Index i = 0; i < n; ++i) {
            std::vector<int> nbrs;
            std::vector<double> dsq;
            tree->knn(points.col(i), k_orient, nbrs, dsq);

            int agree = 0, disagree = 0;
            for (int ni : nbrs) {
                if (ni == static_cast<int>(i)) continue;
                auto ci = static_cast<Eigen::Index>(ni);
                if (result.normals.col(i).dot(result.normals.col(ci)) > 0)
                    ++agree;
                else
                    ++disagree;
            }
            if (disagree > agree) {
                result.normals.col(i) = -result.normals.col(i);
                ++flips;
            }
        }
        if (flips == 0) break;
    }

    return result;
}

NormalEstimation estimate_normals_radius(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        double radius, const KdTree* tree_ptr) {
    auto n = points.cols();
    NormalEstimation result;
    result.normals.resize(3, n);
    result.curvatures.resize(n);
    result.reliabilities.resize(n);

    std::unique_ptr<KdTree> owned_tree;
    const KdTree* tree = tree_ptr;
    if (!tree) {
        owned_tree = std::make_unique<KdTree>(points);
        tree = owned_tree.get();
    }

    for (Eigen::Index i = 0; i < n; ++i) {
        std::vector<int> indices;
        std::vector<double> dists;
        tree->radius_search(points.col(i), radius, indices, dists);

        auto pca = compute_pca_normal(points, indices);
        result.normals.col(i) = pca.normal;
        result.curvatures(i) = pca.curvature;
        result.reliabilities(i) = pca.reliability;

        if (pca.reliability < 0.1) {
            result.unreliable_indices.push_back(i);
        }
    }

    int k_orient = std::min(10, static_cast<int>(n));
    orient_normals_bfs(points, result.normals, *tree, k_orient);

    // Iterative repair (same as knn variant).
    for (int pass = 0; pass < 5; ++pass) {
        int flips = 0;
        for (Eigen::Index i = 0; i < n; ++i) {
            std::vector<int> nbrs;
            std::vector<double> dsq;
            tree->knn(points.col(i), k_orient, nbrs, dsq);
            int agree = 0, disagree = 0;
            for (int ni : nbrs) {
                if (ni == static_cast<int>(i)) continue;
                auto ci = static_cast<Eigen::Index>(ni);
                if (result.normals.col(i).dot(result.normals.col(ci)) > 0)
                    ++agree;
                else ++disagree;
            }
            if (disagree > agree) {
                result.normals.col(i) = -result.normals.col(i);
                ++flips;
            }
        }
        if (flips == 0) break;
    }

    return result;
}

} // namespace alignmesh::spatial
