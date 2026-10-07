#pragma once

#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/io/immutable_source_store.h"

#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::io {

/// Parse binary or ASCII STL from raw bytes.
/// STL uses float32 coordinates — this is recorded in metadata.coordinate_precision.
/// Vertices are merged by exact float32 bit-equality.
ImportedMesh import_stl(const uint8_t* data, std::size_t size,
                        const std::string& source_hash = {});

inline ImportedMesh import_stl(const std::vector<uint8_t>& data,
                               const std::string& source_hash = {}) {
    return import_stl(data.data(), data.size(), source_hash);
}

struct ExportResult {
    bool success = false;
    std::vector<std::string> warnings;
};

/// Export as binary STL (float32 — precision loss from double is recorded).
ExportResult export_stl_binary(const geometry::TriangleMesh& mesh,
                               const std::string& path);

} // namespace alignmesh::io
