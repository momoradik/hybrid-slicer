#pragma once

#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <string>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// Precision-tier / input-format gate
// ============================================================================
//
// Given a requested tolerance, determines the required input formats and
// data-quality conditions and enforces them BEFORE alignment.
//
// Tiers (tolerance in mm):
//   COARSE   ≥ 0.100 mm (100 µm)   STL ok
//   MEDIUM   0.025–0.100 mm         STL ok, chord error checked
//   FINE     0.010–0.025 mm         high-density mesh or CAD; STL discouraged
//   PRECISE  0.001–0.010 mm         analytic CAD ref required; double cloud;
//                                   characterized scanner
//   ULTRA    < 0.001 mm (1 µm)      CAD + certified scanner + 20°C control
//
// FAIL-SAFE: if conditions unmet → INVALID_CLAIM, not a quiet downgrade.
// ============================================================================

enum class PrecisionTier {
    COARSE,    // ≥ 100 µm
    MEDIUM,    // 25–100 µm
    FINE,      // 10–25 µm
    PRECISE,   // 1–10 µm
    ULTRA,     // < 1 µm
};

/// Requirements for a precision tier.
struct TierRequirements {
    PrecisionTier tier;
    std::string tier_name;

    bool stl_allowed = true;
    bool stl_chord_check_required = false;
    bool high_density_required = false;
    bool cad_reference_required = false;
    bool double_precision_cloud_required = false;
    bool characterized_scanner_required = false;
    bool temperature_control_required = false;

    double max_chord_error_mm = 0;  // 0 = not checked

    std::string description;
};

/// Result of the precision-tier gate check.
struct PrecisionGateResult {
    bool passed = false;

    PrecisionTier tier;
    TierRequirements requirements;

    /// Estimated chord error of the STL tessellation (mm).
    double estimated_chord_error = 0;
    bool chord_error_computed = false;

    /// Why the check passed or failed.
    std::vector<std::string> satisfied;
    std::vector<std::string> violations;

    /// INVALID_CLAIM if conditions are unmet.
    bool invalid_claim = false;
    std::string invalid_reason;
};

/// Determine the precision tier for a given tolerance (in mm).
PrecisionTier determine_tier(double tolerance_mm);

/// Get the requirements for a tier.
TierRequirements tier_requirements(PrecisionTier tier, double tolerance_mm);

/// Estimate the STL tessellation chord error from the mesh geometry.
/// Uses the deviation between edge midpoints and the local surface
/// approximation (via triangle normals).
double estimate_chord_error(const geometry::TriangleMesh& mesh);

/// Run the precision-tier gate: check whether the input data meets the
/// requirements for the requested tolerance.
///
/// @param tolerance_mm   Requested tolerance in mm.
/// @param metadata       Source file metadata (format, precision, etc.).
/// @param mesh           The mesh (for chord-error computation).
/// @param has_cad_reference  True if an analytic CAD reference is available.
/// @param has_characterized_scanner  True if scanner is characterized.
/// @param has_temperature_control  True if 20°C environment is controlled.
PrecisionGateResult check_precision_gate(
    double tolerance_mm,
    const io::SourceMetadata& metadata,
    const geometry::TriangleMesh& mesh,
    bool has_cad_reference = false,
    bool has_characterized_scanner = false,
    bool has_temperature_control = false);

} // namespace alignmesh::analysis
