#include "alignmesh/cad/analytic_surface.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>

namespace alignmesh::cad {

using namespace geometry;
static constexpr double kPi = std::numbers::pi;

// ============================================================================
// PlaneS
// ============================================================================

PlaneS::PlaneS(const Vec3& origin, const Vec3& normal)
    : origin_(origin), normal_(normal.normalized()) {
    // Build orthonormal frame.
    Vec3 arbitrary = (std::abs(normal_(0)) < 0.9) ? Vec3::UnitX() : Vec3::UnitY();
    u_axis_ = normal_.cross(arbitrary).normalized();
    v_axis_ = normal_.cross(u_axis_);
}

SurfaceQueryResult PlaneS::closest_point(const Vec3& query) const {
    SurfaceQueryResult r;
    double d = normal_.dot(query - origin_);
    r.closest_point = query - d * normal_;
    r.distance = std::abs(d);
    r.signed_distance = d;
    r.surface_normal = normal_;
    r.u = u_axis_.dot(r.closest_point - origin_);
    r.v = v_axis_.dot(r.closest_point - origin_);
    return r;
}

Vec3 PlaneS::evaluate(double u, double v) const {
    return origin_ + u * u_axis_ + v * v_axis_;
}

Vec3 PlaneS::normal_at(double, double) const {
    return normal_;
}

// ============================================================================
// CylinderS
// ============================================================================

CylinderS::CylinderS(const Vec3& axis_origin, const Vec3& axis_dir, double radius)
    : origin_(axis_origin), axis_(axis_dir.normalized()), radius_(radius) {
    Vec3 arbitrary = (std::abs(axis_(0)) < 0.9) ? Vec3::UnitX() : Vec3::UnitY();
    u_axis_ = axis_.cross(arbitrary).normalized();
    v_axis_ = axis_.cross(u_axis_);
}

SurfaceQueryResult CylinderS::closest_point(const Vec3& query) const {
    SurfaceQueryResult r;
    Vec3 d = query - origin_;
    double along_axis = axis_.dot(d);
    Vec3 radial = d - along_axis * axis_;
    double radial_dist = radial.norm();

    if (radial_dist < 1e-15) {
        // Point is on the cylinder axis → closest point is at radius in any direction.
        r.closest_point = origin_ + along_axis * axis_ + radius_ * u_axis_;
        r.distance = radius_;
        r.signed_distance = -radius_;  // inside
        r.surface_normal = u_axis_;
    } else {
        Vec3 radial_dir = radial / radial_dist;
        r.closest_point = origin_ + along_axis * axis_ + radius_ * radial_dir;
        r.distance = std::abs(radial_dist - radius_);
        r.signed_distance = radial_dist - radius_;  // + = outside, - = inside
        r.surface_normal = radial_dir;
    }

    r.u = std::atan2(r.surface_normal.dot(v_axis_), r.surface_normal.dot(u_axis_));
    r.v = along_axis;
    return r;
}

Vec3 CylinderS::evaluate(double u, double v) const {
    return origin_ + v * axis_ + radius_ * (std::cos(u) * u_axis_ + std::sin(u) * v_axis_);
}

Vec3 CylinderS::normal_at(double u, double) const {
    return std::cos(u) * u_axis_ + std::sin(u) * v_axis_;
}

// ============================================================================
// SphereS
// ============================================================================

SphereS::SphereS(const Vec3& centre, double radius)
    : centre_(centre), radius_(radius) {}

SurfaceQueryResult SphereS::closest_point(const Vec3& query) const {
    SurfaceQueryResult r;
    Vec3 d = query - centre_;
    double dist = d.norm();

    if (dist < 1e-15) {
        // At the centre: closest point is on the surface in the +Z direction.
        r.closest_point = centre_ + Vec3(0, 0, radius_);
        r.distance = radius_;
        r.signed_distance = -radius_;
        r.surface_normal = Vec3(0, 0, 1);
    } else {
        Vec3 dir = d / dist;
        r.closest_point = centre_ + radius_ * dir;
        r.distance = std::abs(dist - radius_);
        r.signed_distance = dist - radius_;
        r.surface_normal = dir;
    }

    // Parametric: spherical coordinates.
    Vec3 n = r.surface_normal;
    r.u = std::atan2(n(1), n(0));
    r.v = std::acos(std::clamp(n(2), -1.0, 1.0));
    return r;
}

Vec3 SphereS::evaluate(double u, double v) const {
    return centre_ + radius_ * Vec3(
        std::sin(v) * std::cos(u),
        std::sin(v) * std::sin(u),
        std::cos(v));
}

Vec3 SphereS::normal_at(double u, double v) const {
    return Vec3(std::sin(v) * std::cos(u),
                std::sin(v) * std::sin(u),
                std::cos(v));
}

// ============================================================================
// True-surface deviation
// ============================================================================

TrueSurfaceDeviation compute_true_surface_deviation(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const AnalyticSurface& surface) {
    TrueSurfaceDeviation result;
    auto n = points.cols();
    if (n == 0) return result;

    result.per_point.resize(static_cast<std::size_t>(n));
    std::vector<double> abs_dists(static_cast<std::size_t>(n));
    std::vector<double> signed_dists(static_cast<std::size_t>(n));
    std::vector<double> sq_dists(static_cast<std::size_t>(n));

    for (Eigen::Index i = 0; i < n; ++i) {
        auto ui = static_cast<std::size_t>(i);
        result.per_point[ui] = surface.closest_point(points.col(i));
        abs_dists[ui] = result.per_point[ui].distance;
        signed_dists[ui] = result.per_point[ui].signed_distance;
        sq_dists[ui] = abs_dists[ui] * abs_dists[ui];
    }

    result.rms = std::sqrt(
        numerics::neumaier_sum(sq_dists.begin(), sq_dists.end()) /
        static_cast<double>(n));
    result.max_abs = *std::max_element(abs_dists.begin(), abs_dists.end());
    result.mean_signed =
        numerics::neumaier_sum(signed_dists.begin(), signed_dists.end()) /
        static_cast<double>(n);

    return result;
}

// ============================================================================
// STEP file header reading (without OCCT — header parsing only)
// ============================================================================

StepFileInfo read_step_header(const std::string& path) {
    StepFileInfo info;

    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        info.warnings.push_back("Cannot open file: " + path);
        return info;
    }

    info.file_size = static_cast<std::size_t>(ifs.tellg());
    ifs.seekg(0);

    // Read enough for header + entity counting.
    std::string content;
    content.resize(std::min(info.file_size, std::size_t(1024 * 1024)));
    ifs.read(content.data(), static_cast<std::streamsize>(content.size()));

    // Verify STEP magic.
    if (content.find("ISO-10303-21") == std::string::npos) {
        info.warnings.push_back("Not a valid STEP file (missing ISO-10303-21)");
        return info;
    }

    // Parse FILE_NAME.
    auto extract = [&](const std::string& key) -> std::string {
        auto pos = content.find(key);
        if (pos == std::string::npos) return {};
        pos = content.find('\'', pos);
        if (pos == std::string::npos) return {};
        auto end = content.find('\'', pos + 1);
        if (end == std::string::npos) return {};
        return content.substr(pos + 1, end - pos - 1);
    };

    info.filename = extract("/* name */");
    info.description = extract("/* description */");
    info.timestamp = extract("/* time_stamp */");
    info.originating_system = extract("/* originating_system */");

    // Parse FILE_SCHEMA.
    auto schema_pos = content.find("FILE_SCHEMA");
    if (schema_pos != std::string::npos) {
        auto q1 = content.find('\'', schema_pos);
        if (q1 != std::string::npos) {
            auto q2 = content.find('\'', q1 + 1);
            if (q2 != std::string::npos)
                info.schema = content.substr(q1 + 1, q2 - q1 - 1);
        }
    }

    // Count entities (lines starting with #digit).
    // Read the full file for entity counting if it's not too large.
    if (info.file_size <= 200 * 1024 * 1024) {
        // Reset and count all entity lines.
        ifs.clear();
        ifs.seekg(0);
        std::string line;
        int count = 0;
        bool in_data = false;
        while (std::getline(ifs, line)) {
            if (line.find("DATA;") != std::string::npos) in_data = true;
            if (line.find("ENDSEC;") != std::string::npos) in_data = false;
            if (in_data && !line.empty() && line[0] == '#') ++count;
        }
        info.entity_count = count;
    }

    // Check for PMI/GD&T entities.
    if (content.find("GEOMETRIC_TOLERANCE") != std::string::npos ||
        content.find("DATUM_FEATURE") != std::string::npos ||
        content.find("DIMENSION_CALLOUT") != std::string::npos) {
        info.has_pmi = true;
    }

    return info;
}

} // namespace alignmesh::cad
