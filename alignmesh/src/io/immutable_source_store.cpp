#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/io/pointcloud_io.h"
#include "alignmesh/io/sha256.h"
#include "alignmesh/io/stl_io.h"
#include "alignmesh/io/ply_io.h"
#include "alignmesh/io/step_io.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace alignmesh::io {

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) throw std::runtime_error("Cannot open file: " + path);

    auto file_size = ifs.tellg();
    if (file_size <= 0) return {};

    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<std::size_t>(file_size));
    ifs.read(reinterpret_cast<char*>(data.data()), file_size);
    return data;
}

// Detect format from raw bytes.
enum class FileFormat { STL, PLY, STEP, UNKNOWN };

static FileFormat detect_format(const uint8_t* data, std::size_t size) {
    if (size >= 4 && std::memcmp(data, "ply", 3) == 0 &&
        (data[3] == '\n' || data[3] == '\r'))
        return FileFormat::PLY;

    // STEP: "ISO-10303-21;" header (checked before STL because some STEP
    // files could theoretically start with "solid" if the header is mangled).
    if (looks_like_step(data, size))
        return FileFormat::STEP;

    // Binary STL: check if size matches 80 + 4 + N*50
    if (size >= 84) {
        uint32_t n;
        std::memcpy(&n, data + 80, 4);
        std::size_t expected = 84 + static_cast<std::size_t>(n) * 50;
        if (size == expected) return FileFormat::STL;
    }

    // ASCII STL: starts with "solid"
    if (size >= 5 && std::memcmp(data, "solid", 5) == 0)
        return FileFormat::STL;

    return FileFormat::UNKNOWN;
}

ImportedMesh ImmutableSourceStore::import_file(const std::string& path) {
    auto bytes = read_file(path);
    if (bytes.empty())
        throw std::runtime_error("Empty file: " + path);

    std::string hash = sha256_hex(bytes);

    auto fmt = detect_format(bytes.data(), bytes.size());

    if (fmt == FileFormat::STEP) {
        // STEP files produce a CadReference, not a plain ImportedMesh.
        // For backward compat with callers that expect import_file() to
        // return ImportedMesh, we import via import_step and return the
        // display tessellation as the mesh. The CadReference is stored
        // separately and accessible via import_source().
        auto step_result = import_step(path, bytes, hash);
        if (!step_result.success) {
            std::string msg = "STEP import failed: ";
            for (auto& e : step_result.errors) msg += e + "; ";
            throw std::runtime_error(msg);
        }
        ImportedMesh result{
            std::move(step_result.display_mesh),
            step_result.metadata,
            step_result.warnings,
            hash
        };

        // Store the CadReference for later retrieval.
        cad_refs_[hash] = step_result.cad_reference;

        entries_[hash] = Entry{std::move(bytes), result.metadata};
        return result;
    }

    ImportedMesh result = [&]() -> ImportedMesh {
        switch (fmt) {
            case FileFormat::STL: return import_stl(bytes, hash);
            case FileFormat::PLY: return import_ply(bytes, hash);
            default: throw std::runtime_error("Unknown file format: " + path);
        }
    }();

    result.hash = hash;

    // Store original bytes + metadata (never mutated after this point).
    entries_[hash] = Entry{std::move(bytes), result.metadata};
    return result;
}

ImportedSource ImmutableSourceStore::import_source(const std::string& path) {
    auto bytes = read_file(path);
    if (bytes.empty())
        throw std::runtime_error("Empty file: " + path);

    std::string hash = sha256_hex(bytes);
    auto fmt = detect_format(bytes.data(), bytes.size());

    // STEP format: import B-rep, return display mesh as the source mesh.
    if (fmt == FileFormat::STEP) {
        auto step_result = import_step(path, bytes, hash);
        if (!step_result.success) {
            std::string msg = "STEP import failed: ";
            for (auto& e : step_result.errors) msg += e + "; ";
            throw std::runtime_error(msg);
        }
        ImportedMesh mesh_result{
            std::move(step_result.display_mesh),
            step_result.metadata,
            step_result.warnings,
            hash
        };

        cad_refs_[hash] = step_result.cad_reference;

        auto meta = mesh_result.metadata;
        auto warns = mesh_result.warnings;
        entries_[hash] = Entry{std::move(bytes), meta};
        return ImportedSource{std::move(mesh_result), std::move(meta),
                              std::move(warns), hash};
    }

    // Try mesh formats first (STL/PLY)
    if (fmt != FileFormat::UNKNOWN && fmt != FileFormat::STEP) {
        ImportedMesh mesh_result = [&]() -> ImportedMesh {
            switch (fmt) {
                case FileFormat::STL: return import_stl(bytes, hash);
                case FileFormat::PLY: return import_ply(bytes, hash);
                default: throw std::runtime_error("Unreachable");
            }
        }();
        mesh_result.hash = hash;
        auto meta = mesh_result.metadata;
        auto warns = mesh_result.warnings;
        entries_[hash] = Entry{std::move(bytes), meta};
        return ImportedSource{std::move(mesh_result), std::move(meta),
                              std::move(warns), hash};
    }

    // Try point cloud (XYZ/PTS/ASC)
    if (looks_like_pointcloud(bytes.data(), bytes.size())) {
        auto pc_result = import_pointcloud(bytes, hash);
        auto meta = pc_result.metadata;
        auto warns = pc_result.warnings;
        entries_[hash] = Entry{std::move(bytes), meta};
        return ImportedSource{std::move(pc_result.cloud), std::move(meta),
                              std::move(warns), hash};
    }

    throw std::runtime_error("Unknown file format: " + path);
}

bool ImmutableSourceStore::has(const std::string& hash) const {
    return entries_.count(hash) > 0;
}

const std::vector<uint8_t>& ImmutableSourceStore::original_bytes(
        const std::string& hash) const {
    auto it = entries_.find(hash);
    if (it == entries_.end())
        throw std::runtime_error("Source not found: " + hash);
    return it->second.bytes;
}

const SourceMetadata& ImmutableSourceStore::metadata(
        const std::string& hash) const {
    auto it = entries_.find(hash);
    if (it == entries_.end())
        throw std::runtime_error("Source not found: " + hash);
    return it->second.meta;
}

} // namespace alignmesh::io
