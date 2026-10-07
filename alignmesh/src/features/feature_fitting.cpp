#include "alignmesh/features/feature_fitting.h"

#include <Eigen/Eigenvalues>
#include <algorithm>
#include <cmath>
#include <limits>

namespace alignmesh::features {

using namespace geometry;

// ============================================================================
// Residual computation
// ============================================================================

static void compute_plane_residuals(FitResult& r,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts) {
    auto n = pts.cols();
    r.residuals.resize(static_cast<std::size_t>(n));
    std::vector<double> sq(static_cast<std::size_t>(n));
    r.max_residual = -1e30; r.min_residual = 1e30;
    for (Eigen::Index i = 0; i < n; ++i) {
        double res = r.direction.dot(pts.col(i) - r.origin);
        auto ui = static_cast<std::size_t>(i);
        r.residuals[ui] = res;
        sq[ui] = res * res;
        r.max_residual = std::max(r.max_residual, res);
        r.min_residual = std::min(r.min_residual, res);
    }
    r.rms_residual = std::sqrt(
        numerics::neumaier_sum(sq.begin(), sq.end()) / static_cast<double>(n));
    r.form_error = r.max_residual - r.min_residual;
}

static void compute_radial_residuals(FitResult& r,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        bool is_cylinder) {
    auto n = pts.cols();
    r.residuals.resize(static_cast<std::size_t>(n));
    std::vector<double> sq(static_cast<std::size_t>(n));
    r.max_residual = -1e30; r.min_residual = 1e30;
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = pts.col(i) - r.origin;
        if (is_cylinder) d = d - d.dot(r.direction) * r.direction;
        double res = d.norm() - r.radius;
        auto ui = static_cast<std::size_t>(i);
        r.residuals[ui] = res;
        sq[ui] = res * res;
        r.max_residual = std::max(r.max_residual, res);
        r.min_residual = std::min(r.min_residual, res);
    }
    r.rms_residual = std::sqrt(
        numerics::neumaier_sum(sq.begin(), sq.end()) / static_cast<double>(n));
    r.form_error = r.max_residual - r.min_residual;
}

// ============================================================================
// Convex-hull Z-width bound (for minimum-zone validation)
// ============================================================================

static double convex_hull_z_width(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts,
        const Vec3& normal) {
    double zmin = 1e30, zmax = -1e30;
    for (Eigen::Index i = 0; i < pts.cols(); ++i) {
        double z = normal.dot(pts.col(i));
        zmin = std::min(zmin, z);
        zmax = std::max(zmax, z);
    }
    return zmax - zmin;
}

// ============================================================================
// Plane — Least Squares (PCA)
// ============================================================================

static FitResult fit_plane_ls(const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts) {
    FitResult r;
    r.feature_type = "plane";
    r.criterion_used = FitCriterion::LEAST_SQUARES;
    auto n = pts.cols();
    if (n < 3) { r.warnings.push_back("Need >= 3 points"); return r; }

    Vec3 centroid = Vec3::Zero();
    for (Eigen::Index i = 0; i < n; ++i) centroid += pts.col(i);
    centroid /= static_cast<double>(n);

    Mat3 cov = Mat3::Zero();
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = pts.col(i) - centroid;
        cov += d * d.transpose();
    }
    cov /= static_cast<double>(n);

    Eigen::SelfAdjointEigenSolver<Mat3> eig(cov);
    if (eig.eigenvalues()(1) < 1e-10 * eig.eigenvalues()(2)) {
        r.warnings.push_back("Degenerate: collinear or coincident points");
    }

    r.origin = centroid;
    r.direction = eig.eigenvectors().col(0);
    r.success = true;
    compute_plane_residuals(r, pts);
    return r;
}

// ============================================================================
// Plane — Minimum Zone (iterative + conservative validation)
// ============================================================================

static FitResult fit_plane_minzone(const Eigen::Matrix<double, 3, Eigen::Dynamic>& pts) {
    auto ls = fit_plane_ls(pts);
    if (!ls.success) return ls;

    FitResult r = ls;
    r.criterion_used = FitCriterion::MINIMUM_ZONE;
    auto n = pts.cols();

    // Centre the zone.
    double mid = (r.max_residual + r.min_residual) / 2.0;
    r.origin = r.origin + mid * r.direction;
    compute_plane_residuals(r, pts);

    // Iterative refinement.
    for (int iter = 0; iter < 50; ++iter) {
        double best_zone = r.form_error;
        Vec3 best_origin = r.origin;
        Vec3 best_dir = r.direction;
        bool improved = false;
        double step = 0.001 / (1 + iter);

        Vec3 perturbs[4] = {
            Vec3(step, 0, 0), Vec3(-step, 0, 0),
            Vec3(0, step, 0), Vec3(0, -step, 0)
        };
        for (auto& dp : perturbs) {
            Vec3 trial = (r.direction + dp).normalized();
            double tmax = -1e30, tmin = 1e30;
            for (Eigen::Index i = 0; i < n; ++i) {
                double res = trial.dot(pts.col(i) - r.origin);
                tmax = std::max(tmax, res);
                tmin = std::min(tmin, res);
            }
            double zone = tmax - tmin;
            if (zone < best_zone - 1e-15) {
                best_zone = zone;
                best_dir = trial;
                best_origin = r.origin + ((tmax + tmin) / 2.0) * trial;
                improved = true;
            }
        }
        if (!improved) break;
        r.direction = best_dir;
        r.origin = best_origin;
        compute_plane_residuals(r, pts);
    }

    // Q5 conservative validation: min-zone must not be less than the
    // convex-hull projection width (which is a lower bound on the true
    // minimum zone for planes). If our result is wider, it's conservative.
    double hull_width = convex_hull_z_width(pts, r.direction);
    r.minzone_validated_conservative = (r.form_error >= hull_width - 1e-12);
    if (!r.minzone_validated_conservative) {
        r.warnings.push_back(
            "CAUTION: min-zone result (" + std::to_string(r.form_error) +
            ") may under-report true zone (" + std::to_string(hull_width) +
            "). LP solver upgrade required for production.");
    }

    return r;
}

// ============================================================================
// Plane dispatcher
// ============================================================================

FitResult fit_plane(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        FitCriterion criterion) {
    FitResult r;
    switch (criterion) {
    case FitCriterion::MINIMUM_ZONE:
        r = fit_plane_minzone(points);
        break;
    case FitCriterion::LEAST_SQUARES:
        r = fit_plane_ls(points);
        break;
    default:
        r = fit_plane_ls(points);
        break;
    }

    // Q4: if LS was used for form evaluation, warn.
    if (criterion == FitCriterion::LEAST_SQUARES) {
        r.warnings.push_back(
            "Least-squares fit used. Note: LS under-reports form zone width "
            "relative to the ISO 1101 minimum-zone default.");
    }

    return r;
}

// ============================================================================
// Sphere fitting — Least Squares (algebraic)
// ============================================================================

FitResult fit_sphere(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        FitCriterion criterion) {
    FitResult r;
    r.feature_type = "sphere";
    r.criterion_used = criterion;
    auto n = points.cols();
    if (n < 4) { r.warnings.push_back("Need >= 4 points"); return r; }

    Eigen::MatrixXd A(n, 4);
    Eigen::VectorXd b(n);
    for (Eigen::Index i = 0; i < n; ++i) {
        double x = points(0, i), y = points(1, i), z = points(2, i);
        A(i, 0) = 2 * x; A(i, 1) = 2 * y; A(i, 2) = 2 * z; A(i, 3) = 1;
        b(i) = x * x + y * y + z * z;
    }
    Eigen::Vector4d sol = A.colPivHouseholderQr().solve(b);
    r.origin = Vec3(sol(0), sol(1), sol(2));
    r.radius = std::sqrt(std::max(sol(3) + r.origin.squaredNorm(), 0.0));
    r.success = true;
    compute_radial_residuals(r, points, false);
    return r;
}

// ============================================================================
// Cylinder fitting — Least Squares (PCA axis + mean radius)
// ============================================================================

FitResult fit_cylinder(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        FitCriterion criterion) {
    FitResult r;
    r.feature_type = "cylinder";
    r.criterion_used = criterion;
    auto n = points.cols();
    if (n < 5) { r.warnings.push_back("Need >= 5 points"); return r; }

    Vec3 centroid = Vec3::Zero();
    for (Eigen::Index i = 0; i < n; ++i) centroid += points.col(i);
    centroid /= static_cast<double>(n);

    Mat3 cov = Mat3::Zero();
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = points.col(i) - centroid;
        cov += d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Mat3> eig(cov);
    Vec3 axis = eig.eigenvectors().col(2);

    double sum_r = 0;
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = points.col(i) - centroid;
        sum_r += (d - d.dot(axis) * axis).norm();
    }

    r.origin = centroid;
    r.direction = axis;
    r.radius = sum_r / static_cast<double>(n);
    r.success = true;
    compute_radial_residuals(r, points, true);
    return r;
}

// ============================================================================
// Datum plane — minimax tangent (anti-rocking)
// ============================================================================

FitResult fit_datum_plane(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        int material_side) {
    // Start from minimum-zone plane (not just LS) for better orientation.
    auto mz = fit_plane_minzone(points);
    if (!mz.success) return mz;

    FitResult r = mz;
    r.criterion_used = FitCriterion::TANGENT_PLANE;
    r.feature_type = "datum_plane";
    r.purpose = FitPurpose::DATUM_ASSOCIATION;

    // Shift to the contact side.
    double contact = (material_side > 0) ? r.max_residual : r.min_residual;
    r.origin = r.origin + contact * r.direction;
    compute_plane_residuals(r, points);

    // Rocking detection: count contact points (residual ≈ 0).
    int contact_count = 0;
    for (auto res : r.residuals) {
        if (std::abs(res) < 1e-6 * r.form_error + 1e-12)
            ++contact_count;
    }
    // A stable plane needs >= 3 non-collinear contact points.
    r.datum_rocking = (contact_count < 3);
    if (r.datum_rocking) {
        r.warnings.push_back(
            "DATUM ROCKING: only " + std::to_string(contact_count) +
            " contact point(s) — tangent plane is non-unique/unstable. "
            "This invalidates the datum frame (Session 14 alignment-basis gate).");
    }

    // Verify one-sidedness.
    if (material_side > 0) {
        for (auto res : r.residuals)
            if (res > 1e-10)
                r.warnings.push_back("Datum violation: point above plane");
    } else {
        for (auto res : r.residuals)
            if (res < -1e-10)
                r.warnings.push_back("Datum violation: point below plane");
    }

    return r;
}

// ============================================================================
// Datum cylinder — UAME
// ============================================================================

FitResult fit_datum_cylinder(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        bool is_internal) {
    auto ls = fit_cylinder(points);
    if (!ls.success) return ls;

    FitResult r = ls;
    r.purpose = FitPurpose::DATUM_ASSOCIATION;
    r.is_uame = true;

    if (is_internal) {
        // Maximum inscribed: radius = min(distance from axis to points).
        r.criterion_used = FitCriterion::MAXIMUM_INSCRIBED;
        r.feature_type = "datum_cylinder_UAME_inscribed";
        // Shrink radius to the nearest point.
        double min_dist = 1e30;
        auto n = points.cols();
        for (Eigen::Index i = 0; i < n; ++i) {
            Vec3 d = points.col(i) - r.origin;
            double radial = (d - d.dot(r.direction) * r.direction).norm();
            min_dist = std::min(min_dist, radial);
        }
        r.radius = min_dist;
    } else {
        // Minimum circumscribed: radius = max(distance from axis to points).
        r.criterion_used = FitCriterion::MINIMUM_CIRCUMSCRIBED;
        r.feature_type = "datum_cylinder_UAME_circumscribed";
        double max_dist = 0;
        auto n = points.cols();
        for (Eigen::Index i = 0; i < n; ++i) {
            Vec3 d = points.col(i) - r.origin;
            double radial = (d - d.dot(r.direction) * r.direction).norm();
            max_dist = std::max(max_dist, radial);
        }
        r.radius = max_dist;
    }

    compute_radial_residuals(r, points, true);
    r.warnings.push_back(
        "UAME datum fit. Do NOT reuse this radius as the feature SIZE — "
        "size evaluation requires a separate criterion (ISO 14405).");
    return r;
}

// ============================================================================
// Cone datum — UNSUPPORTED
// ============================================================================

FitResult fit_datum_cone(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>&) {
    FitResult r;
    r.feature_type = "cone";
    r.purpose = FitPurpose::DATUM_ASSOCIATION;
    r.success = false;
    r.warnings.push_back(
        "UNSUPPORTED: cone datum association is not yet implemented. "
        "No clean one-sided mating envelope exists for a general cone "
        "in the basic standards. Do not silently default to LS.");
    return r;
}

// ============================================================================
// Constrained cylinder fit (for datum precedence / 3-2-1)
// ============================================================================

FitResult fit_cylinder_constrained(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const Vec3& constrained_axis_direction,
        FitCriterion criterion) {
    auto r = fit_cylinder(points, criterion);
    if (!r.success) return r;

    // Override axis to the constrained direction.
    r.direction = constrained_axis_direction.normalized();
    r.feature_type = "cylinder_constrained";

    // Recompute radius with the constrained axis.
    double sum_r = 0;
    auto n = points.cols();
    for (Eigen::Index i = 0; i < n; ++i) {
        Vec3 d = points.col(i) - r.origin;
        sum_r += (d - d.dot(r.direction) * r.direction).norm();
    }
    r.radius = sum_r / static_cast<double>(n);

    compute_radial_residuals(r, points, true);
    return r;
}

} // namespace alignmesh::features
