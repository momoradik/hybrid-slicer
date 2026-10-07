#include "alignmesh/geometry/point_cloud.h"

#include <stdexcept>
#include <utility>

namespace alignmesh::geometry {

PointCloud::PointCloud(Eigen::Matrix<double, 3, Eigen::Dynamic> points)
    : points_(std::move(points)) {}

PointCloud::PointCloud(
        Eigen::Matrix<double, 3, Eigen::Dynamic> points,
        std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> normals,
        std::optional<std::vector<Mat3>> covariances,
        std::string source_hash)
    : points_(std::move(points)),
      normals_(std::move(normals)),
      covariances_(std::move(covariances)),
      source_hash_(std::move(source_hash)) {}

const Eigen::Matrix<double, 3, Eigen::Dynamic>& PointCloud::normals() const {
    if (!normals_) throw std::logic_error("PointCloud has no normals");
    return *normals_;
}

const std::vector<Mat3>& PointCloud::covariances() const {
    if (!covariances_) throw std::logic_error("PointCloud has no covariances");
    return *covariances_;
}

PointCloud PointCloud::transformed(const RigidTransform& T) const {
    const Mat3 R = T.rotation();

    // Points: p' = R*p + t
    auto new_points = T.apply_cloud(points_);

    // Normals: n' = R*n  (rotation only, no translation)
    std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> new_normals;
    if (normals_) {
        new_normals = R * (*normals_);
    }

    // Covariances: S' = R*S*R^T
    std::optional<std::vector<Mat3>> new_covariances;
    if (covariances_) {
        const Mat3 Rt = R.transpose();
        std::vector<Mat3> transformed_covs;
        transformed_covs.reserve(covariances_->size());
        for (const auto& cov : *covariances_) {
            transformed_covs.push_back(R * cov * Rt);
        }
        new_covariances = std::move(transformed_covs);
    }

    // Derived copy: NOT a source (empty source_hash).
    return PointCloud(std::move(new_points), std::move(new_normals),
                      std::move(new_covariances));
}

} // namespace alignmesh::geometry
