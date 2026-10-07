#pragma once

#include "alignmesh/numerics/seeded_rng.h"

#include <Eigen/Core>
#include <vector>

namespace alignmesh::spatial {

/// Normal-space sampling (Rusinkiewicz & Levoy 2001).
///
/// Distributes samples uniformly in normal-direction space rather than
/// spatial position, ensuring good angular coverage of the surface.
/// Points are binned by their normal direction on a discretized sphere;
/// one point per bin is selected (deterministic: first encountered).
///
/// @param points   3xN points.
/// @param normals  3xN unit normals.
/// @param n_bins   number of bins along each angular axis (total bins ~ n_bins^2).
/// @return Indices of the selected points.
std::vector<Eigen::Index> normal_space_sampling(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
    int n_bins = 20);

/// Geometrically stable sampling (Gelfand et al. 2003).
///
/// Selects points that together constrain all 6 DOFs of the rigid
/// transformation. Points are scored by how much they contribute
/// to constraining under-determined directions (based on the point
/// cross-product with the normal, which forms the columns of the
/// constraint Jacobian).
///
/// @param points   3xN points.
/// @param normals  3xN unit normals.
/// @param n_select number of points to select.
/// @param rng      seeded RNG for tie-breaking.
/// @return Indices of the selected points.
std::vector<Eigen::Index> stable_sampling(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& normals,
    int n_select,
    numerics::SeededRng& rng);

} // namespace alignmesh::spatial
