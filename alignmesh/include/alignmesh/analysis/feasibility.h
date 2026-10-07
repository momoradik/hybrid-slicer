#pragma once

#include "alignmesh/analysis/deviation.h"
#include "alignmesh/registration/observability.h"

#include <string>
#include <vector>

namespace alignmesh::analysis {

// ============================================================================
// Task-specific measurement uncertainty (ISO 15530 family)
// ============================================================================
//
// Estimates the achievable measurement uncertainty from the current data
// and compares it to the requested tolerance.
//
// ISO 15530 principle: uncertainty is always relative to a SPECIFIC TASK.
//
// COMBINATION (GUM / ISO 15530-3 faithful):
//
//   U = k * sqrt( sum(u_random_i^2) + 2*sum(cov_ij) ) + |b_systematic|
//
//   - Random, independent contributors: combined via RSS.
//   - Correlated contributors: include covariance terms (or conservatively
//     add linearly if correlation cannot be quantified).
//   - Systematic bias: added LINEARLY (not in quadrature), per ISO 15530-3.
//     Preferred: correct the bias and record the correction.
//     If uncorrected: add |b| linearly to the expanded uncertainty.
//
// THREE-ZONE DECISION (ISO 14253-1):
//
//   Given tolerance zone [LSL, USL]:
//   - CONFORMANT:     value in [LSL + U, USL - U]   (proven conformance)
//   - NON_CONFORMANT: value < LSL - U or > USL + U  (proven non-conformance)
//   - AMBIGUOUS:      within U of a limit            (neither proven)
//
//   Fail-safe: AMBIGUOUS never resolves to PASS.
//   "Counts against the proving party" (default: supplier proves conformance).
//
// ⛔ REVIEW-GATE: interpretation confirmed by Sina (Session 12 review).
// ============================================================================

/// Individual uncertainty contributor.
struct UncertaintyContributor {
    std::string name;
    std::string iso_reference;
    double value_mm = 0;        // standard uncertainty in mm
    bool is_systematic = false; // true = added linearly, false = RSS
    std::string derivation;
};

/// ISO 14253-1 three-zone conformance decision.
enum class ConformanceZone {
    CONFORMANT,      // proven conformance: value in [LSL+U, USL-U]
    NON_CONFORMANT,  // proven non-conformance: outside [LSL-U, USL+U]
    AMBIGUOUS,       // within U of a limit — neither proven (fail-safe: not PASS)
};

/// Feasibility estimate result.
struct FeasibilityResult {
    bool feasible = false;
    bool invalid_claim = false;

    double tolerance_mm = 0;

    /// Individual contributors.
    std::vector<UncertaintyContributor> contributors;

    /// Combined standard uncertainty of random terms u_c (mm, RSS).
    double random_std_uncertainty = 0;

    /// Total systematic bias |b| (mm, added linearly).
    double systematic_bias = 0;

    /// Expanded uncertainty U = k * u_c + |b| (mm).
    double expanded_uncertainty = 0;

    /// Coverage factor (configurable; default k=2 ≈ 95% confidence).
    double coverage_factor = 2.0;

    /// Ratio U / tolerance.
    double uncertainty_tolerance_ratio = 0;

    /// ISO 14253-1 conformance zone boundaries (for bilateral tolerance).
    double acceptance_lower = 0;  // LSL + U
    double acceptance_upper = 0;  // USL - U

    std::string verdict;
    std::vector<std::string> warnings;
    std::vector<std::string> details;
};

/// Input data quality parameters for feasibility estimation.
struct DataQualityParams {
    // ---- Random contributors (RSS) ----

    /// u_p: probing/scanning noise (mm, 1-sigma). ISO 15530-3 §6.2.
    double point_noise_mm = 0;

    /// u_r: reference geometry fidelity (mm). ISO 15530-3 §6.3.
    /// For tessellated reference: chord error.
    /// For analytic CAD: numerical/conversion sub-term only (geometric nominal = 0).
    /// For scanned master: the master's own measurement uncertainty.
    double reference_fidelity_mm = 0;

    /// u_a: alignment/registration uncertainty (mm). ISO 15530-4.
    double alignment_uncertainty_mm = 0;

    /// u_s: sampling inadequacy (mm).
    double sampling_uncertainty_mm = 0;

    /// u_f: form error (mm, surface deviation RMS). ISO 15530-3 §6.5.
    double form_error_rms_mm = 0;

    /// u_t: thermal expansion (mm). ISO 15530-3 §6.6.
    /// Computed as CTE * part_size * |delta_T|.
    double thermal_uncertainty_mm = 0;

    /// u_cal: calibration uncertainty (mm). ISO 15530-3 §6.1.
    double calibration_uncertainty_mm = 0;

    /// u_fix: fixturing uncertainty (mm). Fixture-induced deformation or
    /// positioning error during scanning.
    double fixturing_uncertainty_mm = 0;

    /// u_stitch: multi-scan stitching uncertainty (mm). Each stitch injects
    /// error that propagates. Zero only with single-setup justification.
    double stitching_uncertainty_mm = 0;

    /// u_datum: datum-establishment uncertainty (mm). Uncertainty from the
    /// datum feature fit itself (distinct from alignment covariance to avoid
    /// double-counting).
    double datum_uncertainty_mm = 0;

    // ---- Systematic bias (added linearly, not RSS) ----

    /// b_scanner: systematic scanner bias (mm). Material-dependent,
    /// angle-dependent, reflectivity-dependent offsets.
    /// Preferred: correct the bias and set this to the correction residual.
    /// If uncorrected: set to the estimated bias magnitude.
    double systematic_scanner_bias_mm = 0;
    bool scanner_bias_corrected = false;
    std::string scanner_bias_correction_method;

    // ---- Configuration ----

    /// Coverage factor (default k=2 ≈ 95%, k=3 ≈ 99.7%).
    double coverage_factor = 2.0;

    // ---- Convenience inputs for thermal computation ----
    double part_size_mm = 0;
    double temperature_deviation_c = 0;
    double cte_um_per_m_per_c = 12.0;  // default steel
};

/// Estimate task-specific measurement uncertainty and check feasibility.
FeasibilityResult estimate_feasibility(
    double tolerance_mm,
    const DataQualityParams& quality);

/// Evaluate a measured deviation against a bilateral tolerance zone
/// using the ISO 14253-1 three-zone decision logic.
///
/// @param deviation  Measured deviation value (mm).
/// @param lsl        Lower specification limit (mm, typically -tolerance/2).
/// @param usl        Upper specification limit (mm, typically +tolerance/2).
/// @param U          Expanded uncertainty (mm).
ConformanceZone evaluate_conformance(
    double deviation, double lsl, double usl, double U);

} // namespace alignmesh::analysis
