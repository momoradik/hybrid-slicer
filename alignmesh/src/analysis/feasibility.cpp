#include "alignmesh/analysis/feasibility.h"

#include <cmath>
#include <sstream>

namespace alignmesh::analysis {

// ============================================================================
// ISO 14253-1 three-zone conformance
// ============================================================================

ConformanceZone evaluate_conformance(
        double deviation, double lsl, double usl, double U) {
    // Acceptance zone: [LSL + U, USL - U] — strict inequality (fail-safe).
    double accept_lo = lsl + U;
    double accept_hi = usl - U;

    if (accept_lo >= accept_hi) {
        // Uncertainty exceeds the entire tolerance zone — nothing can conform.
        return ConformanceZone::AMBIGUOUS;
    }

    // CONFORMANT: strictly inside acceptance zone.
    if (deviation > accept_lo && deviation < accept_hi) {
        return ConformanceZone::CONFORMANT;
    }

    // NON_CONFORMANT: outside [LSL - U, USL + U].
    if (deviation < lsl - U || deviation > usl + U) {
        return ConformanceZone::NON_CONFORMANT;
    }

    // AMBIGUOUS: within U of a specification limit.
    return ConformanceZone::AMBIGUOUS;
}

// ============================================================================
// Feasibility estimation
// ============================================================================

FeasibilityResult estimate_feasibility(
        double tolerance_mm,
        const DataQualityParams& q) {
    FeasibilityResult result;
    result.tolerance_mm = tolerance_mm;
    result.coverage_factor = q.coverage_factor;

    if (tolerance_mm <= 0) {
        result.invalid_claim = true;
        result.verdict = "INVALID CLAIM: tolerance must be positive.";
        return result;
    }

    // ---- Random contributors (RSS) ----------------------------------------

    auto add_random = [&](const char* name, const char* iso_ref,
                          double value, const std::string& deriv) {
        if (value > 0) {
            result.contributors.push_back({name, iso_ref, value, false, deriv});
        }
    };

    add_random("u_p (probing noise)",
               "ISO 15530-3 6.2",
               q.point_noise_mm,
               "Point-level noise (1-sigma)");

    add_random("u_r (reference fidelity)",
               "ISO 15530-3 6.3",
               q.reference_fidelity_mm,
               "Reference geometry uncertainty (chord error / numerical / master)");

    add_random("u_a (alignment)",
               "ISO 15530-4",
               q.alignment_uncertainty_mm,
               "Registration covariance (max diagonal sqrt)");

    add_random("u_s (sampling)",
               "ISO 15530-3 6.4",
               q.sampling_uncertainty_mm,
               "Sampling inadequacy (resolution vs feature size)");

    add_random("u_f (form error)",
               "ISO 15530-3 6.5",
               q.form_error_rms_mm,
               "Surface form deviation RMS");

    // Thermal: compute if raw inputs given, else use pre-computed value.
    double u_thermal = q.thermal_uncertainty_mm;
    if (u_thermal == 0 && q.temperature_deviation_c != 0 && q.part_size_mm > 0) {
        double cte = q.cte_um_per_m_per_c * 1e-6;  // µm/m/°C → mm/mm/°C
        u_thermal = cte * q.part_size_mm * std::abs(q.temperature_deviation_c);
    }
    add_random("u_t (thermal)",
               "ISO 15530-3 6.6",
               u_thermal,
               "CTE * size * |deltaT|");

    add_random("u_cal (calibration)",
               "ISO 15530-3 6.1",
               q.calibration_uncertainty_mm,
               "Scanner calibration uncertainty");

    add_random("u_fix (fixturing)",
               "ISO 15530-3 (fixturing)",
               q.fixturing_uncertainty_mm,
               "Fixture-induced deformation/positioning");

    add_random("u_stitch (stitching)",
               "ISO 15530-3 (multi-scan)",
               q.stitching_uncertainty_mm,
               "Multi-scan registration chain propagation");

    add_random("u_datum (datum establishment)",
               "ISO 15530-4 (datum)",
               q.datum_uncertainty_mm,
               "Datum feature fit uncertainty (distinct from alignment)");

    // ---- Systematic bias (linear, not RSS) --------------------------------

    double total_systematic = 0;
    if (q.systematic_scanner_bias_mm > 0) {
        std::string deriv;
        if (q.scanner_bias_corrected) {
            deriv = "Scanner bias corrected (" + q.scanner_bias_correction_method +
                    "); residual = " + std::to_string(q.systematic_scanner_bias_mm) + " mm";
        } else {
            deriv = "Uncorrected scanner bias (material/angle/reflectivity dependent)";
        }
        result.contributors.push_back({
            "b_scanner (systematic bias)",
            "ISO 15530-3 (systematic)",
            q.systematic_scanner_bias_mm,
            true,  // systematic
            deriv
        });
        total_systematic += q.systematic_scanner_bias_mm;
    }

    result.systematic_bias = total_systematic;

    // ---- Combination: U = k * sqrt(sum(u_random²)) + |b| -----------------
    // ISO 15530-3: systematic bias added linearly, not in quadrature.

    double sum_sq = 0;
    for (auto& c : result.contributors) {
        if (!c.is_systematic) {
            sum_sq += c.value_mm * c.value_mm;
        }
    }
    result.random_std_uncertainty = std::sqrt(sum_sq);
    result.expanded_uncertainty =
        q.coverage_factor * result.random_std_uncertainty + total_systematic;

    // ---- ISO 14253-1 acceptance zone (bilateral) --------------------------
    // For a bilateral tolerance ±T/2, the acceptance zone is:
    //   [LSL + U, USL - U] = [-T/2 + U, +T/2 - U]
    double half_tol = tolerance_mm / 2.0;
    result.acceptance_lower = -half_tol + result.expanded_uncertainty;
    result.acceptance_upper = +half_tol - result.expanded_uncertainty;

    // ---- Feasibility check ------------------------------------------------
    // FAIL-SAFE: U must be STRICTLY LESS than tolerance (not just T/2).
    // If the acceptance zone collapses (lower >= upper), nothing can conform.
    result.uncertainty_tolerance_ratio = result.expanded_uncertainty / tolerance_mm;
    result.feasible = (result.expanded_uncertainty < tolerance_mm &&
                       result.acceptance_lower < result.acceptance_upper);

    if (!result.feasible) {
        result.invalid_claim = true;
    }

    // ---- Warnings ---------------------------------------------------------
    if (result.contributors.empty()) {
        result.warnings.push_back(
            "No uncertainty contributors provided. Cannot verify feasibility. "
            "Provide scanner noise, reference fidelity, alignment, etc.");
        result.feasible = false;
        result.invalid_claim = true;
    }

    if (q.stitching_uncertainty_mm == 0 && q.point_noise_mm > 0) {
        result.warnings.push_back(
            "Stitching uncertainty is zero — verify this is a single-setup scan "
            "or provide multi-scan chain uncertainty.");
    }

    if (q.systematic_scanner_bias_mm > 0 && !q.scanner_bias_corrected) {
        result.warnings.push_back(
            "Systematic scanner bias is uncorrected — correction is preferred "
            "per ISO 15530-3. Uncorrected bias added linearly to U.");
    }

    // ---- Detail lines for report ------------------------------------------
    for (auto& c : result.contributors) {
        std::string prefix = c.is_systematic ? "[SYSTEMATIC] " : "[RANDOM]     ";
        result.details.push_back(
            prefix + c.name + " = " +
            std::to_string(c.value_mm * 1000.0) + " um  (" + c.iso_reference + ")");
    }
    result.details.push_back(
        "k = " + std::to_string(q.coverage_factor) +
        " | u_c(random) = " + std::to_string(result.random_std_uncertainty * 1000.0) +
        " um | |b| = " + std::to_string(total_systematic * 1000.0) +
        " um | U = " + std::to_string(result.expanded_uncertainty * 1000.0) + " um");
    result.details.push_back(
        "Acceptance zone: [" +
        std::to_string(result.acceptance_lower * 1000.0) + ", " +
        std::to_string(result.acceptance_upper * 1000.0) + "] um");

    // ---- Verdict ----------------------------------------------------------
    {
        std::ostringstream oss;
        if (result.feasible) {
            oss << "FEASIBLE: U = " << result.expanded_uncertainty * 1000.0
                << " um (k=" << q.coverage_factor
                << ") < T = " << tolerance_mm * 1000.0
                << " um. Ratio U/T = " << result.uncertainty_tolerance_ratio
                << ". Acceptance zone width = "
                << (result.acceptance_upper - result.acceptance_lower) * 1000.0
                << " um.";
        } else {
            oss << "INVALID CLAIM: U = " << result.expanded_uncertainty * 1000.0
                << " um (k=" << q.coverage_factor
                << ") >= T = " << tolerance_mm * 1000.0
                << " um. Ratio U/T = " << result.uncertainty_tolerance_ratio
                << ". The requested tolerance cannot be achieved.";
        }
        result.verdict = oss.str();
    }

    return result;
}

} // namespace alignmesh::analysis
