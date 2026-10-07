#include "alignmesh/spatial/voxel_downsample.h"

#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace alignmesh::spatial {

std::vector<Eigen::Index> voxel_downsample(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        double voxel_size) {
    if (points.cols() == 0 || voxel_size <= 0) return {};

    double inv_voxel = 1.0 / voxel_size;

    // Voxel key = (ix, iy, iz) integer coordinates.
    // Origin is fixed at (0,0,0) for deterministic behaviour.
    using VoxelKey = std::tuple<int64_t, int64_t, int64_t>;

    // For each voxel, track the best point (closest to voxel centre).
    struct VoxelEntry {
        Eigen::Index best_idx = -1;
        double best_dist_sq = std::numeric_limits<double>::max();
    };
    std::map<VoxelKey, VoxelEntry> voxels;

    for (Eigen::Index i = 0; i < points.cols(); ++i) {
        double x = points(0, i);
        double y = points(1, i);
        double z = points(2, i);

        auto ix = static_cast<int64_t>(std::floor(x * inv_voxel));
        auto iy = static_cast<int64_t>(std::floor(y * inv_voxel));
        auto iz = static_cast<int64_t>(std::floor(z * inv_voxel));

        VoxelKey key{ix, iy, iz};

        // Voxel centre
        double cx = (ix + 0.5) * voxel_size;
        double cy = (iy + 0.5) * voxel_size;
        double cz = (iz + 0.5) * voxel_size;
        double dx = x - cx, dy = y - cy, dz = z - cz;
        double dist_sq = dx * dx + dy * dy + dz * dz;

        auto& entry = voxels[key];
        if (dist_sq < entry.best_dist_sq) {
            entry.best_dist_sq = dist_sq;
            entry.best_idx = i;
        }
    }

    std::vector<Eigen::Index> result;
    result.reserve(voxels.size());
    for (auto& [key, entry] : voxels) {
        result.push_back(entry.best_idx);
    }

    return result;
}

} // namespace alignmesh::spatial
