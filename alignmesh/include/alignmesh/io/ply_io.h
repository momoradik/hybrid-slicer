#pragma once

#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/io/stl_io.h"

#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::io {

/// Parse PLY (ASCII or binary little-endian) from raw bytes.
/// Supports float and double vertex coordinates.
ImportedMesh import_ply(const uint8_t* data, std::size_t size,
                        const std::string& source_hash = {});

inline ImportedMesh import_ply(const std::vector<uint8_t>& data,
                               const std::string& source_hash = {}) {
    return import_ply(data.data(), data.size(), source_hash);
}

/// Export as binary little-endian PLY with double-precision coordinates.
ExportResult export_ply_binary_double(const geometry::TriangleMesh& mesh,
                                      const std::string& path);

} // namespace alignmesh::io
