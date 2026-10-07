#pragma once

#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <Eigen/Core>
#include <string>
#include <vector>

namespace alignmesh::features {

// ============================================================================
// Feature-fitting / form-evaluation engine
// ============================================================================
//
// Criteria (ISO 1101 / ISO 5459 / ASME Y14.5):
//   LEAST_SQUARES (L2)        — Gaussian association. Available as override
//                                when drawing specifies Ⓖ. NEVER the default
//                                for form. Report always records criterion used.
//   MINIMUM_ZONE (L∞)         — ISO 1101 default for form tolerances.
//                                Current: iterative from LS seed. MUST validate
//                                conservative vs convex-hull bound. LP solver
//                                upgrade required before production acceptance.
//   MAXIMUM_INSCRIBED          — UAME for internal datums (holes).
//   MINIMUM_CIRCUMSCRIBED      — UAME for external datums (shafts/pins).
//   TANGENT_PLANE              — ISO 5459 datum plane. Minimax tangent
//                                (anti-rocking): minimizes max material-side
//                                residual while staying one-sided. Flags
//                                rocking/instability.
//
// ⛔ Criteria confirmed by review (Session 16 gate):
//   - Form: MINIMUM_ZONE (default). LS only if drawing Ⓖ (recorded+warned).
//   - Datum plane: TANGENT_PLANE (minimax anti-rocking, flag instability).
//   - Datum cylinder/sphere: MAX_INSCRIBED (hole) / MIN_CIRCUMSCRIBED (shaft).
//     Labeled as UAME. Size/form are SEPARATE evaluations — never reuse
//     the datum envelope fit as a size value.
//   - Cone datum: UNSUPPORTED (rejected, not silently defaulted to LS).
// ============================================================================

enum class FitCriterion {
    LEAST_SQUARES,
    MINIMUM_ZONE,
    MAXIMUM_INSCRIBED,
    MINIMUM_CIRCUMSCRIBED,
    TANGENT_PLANE,
};

/// Purpose of the fit (affects criterion defaults and validation).
enum class FitPurpose {
    FORM_EVALUATION,    // ISO 1101 form tolerance → default MINIMUM_ZONE
    DATUM_ASSOCIATION,  // ISO 5459 datum → UAME / tangent
    SIZE_EVALUATION,    // ISO 14405 size → separate from datum/form
};

struct FitResult {
    bool success = false;

    geometry::Vec3 origin = geometry::Vec3::Zero();
    geometry::Vec3 direction = geometry::Vec3::UnitZ();
    double radius = 0;
    double half_angle = 0;

    double rms_residual = 0;
    double max_residual = 0;
    double min_residual = 0;
    double form_error = 0;
    std::vector<double> residuals;

    FitCriterion criterion_used = FitCriterion::LEAST_SQUARES;
    FitPurpose purpose = FitPurpose::FORM_EVALUATION;
    std::string feature_type;

    /// True if the minimum-zone result has been validated conservative
    /// against the convex-hull bound. False → result may under-report.
    bool minzone_validated_conservative = false;

    /// Datum instability: true if the tangent fit is rocking (non-unique).
    bool datum_rocking = false;

    /// Is this a UAME (unrelated actual mating envelope) for datum?
    bool is_uame = false;

    std::vector<std::string> warnings;
};

// ============================================================================
// Fitting functions
// ============================================================================

FitResult fit_plane(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    FitCriterion criterion = FitCriterion::LEAST_SQUARES);

FitResult fit_cylinder(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    FitCriterion criterion = FitCriterion::LEAST_SQUARES);

FitResult fit_sphere(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    FitCriterion criterion = FitCriterion::LEAST_SQUARES);

// ============================================================================
// Datum association
// ============================================================================

/// Datum plane: minimax tangent (anti-rocking). Flags instability.
FitResult fit_datum_plane(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    int material_side = 1);

/// Datum cylinder: UAME. max-inscribed for holes, min-circumscribed for shafts.
/// @param is_internal  true = hole (max-inscribed), false = shaft (min-circumscribed).
FitResult fit_datum_cylinder(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    bool is_internal);

/// Cone datum: EXPLICITLY UNSUPPORTED. Returns success=false with reason.
FitResult fit_datum_cone(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points);

// ============================================================================
// Constrained fit (for datum precedence / 3-2-1)
// ============================================================================

/// Fit a cylinder with axis constrained perpendicular to a given direction.
/// Used for secondary datums constrained by the primary datum plane.
FitResult fit_cylinder_constrained(
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
    const geometry::Vec3& constrained_axis_direction,
    FitCriterion criterion = FitCriterion::LEAST_SQUARES);

} // namespace alignmesh::features
