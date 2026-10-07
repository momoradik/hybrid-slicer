#include "alignmesh/io/ply_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace alignmesh::io {

// ---- PLY type sizes -------------------------------------------------------

enum class PlyType { CHAR, UCHAR, SHORT, USHORT, INT, UINT, FLOAT, DOUBLE };

static std::size_t ply_type_size(PlyType t) {
    switch (t) {
        case PlyType::CHAR: case PlyType::UCHAR: return 1;
        case PlyType::SHORT: case PlyType::USHORT: return 2;
        case PlyType::INT: case PlyType::UINT: case PlyType::FLOAT: return 4;
        case PlyType::DOUBLE: return 8;
    }
    return 0;
}

static PlyType parse_ply_type(const std::string& s) {
    if (s == "char" || s == "int8") return PlyType::CHAR;
    if (s == "uchar" || s == "uint8") return PlyType::UCHAR;
    if (s == "short" || s == "int16") return PlyType::SHORT;
    if (s == "ushort" || s == "uint16") return PlyType::USHORT;
    if (s == "int" || s == "int32") return PlyType::INT;
    if (s == "uint" || s == "uint32") return PlyType::UINT;
    if (s == "float" || s == "float32") return PlyType::FLOAT;
    if (s == "double" || s == "float64") return PlyType::DOUBLE;
    throw std::runtime_error("PLY: unknown type '" + s + "'");
}

struct PlyProperty {
    std::string name;
    PlyType type = PlyType::FLOAT;
    bool is_list = false;
    PlyType list_count_type = PlyType::UCHAR;
    PlyType list_elem_type = PlyType::INT;
};

struct PlyElement {
    std::string name;
    std::size_t count = 0;
    std::vector<PlyProperty> properties;

    std::size_t scalar_stride() const {
        std::size_t s = 0;
        for (auto& p : properties) {
            if (!p.is_list) s += ply_type_size(p.type);
        }
        return s;
    }
};

// ---- binary readers -------------------------------------------------------

static double read_ply_scalar(const uint8_t* p, PlyType t) {
    switch (t) {
        case PlyType::FLOAT: { float v; std::memcpy(&v, p, 4); return v; }
        case PlyType::DOUBLE: { double v; std::memcpy(&v, p, 8); return v; }
        case PlyType::INT: { int32_t v; std::memcpy(&v, p, 4); return v; }
        case PlyType::UINT: { uint32_t v; std::memcpy(&v, p, 4); return v; }
        case PlyType::SHORT: { int16_t v; std::memcpy(&v, p, 2); return v; }
        case PlyType::USHORT: { uint16_t v; std::memcpy(&v, p, 2); return v; }
        case PlyType::CHAR: { int8_t v; std::memcpy(&v, p, 1); return v; }
        case PlyType::UCHAR: { uint8_t v = *p; return v; }
    }
    return 0;
}

static int read_ply_int(const uint8_t* p, PlyType t) {
    return static_cast<int>(read_ply_scalar(p, t));
}

// ---- header parser --------------------------------------------------------

struct PlyHeader {
    enum Format { ASCII, BINARY_LE, BINARY_BE } format = ASCII;
    std::vector<PlyElement> elements;
    std::size_t header_bytes = 0;  // byte offset of data start
};

static PlyHeader parse_ply_header(const uint8_t* data, std::size_t size) {
    // Find end_header
    std::string text(reinterpret_cast<const char*>(data),
                     std::min(size, std::size_t(65536)));
    auto end_pos = text.find("end_header");
    if (end_pos == std::string::npos)
        throw std::runtime_error("PLY: missing end_header");

    // Find the newline after end_header
    auto data_start = text.find('\n', end_pos);
    if (data_start == std::string::npos)
        throw std::runtime_error("PLY: malformed end_header");
    ++data_start;

    PlyHeader hdr;
    hdr.header_bytes = data_start;

    std::istringstream iss(text.substr(0, end_pos));
    std::string line;

    // First line must be "ply"
    std::getline(iss, line);
    if (line.find("ply") == std::string::npos)
        throw std::runtime_error("PLY: missing magic");

    PlyElement* cur_elem = nullptr;

    while (std::getline(iss, line)) {
        // strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string token;
        ls >> token;

        if (token == "format") {
            std::string fmt;
            ls >> fmt;
            if (fmt == "ascii") hdr.format = PlyHeader::ASCII;
            else if (fmt == "binary_little_endian") hdr.format = PlyHeader::BINARY_LE;
            else if (fmt == "binary_big_endian") hdr.format = PlyHeader::BINARY_BE;
            else throw std::runtime_error("PLY: unsupported format '" + fmt + "'");
        } else if (token == "element") {
            PlyElement elem;
            ls >> elem.name >> elem.count;
            hdr.elements.push_back(std::move(elem));
            cur_elem = &hdr.elements.back();
        } else if (token == "property" && cur_elem) {
            std::string next;
            ls >> next;
            if (next == "list") {
                PlyProperty prop;
                prop.is_list = true;
                std::string ct, et;
                ls >> ct >> et >> prop.name;
                prop.list_count_type = parse_ply_type(ct);
                prop.list_elem_type = parse_ply_type(et);
                cur_elem->properties.push_back(std::move(prop));
            } else {
                PlyProperty prop;
                prop.type = parse_ply_type(next);
                ls >> prop.name;
                cur_elem->properties.push_back(std::move(prop));
            }
        }
        // comment and other lines are ignored
    }

    return hdr;
}

// ---- PLY import -----------------------------------------------------------

ImportedMesh import_ply(const uint8_t* data, std::size_t size,
                        const std::string& source_hash) {
    if (size < 4)
        throw std::runtime_error("PLY: file too small");
    if (std::memcmp(data, "ply", 3) != 0)
        throw std::runtime_error("PLY: missing magic");

    auto hdr = parse_ply_header(data, size);

    if (hdr.format == PlyHeader::BINARY_BE)
        throw std::runtime_error("PLY: big-endian not supported");

    // Find vertex and face elements
    const PlyElement* vert_elem = nullptr;
    const PlyElement* face_elem = nullptr;
    for (auto& e : hdr.elements) {
        if (e.name == "vertex") vert_elem = &e;
        if (e.name == "face") face_elem = &e;
    }
    if (!vert_elem)
        throw std::runtime_error("PLY: no vertex element");

    // Find x, y, z property indices and types in vertex element
    int xyz_idx[3] = {-1, -1, -1};
    PlyType xyz_type[3] = {PlyType::FLOAT, PlyType::FLOAT, PlyType::FLOAT};
    const char* xyz_names[3] = {"x", "y", "z"};
    for (int c = 0; c < 3; ++c) {
        for (int p = 0; p < static_cast<int>(vert_elem->properties.size()); ++p) {
            if (vert_elem->properties[static_cast<size_t>(p)].name == xyz_names[c]) {
                xyz_idx[c] = p;
                xyz_type[c] = vert_elem->properties[static_cast<size_t>(p)].type;
            }
        }
        if (xyz_idx[c] < 0)
            throw std::runtime_error(std::string("PLY: missing vertex property '") +
                                     xyz_names[c] + "'");
    }

    bool is_double = (xyz_type[0] == PlyType::DOUBLE);
    std::string coord_prec = is_double ? "float64" : "float32";

    auto nv = static_cast<Eigen::Index>(vert_elem->count);
    Eigen::Matrix<double, 3, Eigen::Dynamic> verts(3, nv);
    bool has_nan = false;

    const uint8_t* ptr = data + hdr.header_bytes;
    const uint8_t* end = data + size;

    // Compute per-vertex byte offsets for binary
    std::vector<std::size_t> vert_prop_offsets;
    std::size_t vert_stride = 0;
    for (auto& p : vert_elem->properties) {
        vert_prop_offsets.push_back(vert_stride);
        vert_stride += ply_type_size(p.type);
    }

    if (hdr.format == PlyHeader::BINARY_LE) {
        for (Eigen::Index i = 0; i < nv; ++i) {
            if (ptr + vert_stride > end)
                throw std::runtime_error("PLY binary: truncated vertex data");
            for (int c = 0; c < 3; ++c) {
                double val = read_ply_scalar(
                    ptr + vert_prop_offsets[static_cast<size_t>(xyz_idx[c])],
                    xyz_type[c]);
                verts(c, i) = val;
                if (std::isnan(val) || std::isinf(val)) has_nan = true;
            }
            ptr += vert_stride;
        }
    } else {
        // ASCII
        std::string text(reinterpret_cast<const char*>(data + hdr.header_bytes),
                         size - hdr.header_bytes);
        std::istringstream iss(text);

        // Read vertices element by element
        // We need to read ALL properties per vertex to advance the stream
        std::size_t elem_idx = 0;
        for (auto& elem : hdr.elements) {
            for (std::size_t ei = 0; ei < elem.count; ++ei) {
                if (&elem == vert_elem) {
                    double prop_vals[64] = {};
                    for (std::size_t pi = 0; pi < elem.properties.size(); ++pi) {
                        if (elem.properties[pi].is_list) {
                            int cnt; iss >> cnt;
                            for (int li = 0; li < cnt; ++li) { int dummy; iss >> dummy; }
                        } else {
                            iss >> prop_vals[pi];
                        }
                    }
                    auto vi = static_cast<Eigen::Index>(ei);
                    for (int c = 0; c < 3; ++c) {
                        verts(c, vi) = prop_vals[static_cast<size_t>(xyz_idx[c])];
                        if (std::isnan(verts(c, vi)) || std::isinf(verts(c, vi)))
                            has_nan = true;
                    }
                } else if (&elem == face_elem) {
                    // Will read faces in a second pass below
                    // Skip for now
                    for (auto& p : elem.properties) {
                        if (p.is_list) {
                            int cnt; iss >> cnt;
                            for (int li = 0; li < cnt; ++li) { int dummy; iss >> dummy; }
                        } else {
                            double dummy; iss >> dummy;
                        }
                    }
                } else {
                    // Skip unknown element
                    for (auto& p : elem.properties) {
                        if (p.is_list) {
                            int cnt; iss >> cnt;
                            for (int li = 0; li < cnt; ++li) { int dummy; iss >> dummy; }
                        } else {
                            double dummy; iss >> dummy;
                        }
                    }
                }
            }
            ++elem_idx;
        }
        // Reset for face reading -- actually we need a cleaner approach.
        // Let me re-parse from the beginning of data section.
        ptr = data + hdr.header_bytes;  // not used for ASCII face reading below
    }

    // Read faces
    Eigen::Index nf = face_elem ? static_cast<Eigen::Index>(face_elem->count) : 0;
    Eigen::Matrix<int, 3, Eigen::Dynamic> tris(3, nf);

    if (face_elem && nf > 0) {
        if (hdr.format == PlyHeader::BINARY_LE) {
            // ptr is already past vertex data
            for (Eigen::Index i = 0; i < nf; ++i) {
                // Read properties; find the list property (vertex_indices)
                for (auto& p : face_elem->properties) {
                    if (p.is_list) {
                        if (ptr + ply_type_size(p.list_count_type) > end)
                            throw std::runtime_error("PLY binary: truncated face data");
                        int count = read_ply_int(ptr, p.list_count_type);
                        ptr += ply_type_size(p.list_count_type);
                        if (count != 3)
                            throw std::runtime_error("PLY: non-triangle face (count=" +
                                std::to_string(count) + ")");
                        for (int v = 0; v < 3; ++v) {
                            if (ptr + ply_type_size(p.list_elem_type) > end)
                                throw std::runtime_error("PLY binary: truncated face indices");
                            tris(v, i) = read_ply_int(ptr, p.list_elem_type);
                            ptr += ply_type_size(p.list_elem_type);
                        }
                    } else {
                        ptr += ply_type_size(p.type);
                    }
                }
            }
        } else {
            // ASCII face reading - reparse
            std::string text(reinterpret_cast<const char*>(data + hdr.header_bytes),
                             size - hdr.header_bytes);
            std::istringstream iss(text);

            // Skip all elements before face_elem
            for (auto& elem : hdr.elements) {
                if (&elem == face_elem) break;
                for (std::size_t ei = 0; ei < elem.count; ++ei) {
                    for (auto& p : elem.properties) {
                        if (p.is_list) {
                            int cnt; iss >> cnt;
                            for (int li = 0; li < cnt; ++li) { int dummy; iss >> dummy; }
                        } else {
                            double dummy; iss >> dummy;
                        }
                    }
                }
            }
            // Read faces
            for (Eigen::Index i = 0; i < nf; ++i) {
                for (auto& p : face_elem->properties) {
                    if (p.is_list) {
                        int count; iss >> count;
                        if (count != 3)
                            throw std::runtime_error("PLY: non-triangle face");
                        for (int v = 0; v < 3; ++v) {
                            iss >> tris(v, i);
                        }
                    } else {
                        double dummy; iss >> dummy;
                    }
                }
            }
        }
    }

    geometry::Vec3 bbox_min = nv > 0 ?
        geometry::Vec3(verts.rowwise().minCoeff()) : geometry::Vec3::Zero();
    geometry::Vec3 bbox_max = nv > 0 ?
        geometry::Vec3(verts.rowwise().maxCoeff()) : geometry::Vec3::Zero();

    std::vector<std::string> warnings;
    if (has_nan) warnings.push_back("PLY contains NaN or Inf coordinates");

    SourceMetadata meta;
    meta.format = (hdr.format == PlyHeader::ASCII) ? "ply-ascii" : "ply-binary-le";
    meta.coordinate_precision = coord_prec;
    meta.vertex_count = static_cast<std::size_t>(nv);
    meta.triangle_count = static_cast<std::size_t>(nf);
    meta.bbox_min = bbox_min;
    meta.bbox_max = bbox_max;

    auto mesh = geometry::TriangleMesh(std::move(verts), std::move(tris), source_hash);
    return ImportedMesh{std::move(mesh), std::move(meta), std::move(warnings)};
}

// ---- PLY binary double export ---------------------------------------------

ExportResult export_ply_binary_double(const geometry::TriangleMesh& mesh,
                                       const std::string& path) {
    ExportResult result;

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        result.warnings.push_back("Cannot open output file: " + path);
        return result;
    }

    // Header
    std::ostringstream hdr;
    hdr << "ply\n";
    hdr << "format binary_little_endian 1.0\n";
    hdr << "element vertex " << mesh.num_vertices() << "\n";
    hdr << "property double x\n";
    hdr << "property double y\n";
    hdr << "property double z\n";
    hdr << "element face " << mesh.num_triangles() << "\n";
    hdr << "property list uchar int vertex_indices\n";
    hdr << "end_header\n";

    std::string header = hdr.str();
    ofs.write(header.data(), static_cast<std::streamsize>(header.size()));

    // Vertex data
    for (Eigen::Index i = 0; i < mesh.num_vertices(); ++i) {
        double xyz[3] = {mesh.vertices()(0, i),
                         mesh.vertices()(1, i),
                         mesh.vertices()(2, i)};
        ofs.write(reinterpret_cast<const char*>(xyz), 24);
    }

    // Face data
    for (Eigen::Index i = 0; i < mesh.num_triangles(); ++i) {
        uint8_t count = 3;
        ofs.write(reinterpret_cast<const char*>(&count), 1);
        int32_t idx[3] = {mesh.triangles()(0, i),
                          mesh.triangles()(1, i),
                          mesh.triangles()(2, i)};
        ofs.write(reinterpret_cast<const char*>(idx), 12);
    }

    result.success = true;
    return result;
}

} // namespace alignmesh::io
