#include "alignmesh/io/pointcloud_io.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace alignmesh::io {

// Trim leading/trailing whitespace.
static std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// Check if a line is a comment or header (not data).
static bool is_comment_line(std::string_view line) {
    line = trim(line);
    if (line.empty()) return true;
    if (line.front() == '#' || line.front() == '!') return true;
    if (line.size() >= 2 && line[0] == '/' && line[1] == '/') return true;
    // Check if first char is a letter (header line like "x y z nx ny nz")
    if (std::isalpha(static_cast<unsigned char>(line.front()))) return true;
    return false;
}

// Parse doubles from a line. Supports whitespace and comma delimiters.
static std::vector<double> parse_line_doubles(std::string_view line) {
    std::vector<double> vals;
    // Replace commas with spaces for uniform parsing
    std::string buf(line);
    std::replace(buf.begin(), buf.end(), ',', ' ');

    const char* p = buf.data();
    const char* end = p + buf.size();
    while (p < end) {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p >= end) break;

        char* next = nullptr;
        double v = std::strtod(p, &next);
        if (next == p) break; // no parse
        vals.push_back(v);
        p = next;
    }
    return vals;
}

// Check if a line looks like it's just a single integer (PTS point count header).
static bool is_single_integer(std::string_view line) {
    line = trim(line);
    if (line.empty()) return false;
    for (char c : line) {
        if (!std::isdigit(static_cast<unsigned char>(c)) &&
            c != '-' && c != '+')
            return false;
    }
    return true;
}

bool looks_like_pointcloud(const uint8_t* data, std::size_t size) {
    if (size == 0) return false;

    // Quick reject: if it starts with "ply" or has binary STL signature, it's not a text cloud.
    if (size >= 4 && std::memcmp(data, "ply", 3) == 0) return false;

    // Check first ~10 non-comment lines for 3+ numeric columns.
    std::string_view text(reinterpret_cast<const char*>(data),
                          std::min(size, std::size_t(8192)));
    int data_lines = 0;
    std::size_t pos = 0;
    while (pos < text.size() && data_lines < 10) {
        auto nl = text.find('\n', pos);
        auto line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() : nl + 1;

        if (is_comment_line(line)) continue;
        if (is_single_integer(trim(line))) continue; // PTS header

        auto vals = parse_line_doubles(line);
        if (vals.size() >= 3) {
            data_lines++;
        } else {
            return false; // Non-numeric content that isn't a comment
        }
    }
    return data_lines >= 1;
}

ImportedPointCloud import_pointcloud(const uint8_t* data, std::size_t size,
                                      const std::string& source_hash) {
    if (size == 0)
        throw std::runtime_error("Empty point cloud file");

    std::string_view text(reinterpret_cast<const char*>(data), size);

    std::vector<double> xs, ys, zs;
    std::vector<double> nxs, nys, nzs;
    bool has_normals = false;
    bool first_data_line = true;
    int expected_cols = 0;
    bool skipped_pts_header = false;

    std::size_t pos = 0;
    std::size_t line_num = 0;
    std::vector<std::string> warnings;

    while (pos < text.size()) {
        auto nl = text.find('\n', pos);
        auto line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
        line_num++;

        if (is_comment_line(line)) continue;

        auto trimmed = trim(line);
        // PTS header: single integer on first data line = point count
        if (!skipped_pts_header && is_single_integer(trimmed)) {
            skipped_pts_header = true;
            continue;
        }
        skipped_pts_header = true;

        auto vals = parse_line_doubles(line);
        if (vals.size() < 3) {
            if (!trimmed.empty())
                warnings.push_back("Skipped line " + std::to_string(line_num) +
                                   ": fewer than 3 columns");
            continue;
        }

        // Fail-safe: reject NaN/Inf (CLAUDE.md invariant 3)
        bool bad = false;
        for (std::size_t i = 0; i < std::min(vals.size(), std::size_t(6)); ++i) {
            if (!std::isfinite(vals[i])) { bad = true; break; }
        }
        if (bad) {
            warnings.push_back("Rejected line " + std::to_string(line_num) +
                               ": NaN or Inf detected");
            continue;
        }

        if (first_data_line) {
            expected_cols = static_cast<int>(vals.size());
            has_normals = (expected_cols >= 6);
            first_data_line = false;
        }

        xs.push_back(vals[0]);
        ys.push_back(vals[1]);
        zs.push_back(vals[2]);

        if (has_normals && vals.size() >= 6) {
            nxs.push_back(vals[3]);
            nys.push_back(vals[4]);
            nzs.push_back(vals[5]);
        }
    }

    if (xs.empty())
        throw std::runtime_error("No valid points in point cloud file");

    // Build Eigen matrices
    Eigen::Index n = static_cast<Eigen::Index>(xs.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> points(3, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        points(0, i) = xs[static_cast<std::size_t>(i)];
        points(1, i) = ys[static_cast<std::size_t>(i)];
        points(2, i) = zs[static_cast<std::size_t>(i)];
    }

    std::optional<Eigen::Matrix<double, 3, Eigen::Dynamic>> normals;
    if (has_normals && static_cast<Eigen::Index>(nxs.size()) == n) {
        Eigen::Matrix<double, 3, Eigen::Dynamic> norm_mat(3, n);
        for (Eigen::Index i = 0; i < n; ++i) {
            norm_mat(0, i) = nxs[static_cast<std::size_t>(i)];
            norm_mat(1, i) = nys[static_cast<std::size_t>(i)];
            norm_mat(2, i) = nzs[static_cast<std::size_t>(i)];
        }
        normals = std::move(norm_mat);
    }

    // Metadata
    SourceMetadata meta;
    meta.format = "pointcloud-text";
    meta.coordinate_precision = "float64";
    meta.vertex_count = static_cast<std::size_t>(n);
    meta.triangle_count = 0;

    auto col_min = points.rowwise().minCoeff();
    auto col_max = points.rowwise().maxCoeff();
    meta.bbox_min = col_min;
    meta.bbox_max = col_max;

    if (warnings.size() > 100) {
        warnings.resize(100);
        warnings.push_back("(truncated — too many warnings)");
    }

    geometry::PointCloud cloud(std::move(points), std::move(normals),
                                std::nullopt, source_hash);

    return ImportedPointCloud{std::move(cloud), std::move(meta),
                               std::move(warnings), source_hash};
}

} // namespace alignmesh::io
