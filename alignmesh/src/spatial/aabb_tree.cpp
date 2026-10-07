#include "alignmesh/spatial/aabb_tree.h"
#include "alignmesh/analysis/deviation.h"  // closest_point_on_triangle

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

namespace alignmesh::spatial {

// ============================================================================
// AABB node and tree internals
// ============================================================================

struct AABB {
    Eigen::Vector3d lo = Eigen::Vector3d::Constant(std::numeric_limits<double>::max());
    Eigen::Vector3d hi = Eigen::Vector3d::Constant(-std::numeric_limits<double>::max());

    void expand(const Eigen::Vector3d& p) {
        lo = lo.cwiseMin(p);
        hi = hi.cwiseMax(p);
    }

    void expand(const AABB& other) {
        lo = lo.cwiseMin(other.lo);
        hi = hi.cwiseMax(other.hi);
    }

    /// Squared distance from point to AABB (0 if inside).
    /// This is a LOWER BOUND on the squared distance to any geometry inside.
    double distance_sq(const Eigen::Vector3d& p) const {
        double dsq = 0;
        for (int d = 0; d < 3; ++d) {
            if (p[d] < lo[d]) { double v = lo[d] - p[d]; dsq += v * v; }
            else if (p[d] > hi[d]) { double v = p[d] - hi[d]; dsq += v * v; }
        }
        return dsq;
    }

    int longest_axis() const {
        Eigen::Vector3d ext = hi - lo;
        if (ext.x() >= ext.y() && ext.x() >= ext.z()) return 0;
        if (ext.y() >= ext.z()) return 1;
        return 2;
    }
};

struct AABBNode {
    AABB box;
    int left = -1;    // child indices (-1 = leaf)
    int right = -1;
    int tri_start = 0;
    int tri_count = 0; // >0 for leaf nodes
};

struct AABBTree::Impl {
    const geometry::TriangleMesh* mesh = nullptr;
    std::vector<AABBNode> nodes;
    std::vector<int> tri_indices; // permuted triangle indices

    static constexpr int LEAF_SIZE = 4;

    void build(const geometry::TriangleMesh& m) {
        mesh = &m;
        auto nf = static_cast<int>(m.num_triangles());
        tri_indices.resize(static_cast<std::size_t>(nf));
        std::iota(tri_indices.begin(), tri_indices.end(), 0);

        // Precompute triangle centroids for splitting.
        std::vector<Eigen::Vector3d> centroids(static_cast<std::size_t>(nf));
        const auto& V = m.vertices();
        const auto& F = m.triangles();
        for (int f = 0; f < nf; ++f) {
            centroids[static_cast<std::size_t>(f)] =
                (V.col(F(0, f)) + V.col(F(1, f)) + V.col(F(2, f))) / 3.0;
        }

        nodes.reserve(static_cast<std::size_t>(2 * nf)); // upper bound
        build_recursive(0, nf, centroids);
    }

    int build_recursive(int start, int end, const std::vector<Eigen::Vector3d>& centroids) {
        int node_idx = static_cast<int>(nodes.size());
        nodes.emplace_back();

        const auto& V = mesh->vertices();
        const auto& F = mesh->triangles();

        // Compute AABB over all triangles in [start, end).
        for (int i = start; i < end; ++i) {
            int fi = tri_indices[static_cast<std::size_t>(i)];
            nodes[static_cast<std::size_t>(node_idx)].box.expand(V.col(F(0, fi)));
            nodes[static_cast<std::size_t>(node_idx)].box.expand(V.col(F(1, fi)));
            nodes[static_cast<std::size_t>(node_idx)].box.expand(V.col(F(2, fi)));
        }

        int count = end - start;
        if (count <= LEAF_SIZE) {
            nodes[static_cast<std::size_t>(node_idx)].tri_start = start;
            nodes[static_cast<std::size_t>(node_idx)].tri_count = count;
            return node_idx;
        }

        // Split: median of centroids along longest axis.
        int axis = nodes[static_cast<std::size_t>(node_idx)].box.longest_axis();
        int mid = start + count / 2;
        std::nth_element(
            tri_indices.begin() + start,
            tri_indices.begin() + mid,
            tri_indices.begin() + end,
            [&](int a, int b) {
                return centroids[static_cast<std::size_t>(a)][axis] <
                       centroids[static_cast<std::size_t>(b)][axis];
            });

        int left = build_recursive(start, mid, centroids);
        int right = build_recursive(mid, end, centroids);
        // Re-fetch — vector may have reallocated during recursive calls.
        nodes[static_cast<std::size_t>(node_idx)].left = left;
        nodes[static_cast<std::size_t>(node_idx)].right = right;
        return node_idx;
    }

    // Iterative closest-point query — safe for arbitrarily deep BVH trees.
    SurfaceNNResult query(const Eigen::Vector3d& p) const {
        SurfaceNNResult best;
        best.distance_sq = std::numeric_limits<double>::max();
        if (nodes.empty()) return best;

        constexpr int MAX_STACK = 128;
        int stack[MAX_STACK];
        int top = 0;
        stack[top++] = 0;  // root

        const auto& V = mesh->vertices();
        const auto& F = mesh->triangles();
        int n_nodes = static_cast<int>(nodes.size());

        while (top > 0) {
            int node_idx = stack[--top];
            if (node_idx < 0 || node_idx >= n_nodes) continue; // bounds guard

            const auto& node = nodes[static_cast<std::size_t>(node_idx)];

            // Prune: if the AABB is farther than the current best, skip.
            if (node.box.distance_sq(p) >= best.distance_sq) continue;

            if (node.tri_count > 0) {
                // Leaf: test each triangle.
                for (int i = node.tri_start; i < node.tri_start + node.tri_count; ++i) {
                    int fi = tri_indices[static_cast<std::size_t>(i)];
                    double dsq;
                    auto cp = analysis::closest_point_on_triangle(
                        p, V.col(F(0, fi)), V.col(F(1, fi)), V.col(F(2, fi)), dsq);
                    if (dsq < best.distance_sq) {
                        best.distance_sq = dsq;
                        best.closest_point = cp;
                        best.triangle_index = fi;
                    }
                }
                continue;
            }

            // Internal: push children — farther child first so nearer is popped first.
            double d_left = (node.left >= 0 && node.left < n_nodes)
                ? nodes[static_cast<std::size_t>(node.left)].box.distance_sq(p)
                : std::numeric_limits<double>::max();
            double d_right = (node.right >= 0 && node.right < n_nodes)
                ? nodes[static_cast<std::size_t>(node.right)].box.distance_sq(p)
                : std::numeric_limits<double>::max();

            int first = (d_left <= d_right) ? node.left : node.right;
            int second = (d_left <= d_right) ? node.right : node.left;

            // Push farther child first (processed second).
            if (second >= 0 && second < n_nodes && top < MAX_STACK) stack[top++] = second;
            if (first >= 0 && first < n_nodes && top < MAX_STACK) stack[top++] = first;
        }

        return best;
    }
};

// ============================================================================
// Public API
// ============================================================================

AABBTree::AABBTree(const geometry::TriangleMesh& mesh) : impl_(std::make_unique<Impl>()) {
    impl_->build(mesh);
}

AABBTree::~AABBTree() = default;
AABBTree::AABBTree(AABBTree&&) noexcept = default;
AABBTree& AABBTree::operator=(AABBTree&&) noexcept = default;

SurfaceNNResult AABBTree::closest_point(const Eigen::Vector3d& query) const {
    return impl_->query(query);
}

} // namespace alignmesh::spatial
