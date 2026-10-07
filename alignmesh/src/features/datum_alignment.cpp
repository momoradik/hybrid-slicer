#include "alignmesh/features/datum_alignment.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <cmath>
#include <numbers>

namespace alignmesh::features {

using namespace geometry;

// ============================================================================
// DOF helpers
// ============================================================================

static DOFState plane_dofs(const Vec3& normal) {
    DOFState d;
    double nx = std::abs(normal(0)), ny = std::abs(normal(1)), nz = std::abs(normal(2));
    if (nz >= nx && nz >= ny) { d.tz = true; d.rx = true; d.ry = true; }
    else if (ny >= nx)        { d.ty = true; d.rx = true; d.rz = true; }
    else                      { d.tx = true; d.ry = true; d.rz = true; }
    return d;
}

static DOFState cylinder_dofs(const Vec3&, const DOFState& locked) {
    DOFState d;
    // Secondary cylinder locks 2 translations (axis position in the primary plane).
    // The axis DIRECTION is already constrained by the primary (⊥ to plane).
    // It does NOT lock a rotation — that's the tertiary's job.
    if (!locked.tx) d.tx = true;
    if (!locked.ty) d.ty = true;
    // Only if both X/Y already locked, try Z.
    if (locked.tx && locked.ty && !locked.tz) d.tz = true;
    return d;
}

static DOFState point_dofs(const DOFState& locked) {
    DOFState d;
    // Tertiary locks the last rotation (rz for a Z-primary frame).
    if (!locked.rz) { d.rz = true; return d; }
    if (!locked.rx) { d.rx = true; return d; }
    if (!locked.ry) { d.ry = true; return d; }
    if (!locked.tx) { d.tx = true; return d; }
    if (!locked.ty) { d.ty = true; return d; }
    if (!locked.tz) { d.tz = true; return d; }
    return d;
}

static DOFState merge(const DOFState& a, const DOFState& b) {
    return {a.tx||b.tx, a.ty||b.ty, a.tz||b.tz, a.rx||b.rx, a.ry||b.ry, a.rz||b.rz};
}

// ============================================================================
// Datum validity checks
// ============================================================================

static void check_primary_validity(DatumFeatureResult& dr, double tolerance_hint = 0.1) {
    if (dr.fit.datum_rocking) {
        dr.valid = false;
        dr.warnings.push_back(
            "PRIMARY DATUM ROCKING: tangent plane is non-unique/unstable → INVALID");
    }
    dr.form_error = dr.fit.form_error;
    if (dr.form_error > tolerance_hint * 0.5) {
        dr.warnings.push_back(
            "Primary datum form error (" + std::to_string(dr.form_error) +
            " mm) is large relative to tolerance — DRF may be untrustworthy");
    }
    // Datum uncertainty: orientation uncertainty from form error on a plane
    // is approximately form_error / extent.
    double extent = 0;
    if (dr.fit.residuals.size() > 0) {
        // Rough extent estimate from the residual range.
        extent = 100.0;  // placeholder — real implementation needs point span
    }
    dr.datum_uncertainty_mm = (extent > 0) ? dr.form_error * 0.5 : 0;
}

static void check_secondary_validity(DatumFeatureResult& dr,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        const Vec3& axis) {
    // Arc coverage: compute the angular span of points around the axis.
    auto n = pts.cols();
    if (n < 5) {
        dr.valid = false;
        dr.warnings.push_back("Secondary datum has too few points (" +
            std::to_string(n) + ") for stable axis determination");
        return;
    }

    // Project points perpendicular to axis and compute angular span.
    Vec3 u = (std::abs(axis(0)) < 0.9) ? Vec3::UnitX() : Vec3::UnitY();
    u = (u - u.dot(axis) * axis).normalized();
    Vec3 v = axis.cross(u);

    double min_angle = 1e30, max_angle = -1e30;
    Vec3 centroid = Vec3::Zero();
    for (Eigen::Index i = 0; i < n; ++i) centroid += pts.col(i);
    centroid /= static_cast<double>(n);

    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = pts.col(i) - centroid;
        Vec3 radial = d - d.dot(axis) * axis;
        double angle = std::atan2(radial.dot(v), radial.dot(u));
        min_angle = std::min(min_angle, angle);
        max_angle = std::max(max_angle, angle);
    }
    dr.arc_coverage_deg = (max_angle - min_angle) * 180.0 / std::numbers::pi;

    if (dr.arc_coverage_deg < 60.0) {
        dr.valid = false;
        dr.warnings.push_back(
            "Secondary datum under-sampled: arc coverage = " +
            std::to_string(dr.arc_coverage_deg) +
            "° (need ≥60° for stable axis) → INVALID");
    } else if (dr.arc_coverage_deg < 120.0) {
        dr.warnings.push_back(
            "Secondary datum marginal: arc coverage = " +
            std::to_string(dr.arc_coverage_deg) + "°");
    }

    dr.datum_uncertainty_mm = dr.fit.rms_residual;
}

static void check_tertiary_validity(DatumFeatureResult& dr,
        const Vec3& tertiary_point,
        const Vec3& secondary_axis_origin) {
    dr.moment_arm = (tertiary_point - secondary_axis_origin).norm();
    if (dr.moment_arm < 1.0) {  // less than 1mm from B axis
        dr.warnings.push_back(
            "Tertiary datum too close to secondary axis (moment arm = " +
            std::to_string(dr.moment_arm) +
            " mm) — rotation poorly constrained");
        // Not invalid, but warned — a 0.5mm moment arm with 0.01mm positioning
        // uncertainty gives ~1.1° rotation uncertainty.
    }
    dr.datum_uncertainty_mm = 0.01;  // positioning uncertainty of a single point
}

// ============================================================================
// Establish datum frame
// ============================================================================

DatumFrameResult establish_datum_frame(
        const std::vector<DatumFeatureSpec>& datums) {
    DatumFrameResult result;

    if (datums.empty()) {
        result.errors.push_back("No datum features specified");
        return result;
    }

    DOFState cumulative;
    Vec3 primary_normal = Vec3::UnitZ();
    Vec3 secondary_axis_origin = Vec3::Zero();
    Vec3 tertiary_point = Vec3::Zero();

    for (std::size_t di = 0; di < datums.size(); ++di) {
        const auto& spec = datums[di];
        DatumFeatureResult dr;
        dr.label = spec.label;
        dr.type = spec.type;

        switch (spec.type) {
        case DatumFeatureSpec::Type::PLANE: {
            dr.fit = fit_datum_plane(spec.points, spec.material_side);
            if (!dr.fit.success) {
                result.errors.push_back("Datum " + spec.label + " plane fit failed");
                return result;
            }
            // Q3: deterministic normal sign — away from material.
            if (spec.material_side > 0) {
                // External: normal points away from material (upward).
                // Already correct from tangent plane fit.
            } else {
                // Internal: flip so normal points away from material.
                dr.fit.direction = -dr.fit.direction;
            }
            primary_normal = dr.fit.direction;
            dr.dofs_locked_by_this = plane_dofs(primary_normal);
            check_primary_validity(dr);
            if (dr.fit.datum_rocking) result.all_datums_stable = false;
            break;
        }
        case DatumFeatureSpec::Type::CYLINDER: {
            // Q2: CONSTRAINED fit — axis ⊥ primary, not free UAME.
            dr.fit = fit_cylinder_constrained(spec.points, primary_normal,
                spec.is_internal ? FitCriterion::MAXIMUM_INSCRIBED :
                                   FitCriterion::MINIMUM_CIRCUMSCRIBED);
            if (!dr.fit.success) {
                result.errors.push_back("Datum " + spec.label + " cylinder fit failed");
                return result;
            }
            dr.fit.is_uame = true;
            secondary_axis_origin = dr.fit.origin;
            // Secondary locks 2 translations only — NOT rotation.
            dr.dofs_locked_by_this = cylinder_dofs(dr.fit.direction, cumulative);
            check_secondary_validity(dr, spec.points, dr.fit.direction);
            break;
        }
        case DatumFeatureSpec::Type::POINT: {
            if (spec.points.cols() < 1) {
                result.errors.push_back("Datum " + spec.label + " needs ≥1 point");
                return result;
            }
            dr.fit.success = true;
            dr.fit.origin = spec.points.col(0);
            dr.fit.feature_type = "point";
            tertiary_point = dr.fit.origin;
            dr.dofs_locked_by_this = point_dofs(cumulative);
            if (!result.datums.empty() && result.datums.size() >= 2) {
                check_tertiary_validity(dr, tertiary_point, secondary_axis_origin);
            }
            break;
        }
        }

        if (!dr.valid) result.all_datums_valid = false;
        cumulative = merge(cumulative, dr.dofs_locked_by_this);
        dr.cumulative_dofs = cumulative;
        result.datums.push_back(std::move(dr));
    }

    result.final_dofs = cumulative;

    // ---- Build DRF transform ------------------------------------------------
    // Z = primary normal (deterministic: away from material)
    Vec3 z_axis = primary_normal;

    // X = B→C direction projected into primary plane (Q3 confirmed).
    Vec3 x_axis;
    if (result.datums.size() >= 3) {
        Vec3 b_to_c = tertiary_point - secondary_axis_origin;
        x_axis = b_to_c - b_to_c.dot(z_axis) * z_axis;
        double xn = x_axis.norm();
        if (xn > 1e-10) x_axis /= xn;
        else {
            // Fallback if C is on the B axis.
            Vec3 arb = (std::abs(z_axis(0)) < 0.9) ? Vec3::UnitX() : Vec3::UnitY();
            x_axis = (arb - arb.dot(z_axis) * z_axis).normalized();
            result.warnings.push_back("Tertiary point on secondary axis — "
                "X direction is ambiguous");
        }
    } else if (result.datums.size() >= 2 &&
               result.datums[1].type == DatumFeatureSpec::Type::CYLINDER) {
        Vec3 cyl_dir = result.datums[1].fit.direction;
        x_axis = (cyl_dir - cyl_dir.dot(z_axis) * z_axis);
        double xn = x_axis.norm();
        if (xn > 1e-10) x_axis /= xn;
        else { x_axis = Vec3::UnitX(); }
    } else {
        Vec3 arb = (std::abs(z_axis(0)) < 0.9) ? Vec3::UnitX() : Vec3::UnitY();
        x_axis = (arb - arb.dot(z_axis) * z_axis).normalized();
    }
    Vec3 y_axis = z_axis.cross(x_axis);

    result.z_axis_convention = "primary normal, away from material";
    result.x_axis_convention = (result.datums.size() >= 3) ?
        "B->C projected into primary plane" :
        "secondary axis or arbitrary in-plane direction";

    Mat3 R;
    R.col(0) = x_axis;
    R.col(1) = y_axis;
    R.col(2) = z_axis;
    Vec3 origin = result.datums[0].fit.origin;
    Vec3 t = -(R.transpose() * origin);
    result.part_to_datum = RigidTransform::from_rotation_translation(R.transpose(), t);

    if (!result.all_datums_valid) {
        result.warnings.push_back(
            "DATUM VALIDITY FAILED: one or more datum features are invalid. "
            "Session 14 alignment-basis gate must block PASS.");
    }

    result.success = true;
    return result;
}

// ============================================================================
// Datum deviation evaluation
// ============================================================================

DatumDeviationResult evaluate_datum_deviation(
        const std::vector<DatumFeatureSpec>& datums,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& inspection_points,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& nominal_points) {
    DatumDeviationResult result;
    result.mode = AlignmentMode::DATUM_CONSTRAINED;
    result.mode_label = "DATUM-CONSTRAINED (primary conformance mode)";

    result.frame = establish_datum_frame(datums);
    if (!result.frame.success) {
        result.warnings.push_back("Datum frame establishment failed");
        return result;
    }

    auto n = inspection_points.cols();
    if (n != nominal_points.cols() || n == 0) {
        result.warnings.push_back("Point count mismatch or empty");
        return result;
    }

    auto transformed = result.frame.part_to_datum.apply_cloud(inspection_points);
    auto nominal_tf = result.frame.part_to_datum.apply_cloud(nominal_points);

    result.deviations.resize(static_cast<std::size_t>(n));
    std::vector<double> sq(static_cast<std::size_t>(n));
    result.max_deviation = 0;

    for (Eigen::Index i = 0; i < n; ++i) {
        double d = (transformed.col(i) - nominal_tf.col(i)).norm();
        auto ui = static_cast<std::size_t>(i);
        result.deviations[ui] = d;
        sq[ui] = d * d;
        result.max_deviation = std::max(result.max_deviation, d);
    }
    result.rms = std::sqrt(
        numerics::neumaier_sum(sq.begin(), sq.end()) / static_cast<double>(n));

    if (!result.frame.all_datums_stable)
        result.warnings.push_back("Datum rocking detected — result unreliable");
    if (!result.frame.all_datums_valid)
        result.warnings.push_back(
            "INVALID DATUM: alignment-basis gate must block PASS");

    return result;
}

} // namespace alignmesh::features
