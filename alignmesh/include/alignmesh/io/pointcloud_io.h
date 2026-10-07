#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/io/immutable_source_store.h"

#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::io {

/// Result of importing a point cloud file (XYZ/PTS/ASC).
struct ImportedPointCloud {
    geometry::PointCloud cloud;
    SourceMetadata metadata;
    std::vector<std::string> warnings;
    std::string hash;
};

/// Parse XYZ/PTS/ASC text point cloud from raw bytes.
///
/// Supported formats:
///   - Whitespace or comma-delimited columns
///   - Lines starting with '#', '//', or non-numeric are skipped (comments/headers)
///   - PTS files: first line is point count (single integer) — auto-detected and skipped
///   - Minimum 3 columns (X Y Z). Columns 4-6 are normals if present.
///   - All coordinates parsed as double (IEEE-754 binary64).
///   - NaN/Inf values are rejected (fail-safe).
ImportedPointCloud import_pointcloud(const uint8_t* data, std::size_t size,
                                      const std::string& source_hash = {});

inline ImportedPointCloud import_pointcloud(const std::vector<uint8_t>& data,
                                             const std::string& source_hash = {}) {
    return import_pointcloud(data.data(), data.size(), source_hash);
}

/// Detect whether raw bytes look like a text point cloud (XYZ/PTS/ASC).
/// Checks first few non-comment lines for 3+ numeric columns.
bool looks_like_pointcloud(const uint8_t* data, std::size_t size);

} // namespace alignmesh::io
