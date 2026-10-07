#include "alignmesh/spatial/kdtree.h"

#include <nanoflann.hpp>
#include <algorithm>
#include <stdexcept>

namespace alignmesh::spatial {

// nanoflann adaptor for a column-major 3xN Eigen matrix (double).
struct EigenCloudAdaptor {
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts;

    explicit EigenCloudAdaptor(const Eigen::Matrix<double, 3, Eigen::Dynamic>& p)
        : pts(p) {}

    std::size_t kdtree_get_point_count() const {
        return static_cast<std::size_t>(pts.cols());
    }
    double kdtree_get_pt(std::size_t idx, std::size_t dim) const {
        return pts(static_cast<Eigen::Index>(dim), static_cast<Eigen::Index>(idx));
    }
    template <class BBOX>
    bool kdtree_get_bbox(BBOX&) const { return false; }
};

using NanoIndex = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, EigenCloudAdaptor>,
    EigenCloudAdaptor, 3, Eigen::Index>;

struct KdTree::Impl {
    Eigen::Matrix<double, 3, Eigen::Dynamic> points;
    EigenCloudAdaptor adaptor;
    NanoIndex index;

    Impl(const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts)
        : points(pts),
          adaptor(points),
          index(3, adaptor, nanoflann::KDTreeSingleIndexAdaptorParams(10))
    {
        index.buildIndex();
    }
};

KdTree::KdTree(const Eigen::Matrix<double, 3, Eigen::Dynamic>& points)
    : impl_(std::make_unique<Impl>(points)) {}

KdTree::~KdTree() = default;
KdTree::KdTree(KdTree&&) noexcept = default;
KdTree& KdTree::operator=(KdTree&&) noexcept = default;

Eigen::Index KdTree::size() const {
    return impl_->points.cols();
}

NNResult KdTree::nearest(const geometry::Vec3& query) const {
    double pt[3] = {query(0), query(1), query(2)};
    Eigen::Index idx = 0;
    double dist_sq = 0;
    nanoflann::KNNResultSet<double, Eigen::Index> rs(1);
    rs.init(&idx, &dist_sq);
    impl_->index.findNeighbors(rs, pt);

    NNResult r;
    r.index = static_cast<int>(idx);
    r.distance_sq = dist_sq;
    return r;
}

void KdTree::knn(const geometry::Vec3& query, int k,
                 std::vector<int>& indices,
                 std::vector<double>& distances_sq) const {
    auto n = static_cast<std::size_t>(std::min(static_cast<Eigen::Index>(k), size()));
    std::vector<Eigen::Index> idx(n);
    std::vector<double> dsq(n);

    double pt[3] = {query(0), query(1), query(2)};
    nanoflann::KNNResultSet<double, Eigen::Index> rs(n);
    rs.init(idx.data(), dsq.data());
    impl_->index.findNeighbors(rs, pt);

    indices.resize(n);
    distances_sq.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        indices[i] = static_cast<int>(idx[i]);
        distances_sq[i] = dsq[i];
    }
}

int KdTree::radius_search(const geometry::Vec3& query, double radius,
                           std::vector<int>& indices,
                           std::vector<double>& distances_sq) const {
    double pt[3] = {query(0), query(1), query(2)};
    double radius_sq = radius * radius;

    std::vector<nanoflann::ResultItem<Eigen::Index, double>> matches;
    nanoflann::SearchParameters params;
    params.sorted = true;

    auto count = impl_->index.radiusSearch(pt, radius_sq, matches, params);

    indices.resize(static_cast<std::size_t>(count));
    distances_sq.resize(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i) {
        indices[i] = static_cast<int>(matches[i].first);
        distances_sq[i] = matches[i].second;
    }
    return static_cast<int>(count);
}

} // namespace alignmesh::spatial
