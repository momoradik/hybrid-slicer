#pragma once

#include "alignmesh/geometry/point_cloud.h"

#include <Eigen/Core>

namespace alignmesh::spatial {

/// Deterministic voxel-grid downsampling.
///
/// Each voxel selects a single representative point using a deterministic
/// rule (the point closest to the voxel centre). The voxel grid origin is
/// fixed at (0,0,0) for reproducibility across runs.
///
/// NOTE: voxel downsampling is used to accelerate coarse registration stages
/// only. All analysis must re-refine on full-resolution data.
///
/// @param points  3xN point matrix.
/// @param voxel_size  edge length of each cubic voxel.
/// @return Indices (into the original point set) of the selected representatives.
std::vector<Eigen::Index> voxel_downsample(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    double voxel_size);

} // namespace alignmesh::spatial
