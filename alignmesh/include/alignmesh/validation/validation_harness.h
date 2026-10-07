#pragma once

#include "alignmesh/numerics/environment_fingerprint.h"

#include <string>
#include <vector>

namespace alignmesh::validation {

// ============================================================================
// Validation harness: MSA, cross-library, cross-machine determinism
// ============================================================================
//
// MSA / Gauge R&R (AIAG MSA 4th ed.):
//   Evaluates repeatability (EV), reproducibility (AV), and
//   combined GRR as a percentage of tolerance (%GRR).
//   %GRR < 10% → acceptable; 10-30% → marginal; >30% → unacceptable.
//
// Cross-library agreement:
//   Compares our production result against reference implementations
//   (PCL, Open3D, libpointmatcher) on shared test cases. These are
//   validation references ONLY — never the production path.
//
// Cross-machine determinism:
//   Same inputs on different machines must produce results within
//   a documented ULP bound. Divergence blocks release.
// ============================================================================

/// A single measurement in an MSA study.
struct MSAMeasurement {
    int part_id = 0;
    int operator_id = 0;
    int trial = 0;
    double value = 0;
};

/// MSA / Gauge R&R result.
struct GaugeRRResult {
    bool computed = false;

    int n_parts = 0;
    int n_operators = 0;
    int n_trials = 0;

    double ev = 0;        // equipment variation (repeatability)
    double av = 0;        // appraiser variation (reproducibility)
    double grr = 0;       // gauge R&R = sqrt(EV² + AV²)
    double pv = 0;        // part variation
    double tv = 0;        // total variation = sqrt(GRR² + PV²)

    double percent_ev = 0;
    double percent_av = 0;
    double percent_grr = 0;  // %GRR = GRR / TV * 100
    double percent_pv = 0;

    /// Precision-to-tolerance ratio.
    double tolerance = 0;
    double p_to_t = 0;   // GRR / tolerance * 100

    std::string classification;  // "acceptable" / "marginal" / "unacceptable"
    std::vector<std::string> details;
};

/// Compute Gauge R&R from a set of measurements.
/// Uses the ANOVA method (AIAG MSA 4th edition).
GaugeRRResult compute_gauge_rr(
    const std::vector<MSAMeasurement>& measurements,
    double tolerance = 0);

/// Cross-library comparison result.
struct CrossLibraryResult {
    std::string library_name;      // e.g., "PCL", "Open3D"
    std::string test_case;
    double our_value = 0;
    double reference_value = 0;
    double delta = 0;
    double bound = 0;              // documented agreement bound
    bool within_bound = false;
    std::string note;
};

/// Cross-machine determinism result.
struct CrossMachineResult {
    numerics::EnvironmentFingerprint machine_a;
    numerics::EnvironmentFingerprint machine_b;

    struct ValueComparison {
        std::string name;
        double value_a = 0;
        double value_b = 0;
        double delta = 0;
        double ulp_bound = 0;
        bool matches = false;
    };

    std::vector<ValueComparison> comparisons;
    bool all_match = false;
    std::vector<std::string> divergences;
};

/// Generate a determinism manifest for cross-machine comparison.
/// Returns a set of named values computed from the validation suite.
struct DeterminismManifest {
    std::string machine_fingerprint;
    std::vector<std::pair<std::string, double>> values;
};

DeterminismManifest generate_determinism_manifest();

/// Compare two manifests and report divergences.
CrossMachineResult compare_manifests(
    const DeterminismManifest& a,
    const DeterminismManifest& b,
    double ulp_bound = 0);

} // namespace alignmesh::validation
