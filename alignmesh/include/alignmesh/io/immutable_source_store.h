#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace alignmesh::io {

/// Metadata detected from the imported file.
struct SourceMetadata {
    std::string format;                // "stl-binary", "stl-ascii", "ply-binary-le", "ply-ascii"
    std::string coordinate_precision;  // "float32", "float64"
    std::size_t vertex_count = 0;
    std::size_t triangle_count = 0;
    geometry::Vec3 bbox_min = geometry::Vec3::Zero();
    geometry::Vec3 bbox_max = geometry::Vec3::Zero();
    std::string units;                 // empty if not detected
};

/// Result of importing a mesh file.
struct ImportedMesh {
    geometry::TriangleMesh mesh;
    SourceMetadata metadata;
    std::vector<std::string> warnings;
    std::string hash;  // SHA-256 hex (set by ImmutableSourceStore::import_file)
};

/// Result of importing a file that could be mesh or point cloud.
/// Measured parts may be raw scanner point clouds (XYZ/PTS/ASC).
struct ImportedSource {
    std::variant<ImportedMesh, geometry::PointCloud> data;
    SourceMetadata metadata;
    std::vector<std::string> warnings;
    std::string hash;

    bool is_mesh() const { return std::holds_alternative<ImportedMesh>(data); }
    bool is_cloud() const { return std::holds_alternative<geometry::PointCloud>(data); }

    const ImportedMesh& as_mesh() const { return std::get<ImportedMesh>(data); }
    const geometry::PointCloud& as_cloud() const { return std::get<geometry::PointCloud>(data); }

    /// Get points regardless of type (mesh vertices or cloud points).
    Eigen::Matrix<double, 3, Eigen::Dynamic> points() const {
        if (is_mesh()) return as_mesh().mesh.vertices();
        return as_cloud().points();
    }
};

// ============================================================================
// ImmutableSourceStore
// ============================================================================
//
// On import, stores the original file bytes and a SHA-256 hash. The stored
// original is NEVER mutated. All geometry derives from a copy. This satisfies
// CLAUDE.md invariant 4 (IMMUTABLE SOURCE).
// ============================================================================

class ImmutableSourceStore {
public:
    /// Import a file: read bytes, compute SHA-256, parse, store original.
    /// The returned mesh has source_hash set to the SHA-256 of the file.
    ImportedMesh import_file(const std::string& path);

    /// Import a file that could be mesh (STL/PLY) or point cloud (XYZ/PTS/ASC).
    /// For measured parts that may come from scanners as raw point clouds.
    ImportedSource import_source(const std::string& path);

    /// Check if a source with this hash is stored.
    bool has(const std::string& hash) const;

    /// Get the original bytes for a stored source.
    const std::vector<uint8_t>& original_bytes(const std::string& hash) const;

    /// Get the metadata for a stored source.
    const SourceMetadata& metadata(const std::string& hash) const;

    /// Number of stored sources.
    std::size_t size() const { return entries_.size(); }

    /// Get the CadReference for a STEP import (null if not a STEP file).
    std::shared_ptr<geometry::ReferenceGeometry> cad_reference(
            const std::string& hash) const {
        auto it = cad_refs_.find(hash);
        return (it != cad_refs_.end()) ? it->second : nullptr;
    }

    /// Check if a source has a CAD B-rep reference (STEP import).
    bool has_cad_reference(const std::string& hash) const {
        return cad_refs_.count(hash) > 0;
    }

private:
    struct Entry {
        std::vector<uint8_t> bytes;
        SourceMetadata meta;
    };
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, std::shared_ptr<geometry::ReferenceGeometry>> cad_refs_;
};

/// Read a file into bytes. Throws on failure.
std::vector<uint8_t> read_file(const std::string& path);

} // namespace alignmesh::io
