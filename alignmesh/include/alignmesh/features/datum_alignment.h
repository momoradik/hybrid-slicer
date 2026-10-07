#pragma once

#include "alignmesh/features/feature_fitting.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::features {

// ============================================================================
// Datum / RPS-constrained alignment (primary conformance mode)
// ============================================================================
//
// 3-2-1 DOF locking (ISO 5459 / ASME Y14.5):
//   Primary plane    → 3 DOFs (1 trans along normal + 2 rotations)
//   Secondary axis   → 2 DOFs (2 trans ⊥ axis, axis constrained ⊥ primary)
//   Tertiary point   → 1 DOF  (remaining rotation, via B→C direction)
//
// DRF convention:
//   Z = primary plane normal (away from material, deterministic sign)
//   X = from datum-B axis toward datum-C, projected into primary plane
//   Y = Z × X (right-handed)
//   Origin = primary plane origin (on the tangent surface)
//
// Datum-feature validity (Session 14 alignment-basis gate):
//   - Primary rocking → INVALID
//   - Primary form error >> tolerance → WARNING
//   - Secondary under-sampled (partial arc) → INVALID
//   - Tertiary too close to B axis (short moment arm) → WARNING
//
// ⛔ Confirmed by review (Session 17 gate).
// ============================================================================

struct DatumFeatureSpec {
    std::string label;
    enum class Type { PLANE, CYLINDER, POINT };
    Type type = Type::PLANE;
    Eigen::Matrix<double, 3, Eigen::Dynamic> points;
    bool is_internal = false;
    int material_side = 1;
};

struct DOFState {
    bool tx = false, ty = false, tz = false;
    bool rx = false, ry = false, rz = false;
    int total_locked() const {
        return int(tx)+int(ty)+int(tz)+int(rx)+int(ry)+int(rz);
    }
};

struct DatumFeatureResult {
    std::string label;
    DatumFeatureSpec::Type type;
    FitResult fit;
    DOFState dofs_locked_by_this;
    DOFState cumulative_dofs;

    /// Datum validity checks.
    bool valid = true;
    double form_error = 0;           // primary: flatness
    double arc_coverage_deg = 0;     // secondary: angular coverage
    double moment_arm = 0;           // tertiary: distance from B axis
    double datum_uncertainty_mm = 0; // propagated into GUM budget

    std::vector<std::string> warnings;
};

struct DatumFrameResult {
    bool success = false;
    geometry::RigidTransform part_to_datum;
    std::vector<DatumFeatureResult> datums;
    DOFState final_dofs;

    bool all_datums_valid = true;   // feeds Session 14 alignment-basis gate
    bool all_datums_stable = true;

    /// DRF convention details (recorded for audit).
    std::string z_axis_convention;  // "primary normal, away from material"
    std::string x_axis_convention;  // "B→C projected into primary plane"

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

enum class AlignmentMode {
    DATUM_CONSTRAINED,
    BEST_FIT,
    RPS,
};

DatumFrameResult establish_datum_frame(
    const std::vector<DatumFeatureSpec>& datums);

struct DatumDeviationResult {
    DatumFrameResult frame;
    AlignmentMode mode = AlignmentMode::DATUM_CONSTRAINED;
    std::vector<double> deviations;
    double rms = 0;
    double max_deviation = 0;
    std::string mode_label;
    std::vector<std::string> warnings;
};

DatumDeviationResult evaluate_datum_deviation(
    const std::vector<DatumFeatureSpec>& datums,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& inspection_points,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& nominal_points);

} // namespace alignmesh::features
