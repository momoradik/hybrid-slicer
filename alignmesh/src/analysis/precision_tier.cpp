#include "alignmesh/analysis/precision_tier.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace alignmesh::analysis {

using namespace geometry;

// ============================================================================
// Tier determination
// ============================================================================

PrecisionTier determine_tier(double tolerance_mm) {
    if (tolerance_mm <= 0)      return PrecisionTier::ULTRA;
    if (tolerance_mm < 0.001)   return PrecisionTier::ULTRA;
    if (tolerance_mm < 0.010)   return PrecisionTier::PRECISE;
    if (tolerance_mm < 0.025)   return PrecisionTier::FINE;
    if (tolerance_mm < 0.100)   return PrecisionTier::MEDIUM;
    return PrecisionTier::COARSE;
}

TierRequirements tier_requirements(PrecisionTier tier, double tolerance_mm) {
    TierRequirements req;
    req.tier = tier;

    switch (tier) {
    case PrecisionTier::COARSE:
        req.tier_name = "COARSE (>=100um)";
        req.stl_allowed = true;
        req.description = "STL tessellation acceptable at this tolerance level.";
        break;

    case PrecisionTier::MEDIUM:
        req.tier_name = "MEDIUM (25-100um)";
        req.stl_allowed = true;
        req.stl_chord_check_required = true;
        req.max_chord_error_mm = tolerance_mm * 0.5;
        req.description = "STL allowed if chord error < tolerance/2. "
            "Verify tessellation quality.";
        break;

    case PrecisionTier::FINE:
        req.tier_name = "FINE (10-25um)";
        req.stl_allowed = false;
        req.high_density_required = true;
        req.description = "STL discouraged; use high-density mesh or CAD reference. "
            "Float32 coordinates may limit precision.";
        break;

    case PrecisionTier::PRECISE:
        req.tier_name = "PRECISE (1-10um)";
        req.stl_allowed = false;
        req.cad_reference_required = true;
        req.double_precision_cloud_required = true;
        req.characterized_scanner_required = true;
        req.description = "Analytic CAD reference required. Double-precision cloud. "
            "Characterized scanner with known uncertainty budget.";
        break;

    case PrecisionTier::ULTRA:
        req.tier_name = "ULTRA (<1um)";
        req.stl_allowed = false;
        req.cad_reference_required = true;
        req.double_precision_cloud_required = true;
        req.characterized_scanner_required = true;
        req.temperature_control_required = true;
        req.description = "CAD + certified scanner + 20C thermal control. "
            "Sub-micron requires full metrological traceability.";
        break;
    }

    return req;
}

// ============================================================================
// Chord-error estimation
// ============================================================================

double estimate_chord_error(const TriangleMesh& mesh) {
    if (mesh.num_triangles() == 0) return 0;

    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    const auto& tri_adj = mesh.triangle_adjacency();

    // For each internal edge (shared by two triangles), compute the
    // deviation of the edge midpoint from the averaged plane of the
    // two adjacent triangles. This estimates the tessellation chord error.
    //
    // Chord error for a flat approximation of a curved surface:
    //   error = distance from the edge midpoint to the plane defined by
    //   the average of the two triangle normals at that edge.

    double max_chord_error = 0;

    using Edge = std::pair<int, int>;
    std::set<Edge> processed;

    for (Eigen::Index f = 0; f < mesh.num_triangles(); ++f) {
        for (int j = 0; j < 3; ++j) {
            int adj = tri_adj(j, f);
            if (adj < 0 || adj <= static_cast<int>(f)) continue;

            int v0 = tris(j, f);
            int v1 = tris((j + 1) % 3, f);
            Edge edge{std::min(v0, v1), std::max(v0, v1)};
            if (!processed.insert(edge).second) continue;

            // Compute normals of the two adjacent triangles.
            auto tri_normal = [&](Eigen::Index fi) -> Vec3 {
                Vec3 a = verts.col(tris(0, fi));
                Vec3 b = verts.col(tris(1, fi));
                Vec3 c = verts.col(tris(2, fi));
                return (b - a).cross(c - a).normalized();
            };

            Vec3 n1 = tri_normal(f);
            Vec3 n2 = tri_normal(static_cast<Eigen::Index>(adj));

            // Dihedral angle between the two triangles.
            double cos_angle = n1.dot(n2);
            cos_angle = std::clamp(cos_angle, -1.0, 1.0);
            double dihedral = std::acos(cos_angle);

            // Edge length.
            double edge_len = (verts.col(v1) - verts.col(v0)).norm();

            // Chord error estimate: for a circular arc with the observed
            // dihedral angle and chord (edge) length.
            // chord_error = (edge_len / 2) * tan(dihedral / 2)
            // For small angles: chord_error ≈ edge_len * dihedral / 4
            if (dihedral > 1e-10) {
                double ce = (edge_len / 2.0) * std::tan(dihedral / 2.0);
                // Clamp for near-180° dihedral (non-manifold / sharp edge).
                ce = std::min(ce, edge_len);
                max_chord_error = std::max(max_chord_error, ce);
            }
        }
    }

    return max_chord_error;
}

// ============================================================================
// Precision gate check
// ============================================================================

PrecisionGateResult check_precision_gate(
        double tolerance_mm,
        const io::SourceMetadata& metadata,
        const TriangleMesh& mesh,
        bool has_cad_reference,
        bool has_characterized_scanner,
        bool has_temperature_control) {
    PrecisionGateResult result;

    if (tolerance_mm <= 0) {
        result.tier = PrecisionTier::ULTRA;
        result.requirements = tier_requirements(PrecisionTier::ULTRA, tolerance_mm);
        result.invalid_claim = true;
        result.invalid_reason = "Tolerance must be positive";
        result.violations.push_back("Invalid tolerance: " + std::to_string(tolerance_mm));
        return result;
    }

    result.tier = determine_tier(tolerance_mm);
    result.requirements = tier_requirements(result.tier, tolerance_mm);
    const auto& req = result.requirements;

    bool is_stl = (metadata.format.find("stl") != std::string::npos);
    bool is_float32 = (metadata.coordinate_precision == "float32");

    // ---- Check each requirement ----

    // STL format check.
    if (!req.stl_allowed && is_stl) {
        result.violations.push_back(
            "STL format not allowed at " + req.tier_name +
            " tier — use high-density mesh or CAD reference");
    } else if (is_stl) {
        result.satisfied.push_back("STL format acceptable at this tier");
    }

    // Chord error check (MEDIUM tier).
    if (req.stl_chord_check_required && is_stl) {
        double ce = estimate_chord_error(mesh);
        result.estimated_chord_error = ce;
        result.chord_error_computed = true;
        if (ce > req.max_chord_error_mm) {
            result.violations.push_back(
                "STL chord error (" + std::to_string(ce) +
                " mm) exceeds limit (" +
                std::to_string(req.max_chord_error_mm) +
                " mm = tolerance/2)");
        } else {
            result.satisfied.push_back(
                "Chord error (" + std::to_string(ce) +
                " mm) within limit");
        }
    }

    // CAD reference check.
    if (req.cad_reference_required && !has_cad_reference) {
        result.violations.push_back(
            "Analytic CAD reference required at " + req.tier_name +
            " tier — tessellated mesh is insufficient");
    } else if (req.cad_reference_required) {
        result.satisfied.push_back("CAD reference available");
    }

    // Double precision check.
    if (req.double_precision_cloud_required && is_float32) {
        result.violations.push_back(
            "Double-precision (float64) cloud required at " + req.tier_name +
            " tier — input is float32");
    } else if (req.double_precision_cloud_required) {
        result.satisfied.push_back("Cloud is double precision");
    }

    // Characterized scanner.
    if (req.characterized_scanner_required && !has_characterized_scanner) {
        result.violations.push_back(
            "Characterized scanner required at " + req.tier_name + " tier");
    } else if (req.characterized_scanner_required) {
        result.satisfied.push_back("Scanner is characterized");
    }

    // Temperature control.
    if (req.temperature_control_required && !has_temperature_control) {
        result.violations.push_back(
            "20°C temperature control required at " + req.tier_name + " tier");
    } else if (req.temperature_control_required) {
        result.satisfied.push_back("Temperature control confirmed");
    }

    // ---- Determine pass/fail ----
    result.passed = result.violations.empty();
    if (!result.passed) {
        result.invalid_claim = true;
        result.invalid_reason =
            "INVALID CLAIM: input data does not meet requirements for " +
            std::to_string(tolerance_mm) + " mm tolerance (" +
            req.tier_name + "). " +
            std::to_string(result.violations.size()) + " violation(s).";
    }

    return result;
}

} // namespace alignmesh::analysis
