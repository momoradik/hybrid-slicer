#pragma once

#include "alignmesh/analysis/decision_rule.h"
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/analysis/feasibility.h"
#include "alignmesh/analysis/gum_uncertainty.h"
#include "alignmesh/analysis/precision_tier.h"
#include "alignmesh/numerics/environment_fingerprint.h"
#include "alignmesh/registration/observability.h"

#include <Eigen/Core>
#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::service {

// ============================================================================
// Result Package — the ONLY thing the UI receives from the core
// ============================================================================
//
// COMPUTE FIREWALL (spec §8.6):
//   The UI DISPLAYS and ORCHESTRATES only.
//   It NEVER computes a measurement, transform, color mapping from raw
//   distances, or pass/fail decision.
//
//   Everything the UI needs to render is PRE-COMPUTED by the core and
//   delivered in this package. The UI takes these values verbatim:
//   - Per-point deviations (already computed, signed)
//   - Per-point heatmap colors (already mapped from deviation to color)
//   - Statistics (already computed)
//   - Decision/verdict (already classified)
//   - Uncertainty budget (already evaluated)
//   - Environment fingerprint (for provenance)
//
//   The heatmap is CORROBORATING, NOT AUTHORITATIVE — labeled as such.
//
//   If the core is unavailable, the UI shows an error.
//   It NEVER fabricates a result.
// ============================================================================

/// Pre-computed color for a deviation point (RGBA, 0-255).
struct HeatmapColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// Per-point display data (pre-computed by core).
struct PointDisplayData {
    double x = 0, y = 0, z = 0;           // position (in display frame)
    double deviation = 0;                   // signed deviation (mm)
    double abs_deviation = 0;               // unsigned
    HeatmapColor color;                     // already mapped
};

/// The full result package sent from core to UI.
struct ResultPackage {
    bool valid = false;

    // ---- Identity / provenance ----
    std::string job_id;
    std::string reference_file;
    std::string measured_file;
    std::string reference_hash;
    std::string measured_hash;
    numerics::EnvironmentFingerprint fingerprint;
    std::string core_version;

    // ---- Alignment result ----
    std::string alignment_mode;       // "DATUM-CONSTRAINED" / "BEST-FIT" / etc.
    std::string alignment_basis;      // which datums / landmarks
    double alignment_rms = 0;
    bool alignment_converged = false;

    // ---- Transform (for before/after overlay) ----
    // 4x4 matrix, row-major, pre-computed — UI applies this to render
    // the aligned overlay, but NEVER derives measurements from it.
    double transform_matrix[16] = {};

    // ---- Per-point deviations (pre-computed) ----
    std::vector<PointDisplayData> points;

    // ---- Statistics (pre-computed) ----
    analysis::DeviationStatistics unsigned_stats;
    analysis::DeviationStatistics signed_stats;

    // ---- Heatmap metadata (pre-computed mapping) ----
    double heatmap_min = 0;           // deviation → color range
    double heatmap_max = 0;
    std::string heatmap_label;        // "CORROBORATING — not authoritative"

    // ---- Uncertainty ----
    double expanded_uncertainty = 0;
    double coverage_factor = 0;
    std::vector<std::string> uncertainty_details;

    /// True only when ALL required uncertainty contributors are populated
    /// from real calibrated-artifact data. Until established, PASS is blocked.
    bool uncertainty_established = false;
    std::vector<std::string> unestablished_contributors;

    // ---- Decision ----
    analysis::Verdict verdict = analysis::Verdict::INVALID;
    analysis::Verdict granular_verdict = analysis::Verdict::INVALID;
    std::string verdict_label;
    std::string proof_burden;
    double tolerance = 0;
    double acceptance_lower = 0;
    double acceptance_upper = 0;

    // ---- Precision tier ----
    std::string precision_tier;
    bool precision_gate_passed = false;

    // ---- Observability ----
    bool fully_constrained = false;
    int num_under_constrained = 0;

    // ---- RPS projected points (for display) ----
    // The projected location on the measured cloud for each RPS point.
    // In the REFERENCE frame (same as rps nominal points).
    struct RPSProjectedPoint { double x=0, y=0, z=0; bool valid=false; };
    std::vector<RPSProjectedPoint> rps_projected_points;

    // ---- Warnings / errors ----
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    // ---- Report metadata ----
    std::string timestamp;
    std::vector<std::string> report_lines;
};

// ============================================================================
// Service API — the core-side interface
// ============================================================================

/// A corresponding point pair for landmark alignment.
struct LandmarkPair {
    double ref_x = 0, ref_y = 0, ref_z = 0;    // point on reference
    double meas_x = 0, meas_y = 0, meas_z = 0;  // corresponding point on measured
    double weight = 1.0;
};

/// A directional lock for an RPS point (from UI).
struct RPSLockSpec {
    std::string axis;    // "x" / "y" / "z" / "normal" (resolved to surface normal)
    double weight = 1.0; // priority weight (>0)
};

/// An RPS datum point specification (for pre-aligned RPS alignment).
/// Each point has a position on the reference part and one or more
/// directional locks with weights that control the fit priority.
struct RPSPointSpec {
    double x = 0, y = 0, z = 0;       // position on reference part
    std::vector<RPSLockSpec> locks;    // per-direction constraints
};

/// Inspection request from the UI.
struct InspectionRequest {
    std::string reference_path;
    std::string measured_path;
    double tolerance_mm = 0.1;

    /// Alignment mode.
    std::string alignment_mode = "coarse-to-fine";  // "coarse-to-fine" / "best-fit" / "landmark" / "pre-aligned-rps"

    /// Landmark pairs (when alignment_mode == "landmark").
    /// Minimum 3 non-collinear pairs required.
    std::vector<LandmarkPair> landmarks;

    /// RPS constraint points (when alignment_mode == "pre-aligned-rps").
    /// Each point has reference position + directional locks with weights.
    /// Total directional constraints must have Jacobian rank >= 6.
    std::vector<RPSPointSpec> rps_points;

    /// Rotation search step in degrees (for exhaustive search).
    /// Smaller = more thorough but slower. Default 30°.
    double angle_step_degrees = 30.0;

    /// Heatmap range (0 = auto from tolerance).
    double heatmap_range = 0;
};

/// Run a full inspection and produce a result package.
/// This is the ONLY entry point the UI calls.
/// All computation happens here; the UI receives only pre-computed values.
/// If a store is provided, it reuses previously imported files (avoids re-importing STEP).
ResultPackage run_inspection(const InspectionRequest& request,
                             io::ImmutableSourceStore* store = nullptr);

/// Map a deviation value to a heatmap color.
/// This is done IN THE CORE, not in the UI.
HeatmapColor deviation_to_color(double deviation, double range_min, double range_max);

} // namespace alignmesh::service
