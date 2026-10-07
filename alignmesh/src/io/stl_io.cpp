#define _CRT_SECURE_NO_WARNINGS
#include "alignmesh/io/stl_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace alignmesh::io {

// Maximum triangles we'll attempt to allocate (prevents OOM on corrupt files).
static constexpr uint32_t kMaxTriangles = 100'000'000;

// ---- vertex merging helpers ------------------------------------------------

struct Float3 { float x, y, z; };

struct Float3Hash {
    std::size_t operator()(const Float3& f) const {
        uint32_t a, b, c;
        std::memcpy(&a, &f.x, 4);
        std::memcpy(&b, &f.y, 4);
        std::memcpy(&c, &f.z, 4);
        // FNV-1a inspired combine
        std::size_t h = 2166136261u;
        h = (h ^ a) * 16777619u;
        h = (h ^ b) * 16777619u;
        h = (h ^ c) * 16777619u;
        return h;
    }
};

struct Float3Eq {
    bool operator()(const Float3& a, const Float3& b) const {
        return std::memcmp(&a, &b, sizeof(Float3)) == 0;
    }
};

static int add_or_get_vertex(
    std::unordered_map<Float3, int, Float3Hash, Float3Eq>& map,
    std::vector<geometry::Vec3>& verts,
    float fx, float fy, float fz)
{
    Float3 key{fx, fy, fz};
    auto it = map.find(key);
    if (it != map.end()) return it->second;
    int idx = static_cast<int>(verts.size());
    verts.emplace_back(static_cast<double>(fx),
                       static_cast<double>(fy),
                       static_cast<double>(fz));
    map[key] = idx;
    return idx;
}

// ---- little-endian readers ------------------------------------------------

static float read_f32_le(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

static uint32_t read_u32_le(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

// ---- binary STL import ----------------------------------------------------

static ImportedMesh import_stl_binary(const uint8_t* data, std::size_t size,
                                       const std::string& source_hash) {
    if (size < 84)
        throw std::runtime_error("STL binary: file too small (< 84 bytes)");

    uint32_t num_tri = read_u32_le(data + 80);
    std::size_t expected = 84 + static_cast<std::size_t>(num_tri) * 50;
    if (size < expected)
        throw std::runtime_error("STL binary: truncated (claims " +
            std::to_string(num_tri) + " triangles but file too short)");
    if (num_tri > kMaxTriangles)
        throw std::runtime_error("STL binary: triangle count exceeds limit (" +
            std::to_string(num_tri) + " > " + std::to_string(kMaxTriangles) + ")");

    std::unordered_map<Float3, int, Float3Hash, Float3Eq> vmap;
    std::vector<geometry::Vec3> verts;
    verts.reserve(num_tri);  // rough estimate

    Eigen::Matrix<int, 3, Eigen::Dynamic> tris(3, static_cast<Eigen::Index>(num_tri));

    std::vector<std::string> warnings;
    bool has_nan = false;

    for (uint32_t t = 0; t < num_tri; ++t) {
        const uint8_t* rec = data + 84 + static_cast<std::size_t>(t) * 50;
        // Skip normal (12 bytes), read 3 vertices (36 bytes)
        for (int v = 0; v < 3; ++v) {
            const uint8_t* vp = rec + 12 + v * 12;
            float fx = read_f32_le(vp);
            float fy = read_f32_le(vp + 4);
            float fz = read_f32_le(vp + 8);

            if (std::isnan(fx) || std::isnan(fy) || std::isnan(fz) ||
                std::isinf(fx) || std::isinf(fy) || std::isinf(fz)) {
                has_nan = true;
            }

            tris(v, static_cast<Eigen::Index>(t)) =
                add_or_get_vertex(vmap, verts, fx, fy, fz);
        }
    }

    if (has_nan)
        warnings.push_back("STL contains NaN or Inf coordinates");

    // Build Eigen vertex matrix
    Eigen::Matrix<double, 3, Eigen::Dynamic> vert_mat(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i) {
        vert_mat.col(static_cast<Eigen::Index>(i)) = verts[i];
    }

    // Bounding box
    geometry::Vec3 bbox_min = vert_mat.rowwise().minCoeff();
    geometry::Vec3 bbox_max = vert_mat.rowwise().maxCoeff();

    SourceMetadata meta;
    meta.format = "stl-binary";
    meta.coordinate_precision = "float32";
    meta.vertex_count = verts.size();
    meta.triangle_count = num_tri;
    meta.bbox_min = bbox_min;
    meta.bbox_max = bbox_max;

    warnings.insert(warnings.begin(),
        "STL coordinates are float32 — downstream precision-tier logic should account for this");

    auto mesh = geometry::TriangleMesh(std::move(vert_mat), std::move(tris), source_hash);
    return ImportedMesh{std::move(mesh), std::move(meta), std::move(warnings)};
}

// ---- ASCII STL import -----------------------------------------------------

static ImportedMesh import_stl_ascii(const uint8_t* data, std::size_t size,
                                      const std::string& source_hash) {
    std::string text(reinterpret_cast<const char*>(data), size);
    std::istringstream iss(text);

    std::unordered_map<Float3, int, Float3Hash, Float3Eq> vmap;
    std::vector<geometry::Vec3> verts;
    std::vector<Eigen::Vector3i> tri_list;

    std::string line;
    bool has_nan = false;

    while (std::getline(iss, line)) {
        // Trim leading whitespace
        auto pos = line.find_first_not_of(" \t\r");
        if (pos == std::string::npos) continue;
        line = line.substr(pos);

        if (line.rfind("vertex ", 0) == 0) {
            float fx, fy, fz;
            if (std::sscanf(line.c_str(), "vertex %f %f %f", &fx, &fy, &fz) != 3)
                throw std::runtime_error("STL ASCII: malformed vertex line");

            if (std::isnan(fx) || std::isnan(fy) || std::isnan(fz) ||
                std::isinf(fx) || std::isinf(fy) || std::isinf(fz))
                has_nan = true;

            int idx = add_or_get_vertex(vmap, verts, fx, fy, fz);

            // Accumulate into current triangle
            if (tri_list.empty() || tri_list.back()(0) != -1 &&
                tri_list.back()(1) != -1 && tri_list.back()(2) != -1) {
                tri_list.push_back(Eigen::Vector3i(-1, -1, -1));
            }
            auto& tri = tri_list.back();
            if (tri(0) == -1) tri(0) = idx;
            else if (tri(1) == -1) tri(1) = idx;
            else tri(2) = idx;
        } else if (line.rfind("outer loop", 0) == 0) {
            tri_list.push_back(Eigen::Vector3i(-1, -1, -1));
        }
    }

    // Remove incomplete triangles
    tri_list.erase(
        std::remove_if(tri_list.begin(), tri_list.end(),
            [](const Eigen::Vector3i& t) { return t(0) < 0 || t(1) < 0 || t(2) < 0; }),
        tri_list.end());

    if (tri_list.size() > kMaxTriangles)
        throw std::runtime_error("STL ASCII: triangle count exceeds limit");

    Eigen::Matrix<double, 3, Eigen::Dynamic> vert_mat(3, static_cast<Eigen::Index>(verts.size()));
    for (std::size_t i = 0; i < verts.size(); ++i)
        vert_mat.col(static_cast<Eigen::Index>(i)) = verts[i];

    Eigen::Matrix<int, 3, Eigen::Dynamic> tris(3, static_cast<Eigen::Index>(tri_list.size()));
    for (std::size_t i = 0; i < tri_list.size(); ++i)
        tris.col(static_cast<Eigen::Index>(i)) = tri_list[i];

    geometry::Vec3 bbox_min = vert_mat.cols() > 0 ?
        geometry::Vec3(vert_mat.rowwise().minCoeff()) : geometry::Vec3::Zero();
    geometry::Vec3 bbox_max = vert_mat.cols() > 0 ?
        geometry::Vec3(vert_mat.rowwise().maxCoeff()) : geometry::Vec3::Zero();

    std::vector<std::string> warnings;
    warnings.push_back("STL coordinates are float32 — downstream precision-tier logic should account for this");
    if (has_nan) warnings.push_back("STL contains NaN or Inf coordinates");

    SourceMetadata meta;
    meta.format = "stl-ascii";
    meta.coordinate_precision = "float32";
    meta.vertex_count = verts.size();
    meta.triangle_count = tri_list.size();
    meta.bbox_min = bbox_min;
    meta.bbox_max = bbox_max;

    auto mesh = geometry::TriangleMesh(std::move(vert_mat), std::move(tris), source_hash);
    return ImportedMesh{std::move(mesh), std::move(meta), std::move(warnings)};
}

// ---- format detection + dispatch ------------------------------------------

ImportedMesh import_stl(const uint8_t* data, std::size_t size,
                        const std::string& source_hash) {
    if (size == 0)
        throw std::runtime_error("STL: empty file");

    if (size >= 84) {
        uint32_t num_tri = read_u32_le(data + 80);
        std::size_t expected = 84 + static_cast<std::size_t>(num_tri) * 50;

        // Exact size match -> binary STL.
        if (size == expected) {
            return import_stl_binary(data, size, source_hash);
        }

        // Does NOT start with "solid" and has a non-zero triangle count ->
        // likely a truncated or corrupt binary STL. Let the binary importer
        // throw on the size mismatch rather than silently parsing as ASCII.
        bool starts_solid = (size >= 5 &&
                             std::memcmp(data, "solid", 5) == 0);
        if (!starts_solid && num_tri > 0) {
            return import_stl_binary(data, size, source_hash);
        }
    }

    // Fall back to ASCII
    return import_stl_ascii(data, size, source_hash);
}

// ---- binary STL export ----------------------------------------------------

ExportResult export_stl_binary(const geometry::TriangleMesh& mesh,
                               const std::string& path) {
    ExportResult result;
    result.warnings.push_back(
        "STL export converts double to float32 — precision loss may occur");

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        result.success = false;
        result.warnings.push_back("Cannot open output file: " + path);
        return result;
    }

    // 80-byte header
    char header[80] = {};
    const char tag[] = "alignmesh binary STL";
    std::memcpy(header, tag, sizeof(tag) - 1);
    ofs.write(header, 80);

    // Triangle count
    auto num_tri = static_cast<uint32_t>(mesh.num_triangles());
    ofs.write(reinterpret_cast<const char*>(&num_tri), 4);

    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();

    for (Eigen::Index t = 0; t < mesh.num_triangles(); ++t) {
        geometry::Vec3 v0 = verts.col(tris(0, t));
        geometry::Vec3 v1 = verts.col(tris(1, t));
        geometry::Vec3 v2 = verts.col(tris(2, t));
        geometry::Vec3 normal = (v1 - v0).cross(v2 - v0);
        double len = normal.norm();
        if (len > 0) normal /= len;

        float buf[12];
        buf[0]  = static_cast<float>(normal(0));
        buf[1]  = static_cast<float>(normal(1));
        buf[2]  = static_cast<float>(normal(2));
        buf[3]  = static_cast<float>(v0(0));
        buf[4]  = static_cast<float>(v0(1));
        buf[5]  = static_cast<float>(v0(2));
        buf[6]  = static_cast<float>(v1(0));
        buf[7]  = static_cast<float>(v1(1));
        buf[8]  = static_cast<float>(v1(2));
        buf[9]  = static_cast<float>(v2(0));
        buf[10] = static_cast<float>(v2(1));
        buf[11] = static_cast<float>(v2(2));
        ofs.write(reinterpret_cast<const char*>(buf), 48);

        uint16_t attr = 0;
        ofs.write(reinterpret_cast<const char*>(&attr), 2);
    }

    result.success = true;
    return result;
}

} // namespace alignmesh::io
