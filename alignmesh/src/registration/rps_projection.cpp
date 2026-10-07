#include "alignmesh/registration/rps_projection.h"
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/numerics/compensated_sum.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Internal helpers — robust local quadric surface fitting
// (Khameneifar & Feng, CAD 2017: balanced neighborhood for local quadric)
// ============================================================================

static constexpr double kPi = 3.14159265358979323846;

// Compute surface normal at a point on the reference mesh by finding the
// nearest triangle and using its face normal.  Returns false if the nearest
// triangle is degenerate (zero-area) → NO_NORMAL.
static bool reference_normal_at(
        const Eigen::Vector3d& point,
        const geometry::TriangleMesh& mesh,
        Eigen::Vector3d& normal_out) {
    const auto& V = mesh.vertices();
    const auto& F = mesh.triangles();

    if (F.cols() == 0) return false;

    double best_dist_sq = std::numeric_limits<double>::max();
    Eigen::Vector3d best_normal = Eigen::Vector3d::Zero();
    bool found_valid = false;

    // Brute-force over triangles (datum points are few — typically 1-6).
    for (Eigen::Index f = 0; f < F.cols(); ++f) {
        auto i0 = F(0, f), i1 = F(1, f), i2 = F(2, f);
        Eigen::Vector3d v0 = V.col(i0), v1 = V.col(i1), v2 = V.col(i2);
        double dist_sq;
        analysis::closest_point_on_triangle(point, v0, v1, v2, dist_sq);
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            Eigen::Vector3d edge1 = v1 - v0;
            Eigen::Vector3d edge2 = v2 - v0;
            Eigen::Vector3d n = edge1.cross(edge2);
            double len = n.norm();
            if (len > 1e-15) {
                best_normal = n / len;
                found_valid = true;
            } else {
                // Degenerate triangle — keep searching but record it
                found_valid = false;
            }
        }
    }
    if (found_valid) {
        normal_out = best_normal;
    }
    return found_valid;
}

// Compute the auto search radius from the measured point cloud.
// Uses the 90th-percentile nearest-neighbor distance × factor, with
// a bbox-based floor. The p90 is used instead of the median to avoid
// being fooled by near-duplicate vertices from STL float32 merging
// (which create dense clusters with sub-micron NN distances that are
// not representative of the actual surface sampling spacing).
static double compute_auto_search_radius(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& points,
        const spatial::KdTree& tree,
        double factor) {
    Eigen::Index n = points.cols();
    if (n < 2) return 1.0; // degenerate

    // Sample up to 500 points for NN distance estimation.
    int sample_count = static_cast<int>(std::min(n, Eigen::Index(500)));
    int step = std::max(1, static_cast<int>(n / sample_count));

    std::vector<double> nn_dists;
    nn_dists.reserve(static_cast<std::size_t>(sample_count));
    std::vector<int> knn_idx;
    std::vector<double> knn_dsq;

    for (Eigen::Index i = 0; i < n; i += step) {
        tree.knn(points.col(i), 2, knn_idx, knn_dsq);
        if (knn_dsq.size() >= 2 && knn_dsq[1] > 0) {
            nn_dists.push_back(std::sqrt(knn_dsq[1]));
        }
    }
    if (nn_dists.empty()) return 1.0;

    std::sort(nn_dists.begin(), nn_dists.end());
    double p90 = nn_dists[nn_dists.size() * 9 / 10];
    double radius = p90 * factor;

    // Floor: at least bbox_diagonal / 100 to ensure the search radius
    // covers the pre-alignment residual (~0.5mm typical on a 150mm part)
    // plus a meaningful surface patch. This prevents pathologically small
    // radii on meshes with near-duplicate vertices from float32 merging.
    auto bbox_size = points.rowwise().maxCoeff() - points.rowwise().minCoeff();
    double diag = bbox_size.norm();
    double floor = diag / 100.0;
    if (radius < floor) radius = floor;

    return radius;
}

// Huber weight function: w(r) = 1 if |r| <= k, else k/|r|.
static double huber_weight(double residual, double k_sigma) {
    double abs_r = std::abs(residual);
    if (abs_r <= k_sigma) return 1.0;
    return k_sigma / abs_r;
}

// Result of the local surface fit.
struct LocalSurfaceFit {
    bool success = false;
    bool grazing = false;                // true if nominal normal is near-tangent to fitted surface
    Eigen::Vector3d fitted_point;        // projected point on the fitted surface
    Eigen::Vector3d fitted_normal;       // surface normal of the fitted surface at the projected point
    double roughness = 0;                // RMS fit residual
    int num_intersections = 0;           // number of line-surface intersections found
    std::string failure_reason;
};

// Build a local coordinate frame for surface fitting.
// origin: centroid of the neighborhood.
// Z-axis: PCA smallest-eigenvector direction (surface normal estimate).
// X, Y: tangent plane basis.
// Returns: rotation matrix R where columns are [x_local, y_local, z_local].
static Eigen::Matrix3d local_frame(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& local_pts,
        const Eigen::VectorXd& weights,
        const Eigen::Vector3d& centroid,
        Eigen::Vector3d& pca_normal) {
    // Weighted covariance matrix.
    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    double total_w = 0;
    for (Eigen::Index i = 0; i < local_pts.cols(); ++i) {
        Eigen::Vector3d d = local_pts.col(i) - centroid;
        cov += weights(i) * d * d.transpose();
        total_w += weights(i);
    }
    if (total_w > 1e-30) cov /= total_w;

    // Eigendecomposition — sorted ascending: lambda_0 <= lambda_1 <= lambda_2.
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
    auto evecs = eig.eigenvectors(); // columns sorted by ascending eigenvalue

    // PCA normal = eigenvector of smallest eigenvalue (column 0).
    pca_normal = evecs.col(0);

    // Build orthonormal frame: z = pca_normal, x = evecs.col(2), y = evecs.col(1)
    Eigen::Matrix3d R;
    R.col(0) = evecs.col(2); // x (largest spread)
    R.col(1) = evecs.col(1); // y
    R.col(2) = pca_normal;   // z (normal)
    return R;
}

// Fit a robust local quadric surface and project p_nom along nominal_normal
// onto it. Second-order polynomial fit in PCA frame with Huber IRLS.
static LocalSurfaceFit fit_and_project(
        const Eigen::Vector3d& p_nom,
        const Eigen::Vector3d& nominal_normal,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& neighbor_pts,
        int min_neighbors_quadric,
        double huber_k,
        int robust_iterations) {

    LocalSurfaceFit result;
    Eigen::Index n = neighbor_pts.cols();

    // Weighted centroid (initial: uniform weights).
    Eigen::VectorXd weights = Eigen::VectorXd::Ones(n);
    Eigen::Vector3d centroid = neighbor_pts.rowwise().mean();

    // Build local coordinate frame via PCA.
    Eigen::Vector3d pca_normal;
    Eigen::Matrix3d frame = local_frame(neighbor_pts, weights, centroid, pca_normal);

    // Orient PCA normal to agree with the nominal normal.
    if (pca_normal.dot(nominal_normal) < 0) {
        pca_normal = -pca_normal;
        frame.col(2) = -frame.col(2);
    }

    // Transform points to local coordinates.
    Eigen::Matrix<double, 3, Eigen::Dynamic> local(3, n);
    for (Eigen::Index i = 0; i < n; ++i) {
        local.col(i) = frame.transpose() * (neighbor_pts.col(i) - centroid);
    }
    // Query point in local coords.
    Eigen::Vector3d q_local = frame.transpose() * (p_nom - centroid);
    // Nominal normal in local coords.
    Eigen::Vector3d n_local = frame.transpose() * nominal_normal;

    bool use_quadric = (n >= min_neighbors_quadric);

    // ── Surface fit with robust IRLS ──────────────────────────────────
    // Quadric: z = a0 + a1*x + a2*y + a3*x^2 + a4*x*y + a5*y^2 (6 params)
    // Plane:   z = a0 + a1*x + a2*y                              (3 params)

    int n_params = use_quadric ? 6 : 3;
    Eigen::VectorXd coeffs;

    for (int iter = 0; iter <= robust_iterations; ++iter) {
        // Build design matrix.
        Eigen::MatrixXd A(n, n_params);
        Eigen::VectorXd b(n);

        for (Eigen::Index i = 0; i < n; ++i) {
            double x = local(0, i), y = local(1, i), z = local(2, i);
            A(i, 0) = 1.0;
            A(i, 1) = x;
            A(i, 2) = y;
            if (use_quadric) {
                A(i, 3) = x * x;
                A(i, 4) = x * y;
                A(i, 5) = y * y;
            }
            b(i) = z;
        }

        // Weighted least squares: (A^T W A) c = A^T W b
        Eigen::MatrixXd W = weights.asDiagonal();
        Eigen::MatrixXd AtW = A.transpose() * W;
        Eigen::MatrixXd AtWA = AtW * A;
        Eigen::VectorXd AtWb = AtW * b;

        // Solve via LDLT (symmetric positive semi-definite).
        Eigen::LDLT<Eigen::MatrixXd> ldlt(AtWA);
        if (ldlt.info() != Eigen::Success || !ldlt.isPositive()) {
            // Singular — can't fit
            result.failure_reason = "Surface fit: singular normal equations";
            return result;
        }
        coeffs = ldlt.solve(AtWb);

        // Compute residuals and update Huber weights for next iteration.
        if (iter < robust_iterations) {
            std::vector<double> residuals_abs;
            residuals_abs.reserve(static_cast<std::size_t>(n));
            for (Eigen::Index i = 0; i < n; ++i) {
                double x = local(0, i), y = local(1, i), z = local(2, i);
                double z_fit = coeffs(0) + coeffs(1) * x + coeffs(2) * y;
                if (use_quadric) {
                    z_fit += coeffs(3) * x * x + coeffs(4) * x * y + coeffs(5) * y * y;
                }
                residuals_abs.push_back(std::abs(z - z_fit));
            }

            // Estimate sigma (MAD * 1.4826 for robustness).
            std::vector<double> sorted_res = residuals_abs;
            std::sort(sorted_res.begin(), sorted_res.end());
            double mad = sorted_res[sorted_res.size() / 2];
            double sigma = std::max(mad * 1.4826, 1e-15);
            double k_sigma = huber_k * sigma;

            for (Eigen::Index i = 0; i < n; ++i) {
                weights(i) = huber_weight(residuals_abs[static_cast<std::size_t>(i)], k_sigma);
            }
        }
    }

    // ── Compute roughness (RMS of weighted residuals) ─────────────────
    {
        std::vector<double> sq_res;
        sq_res.reserve(static_cast<std::size_t>(n));
        for (Eigen::Index i = 0; i < n; ++i) {
            double x = local(0, i), y = local(1, i), z = local(2, i);
            double z_fit = coeffs(0) + coeffs(1) * x + coeffs(2) * y;
            if (use_quadric) {
                z_fit += coeffs(3) * x * x + coeffs(4) * x * y + coeffs(5) * y * y;
            }
            double r = z - z_fit;
            sq_res.push_back(r * r);
        }
        double sum_sq = numerics::neumaier_sum(sq_res.begin(), sq_res.end());
        result.roughness = std::sqrt(sum_sq / static_cast<double>(n));
    }

    // ── Compute fitted surface normal at query point BEFORE intersection ─
    // This detects grazing early regardless of whether intersection succeeds.

    double qx = q_local.x(), qy = q_local.y(), qz = q_local.z();
    double nx = n_local.x(), ny = n_local.y(), nz = n_local.z();

    {
        Eigen::Vector3d surf_n_local;
        if (use_quadric) {
            double dfdx = coeffs(1) + 2.0 * coeffs(3) * qx + coeffs(4) * qy;
            double dfdy = coeffs(2) + coeffs(4) * qx + 2.0 * coeffs(5) * qy;
            surf_n_local = Eigen::Vector3d(-dfdx, -dfdy, 1.0);
        } else {
            surf_n_local = Eigen::Vector3d(-coeffs(1), -coeffs(2), 1.0);
        }
        surf_n_local.normalize();
        result.fitted_normal = frame * surf_n_local;
        if (result.fitted_normal.dot(nominal_normal) < 0) {
            result.fitted_normal = -result.fitted_normal;
        }

        // Grazing check: is the nominal normal near-tangent to the fitted surface?
        double cos_angle = std::abs(nominal_normal.dot(result.fitted_normal));
        if (cos_angle < 1e-6) {
            // Definitively grazing — report and bail.
            result.grazing = true;
            result.failure_reason = "Nominal normal is tangent to fitted surface "
                                    "(cos_angle=" + std::to_string(cos_angle) + ")";
            return result;
        }
    }

    // ── Project p_nom along nominal_normal onto the fitted surface ─────
    // Line in local coords: P(t) = q_local + t * n_local
    // Surface: z = f(x, y)

    if (use_quadric) {
        double a0 = coeffs(0), a1 = coeffs(1), a2 = coeffs(2);
        double a3 = coeffs(3), a4 = coeffs(4), a5 = coeffs(5);

        double C = a0 + a1 * qx + a2 * qy + a3 * qx * qx + a4 * qx * qy + a5 * qy * qy;
        double B = a1 * nx + a2 * ny + 2.0 * a3 * qx * nx + a4 * (qx * ny + qy * nx) + 2.0 * a5 * qy * ny;
        double A_coeff = a3 * nx * nx + a4 * nx * ny + a5 * ny * ny;

        double p = B - nz;
        double q = C - qz;

        std::vector<double> t_solutions;

        if (std::abs(A_coeff) < 1e-14) {
            if (std::abs(p) > 1e-14) {
                t_solutions.push_back(-q / p);
            }
        } else {
            double disc = p * p - 4.0 * A_coeff * q;
            if (disc >= 0) {
                double sqrt_disc = std::sqrt(disc);
                t_solutions.push_back((-p + sqrt_disc) / (2.0 * A_coeff));
                if (disc > 1e-20) {
                    t_solutions.push_back((-p - sqrt_disc) / (2.0 * A_coeff));
                }
            }
        }

        result.num_intersections = static_cast<int>(t_solutions.size());

        if (t_solutions.empty()) {
            result.grazing = true;
            result.failure_reason = "Normal line does not intersect fitted surface (grazing/degenerate)";
            return result;
        }

        // Pick the solution closest to t=0 (closest to p_nom).
        double best_t = t_solutions[0];
        for (double t_val : t_solutions) {
            if (std::abs(t_val) < std::abs(best_t)) best_t = t_val;
        }

        Eigen::Vector3d hit_local = q_local + best_t * n_local;
        result.fitted_point = frame * hit_local + centroid;

        // Recompute surface normal at the actual hit point (may differ from query point).
        double hx = hit_local.x(), hy = hit_local.y();
        double dfdx = a1 + 2.0 * a3 * hx + a4 * hy;
        double dfdy = a2 + a4 * hx + 2.0 * a5 * hy;
        Eigen::Vector3d grad_local(-dfdx, -dfdy, 1.0);
        grad_local.normalize();
        result.fitted_normal = frame * grad_local;
        if (result.fitted_normal.dot(nominal_normal) < 0) {
            result.fitted_normal = -result.fitted_normal;
        }

    } else {
        // PCA plane: z = a0 + a1*x + a2*y
        double denom = nz - coeffs(1) * nx - coeffs(2) * ny;
        double numer = coeffs(0) + coeffs(1) * qx + coeffs(2) * qy - qz;

        if (std::abs(denom) < 1e-14) {
            result.num_intersections = 0;
            result.grazing = true;
            result.failure_reason = "Normal line parallel to fitted plane (grazing)";
            return result;
        }

        result.num_intersections = 1;
        double t = numer / denom;
        Eigen::Vector3d hit_local = q_local + t * n_local;
        result.fitted_point = frame * hit_local + centroid;
    }

    result.success = true;
    return result;
}

// ============================================================================
// Core projection: project a single point via MLS/PCA local surface fit.
// ============================================================================

static ProjectedPoint project_single_point(
        const Eigen::Vector3d& p_nom,
        const geometry::TriangleMesh& ref_mesh,
        const spatial::KdTree& meas_tree,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& aligned_meas,
        double effective_search_radius,
        const RPSProjectionSettings& settings) {

    ProjectedPoint proj;
    proj.ref_point = p_nom;
    proj.projected_point = p_nom; // default to nominal if projection fails
    proj.quality.search_radius = effective_search_radius;

    // ── Step 0: compute nominal surface normal from reference mesh ─────
    Eigen::Vector3d ref_normal;
    if (!reference_normal_at(p_nom, ref_mesh, ref_normal)) {
        proj.quality.status = ProjectionStatus::NO_NORMAL;
        proj.quality.warning = "Reference mesh has no valid (non-degenerate) "
                               "triangle near this point";
        return proj;
    }
    proj.ref_normal = ref_normal;

    // ── Step 1: collect measured neighbors within search_radius ────────
    std::vector<int> indices;
    std::vector<double> distances_sq;
    meas_tree.radius_search(p_nom, effective_search_radius, indices, distances_sq);

    proj.quality.neighborhood_size = static_cast<int>(indices.size());

    if (static_cast<int>(indices.size()) < settings.min_neighbors) {
        proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
        proj.quality.warning = "Only " + std::to_string(indices.size()) +
            " neighbors within search radius " +
            std::to_string(effective_search_radius) +
            "mm (need " + std::to_string(settings.min_neighbors) + ")";
        return proj;
    }

    // Check for hole: is the nearest point unreasonably far?
    double nearest_dist = std::numeric_limits<double>::max();
    for (auto dsq : distances_sq) {
        if (dsq < nearest_dist) nearest_dist = dsq;
    }
    nearest_dist = std::sqrt(nearest_dist);

    if (nearest_dist > settings.max_projection_distance) {
        proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
        proj.quality.distance = nearest_dist;
        proj.quality.warning = "Nearest measured point at " +
            std::to_string(nearest_dist) + "mm exceeds max_projection_distance " +
            std::to_string(settings.max_projection_distance) +
            "mm — query is over a hole";
        return proj;
    }

    // ── Step 2: extract neighborhood points ───────────────────────────
    Eigen::Index nn = static_cast<Eigen::Index>(indices.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> neighbor_pts(3, nn);
    for (Eigen::Index i = 0; i < nn; ++i) {
        neighbor_pts.col(i) = aligned_meas.col(indices[static_cast<std::size_t>(i)]);
    }

    // ── Step 2b: balanced-neighborhood validation ────────────────────
    // (Khameneifar & Feng, CAD 2017): the query point must lie within
    // the neighborhood's footprint on the local tangent plane. If the
    // query is at the edge or outside, the polynomial fit extrapolates
    // and produces unreliable correspondences.
    //
    // Practical test: project query and neighbors onto the PCA tangent
    // plane, check that query's distance from centroid is ≤ the 75th
    // percentile of neighbor distances from centroid. If the query is
    // farther than the bulk of the neighborhood, the patch is unbalanced.
    {
        Eigen::Vector3d nbr_centroid = neighbor_pts.rowwise().mean();
        Eigen::Vector3d q_to_c = p_nom - nbr_centroid;
        double q_dist = q_to_c.norm();

        // Collect neighbor distances from centroid.
        std::vector<double> nbr_dists;
        nbr_dists.reserve(static_cast<std::size_t>(nn));
        for (Eigen::Index i = 0; i < nn; ++i) {
            nbr_dists.push_back((neighbor_pts.col(i) - nbr_centroid).norm());
        }
        std::sort(nbr_dists.begin(), nbr_dists.end());
        // 75th percentile of neighbor-centroid distances.
        double p75 = nbr_dists[nbr_dists.size() * 3 / 4];

        // If query is farther from centroid than 2× the 75th percentile,
        // the neighborhood is unbalanced — the fit would extrapolate.
        if (p75 > 1e-12 && q_dist > 2.0 * p75) {
            proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
            proj.quality.warning = "Unbalanced neighborhood: query " +
                std::to_string(q_dist) + "mm from centroid vs p75=" +
                std::to_string(p75) + "mm — fit would extrapolate";
            return proj;
        }
    }

    // ── Step 3: fit local surface and project ─────────────────────────
    auto fit = fit_and_project(
        p_nom, ref_normal, neighbor_pts,
        settings.min_neighbors_quadric, settings.huber_k, settings.robust_iterations);

    if (!fit.success) {
        // Fit failed — classify the failure.
        if (fit.grazing) {
            proj.quality.status = ProjectionStatus::GRAZING;
        } else {
            proj.quality.status = ProjectionStatus::AMBIGUOUS_PATCH;
        }
        proj.quality.warning = fit.failure_reason;
        proj.quality.patch_roughness = fit.roughness;
        return proj;
    }

    // ── Step 4: check failure conditions on the successful fit ─────────

    proj.projected_point = fit.fitted_point;
    proj.quality.patch_roughness = fit.roughness;
    proj.quality.distance = (fit.fitted_point - p_nom).norm();

    // Check roughness.
    if (fit.roughness > settings.max_roughness) {
        proj.quality.status = ProjectionStatus::AMBIGUOUS_PATCH;
        proj.quality.warning = "Local fit roughness " + std::to_string(fit.roughness) +
            "mm exceeds threshold " + std::to_string(settings.max_roughness) + "mm";
        return proj;
    }

    // Check grazing: angle between nominal normal and fitted surface normal.
    double cos_angle = std::abs(ref_normal.dot(fit.fitted_normal));
    double angle_deg = std::acos(std::min(1.0, cos_angle)) * 180.0 / kPi;
    proj.quality.normal_angle_deg = angle_deg;

    if (cos_angle < settings.min_cos_grazing) {
        proj.quality.status = ProjectionStatus::GRAZING;
        proj.quality.warning = "Nominal normal at " + std::to_string(angle_deg) +
            " deg from fitted surface normal (threshold: " +
            std::to_string(std::acos(settings.min_cos_grazing) * 180.0 / kPi) + " deg)";
        return proj;
    }

    // Check multiple intersections.
    if (fit.num_intersections > 1) {
        proj.quality.warning = "Normal line intersects fitted surface at " +
            std::to_string(fit.num_intersections) + " points (closest selected)";
    }

    // Check projection distance.
    if (proj.quality.distance > settings.max_projection_distance) {
        proj.quality.status = ProjectionStatus::AMBIGUOUS_PATCH;
        proj.quality.warning = "Projection distance " +
            std::to_string(proj.quality.distance) +
            "mm exceeds limit " + std::to_string(settings.max_projection_distance) + "mm";
        return proj;
    }

    proj.quality.status = ProjectionStatus::OK;
    return proj;
}

// ============================================================================
// Public API — mesh overload
// ============================================================================

RPSProjectionResult project_rps_points(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
        const geometry::TriangleMesh& ref_mesh,
        const geometry::TriangleMesh& meas_mesh,
        const geometry::RigidTransform& pre_alignment,
        const RPSProjectionSettings& settings) {

    RPSProjectionResult result;
    Eigen::Index n = ref_points.cols();
    if (n == 0) {
        result.errors.push_back("No reference points to project");
        return result;
    }

    // Pre-align measured vertices into the reference frame.
    auto aligned_meas = pre_alignment.apply_cloud(meas_mesh.vertices());

    if (aligned_meas.cols() < settings.min_neighbors) {
        result.errors.push_back("Measured mesh has only " +
            std::to_string(aligned_meas.cols()) + " vertices — insufficient for projection");
        return result;
    }

    // Build KD-tree on aligned measured vertices.
    spatial::KdTree meas_tree(aligned_meas);

    // Compute effective search radius.
    double effective_radius = settings.search_radius;
    if (effective_radius <= 0) {
        effective_radius = compute_auto_search_radius(
            aligned_meas, meas_tree, settings.search_radius_factor);
    }

    // Project each reference point.
    result.projections.resize(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        result.projections[static_cast<std::size_t>(i)] = project_single_point(
            ref_points.col(i), ref_mesh, meas_tree, aligned_meas,
            effective_radius, settings);

        auto& proj = result.projections[static_cast<std::size_t>(i)];
        if (proj.quality.status == ProjectionStatus::OK) {
            result.num_ok++;
        } else {
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
        }
    }

    result.success = true;
    return result;
}

// ============================================================================
// Public API — point cloud overload
// ============================================================================

RPSProjectionResult project_rps_points(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
        const geometry::TriangleMesh& ref_mesh,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& meas_points,
        const geometry::RigidTransform& pre_alignment,
        const RPSProjectionSettings& settings) {

    RPSProjectionResult result;
    Eigen::Index n = ref_points.cols();
    if (n == 0) {
        result.errors.push_back("No reference points to project");
        return result;
    }

    // Pre-align measured points into the reference frame.
    auto aligned_meas = pre_alignment.apply_cloud(meas_points);

    if (aligned_meas.cols() < settings.min_neighbors) {
        result.errors.push_back("Measured cloud has only " +
            std::to_string(aligned_meas.cols()) + " points — insufficient for projection");
        return result;
    }

    // Build KD-tree on aligned measured points.
    spatial::KdTree meas_tree(aligned_meas);

    // Compute effective search radius.
    double effective_radius = settings.search_radius;
    if (effective_radius <= 0) {
        effective_radius = compute_auto_search_radius(
            aligned_meas, meas_tree, settings.search_radius_factor);
    }

    // Project each reference point — same MLS/PCA algorithm as mesh overload.
    result.projections.resize(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        result.projections[static_cast<std::size_t>(i)] = project_single_point(
            ref_points.col(i), ref_mesh, meas_tree, aligned_meas,
            effective_radius, settings);

        auto& proj = result.projections[static_cast<std::size_t>(i)];
        if (proj.quality.status == ProjectionStatus::OK) {
            result.num_ok++;
        } else {
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
        }
    }

    result.success = true;
    return result;
}

// ============================================================================
// Internal: project a single point with a pre-computed reference normal.
// Factored out of project_single_point so both TriangleMesh and
// ReferenceGeometry paths share the same MLS/PCA projection logic.
// ============================================================================

static ProjectedPoint project_single_point_with_normal(
        const Eigen::Vector3d& p_nom,
        const Eigen::Vector3d& ref_normal,
        const spatial::KdTree& meas_tree,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& aligned_meas,
        double effective_search_radius,
        const RPSProjectionSettings& settings) {

    ProjectedPoint proj;
    proj.ref_point = p_nom;
    proj.projected_point = p_nom;
    proj.ref_normal = ref_normal;
    proj.quality.search_radius = effective_search_radius;

    // ── Step 1: collect measured neighbors within search_radius ────────
    std::vector<int> indices;
    std::vector<double> distances_sq;
    meas_tree.radius_search(p_nom, effective_search_radius, indices, distances_sq);

    proj.quality.neighborhood_size = static_cast<int>(indices.size());

    if (static_cast<int>(indices.size()) < settings.min_neighbors) {
        proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
        proj.quality.warning = "Only " + std::to_string(indices.size()) +
            " neighbors within search radius " +
            std::to_string(effective_search_radius) +
            "mm (need " + std::to_string(settings.min_neighbors) + ")";
        return proj;
    }

    double nearest_dist = std::numeric_limits<double>::max();
    for (auto dsq : distances_sq) {
        if (dsq < nearest_dist) nearest_dist = dsq;
    }
    nearest_dist = std::sqrt(nearest_dist);

    if (nearest_dist > settings.max_projection_distance) {
        proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
        proj.quality.distance = nearest_dist;
        proj.quality.warning = "Nearest measured point at " +
            std::to_string(nearest_dist) + "mm exceeds max_projection_distance " +
            std::to_string(settings.max_projection_distance) + "mm";
        return proj;
    }

    Eigen::Index nn = static_cast<Eigen::Index>(indices.size());
    Eigen::Matrix<double, 3, Eigen::Dynamic> neighbor_pts(3, nn);
    for (Eigen::Index i = 0; i < nn; ++i) {
        neighbor_pts.col(i) = aligned_meas.col(indices[static_cast<std::size_t>(i)]);
    }

    // Balanced-neighborhood check.
    {
        Eigen::Vector3d nbr_centroid = neighbor_pts.rowwise().mean();
        double q_dist = (p_nom - nbr_centroid).norm();
        std::vector<double> nbr_dists;
        nbr_dists.reserve(static_cast<std::size_t>(nn));
        for (Eigen::Index i = 0; i < nn; ++i) {
            nbr_dists.push_back((neighbor_pts.col(i) - nbr_centroid).norm());
        }
        std::sort(nbr_dists.begin(), nbr_dists.end());
        double p75 = nbr_dists[nbr_dists.size() * 3 / 4];
        if (p75 > 1e-12 && q_dist > 2.0 * p75) {
            proj.quality.status = ProjectionStatus::INSUFFICIENT_DATA;
            proj.quality.warning = "Unbalanced neighborhood: query " +
                std::to_string(q_dist) + "mm from centroid vs p75=" +
                std::to_string(p75) + "mm";
            return proj;
        }
    }

    auto fit = fit_and_project(
        p_nom, ref_normal, neighbor_pts,
        settings.min_neighbors_quadric, settings.huber_k, settings.robust_iterations);

    if (!fit.success) {
        proj.quality.status = fit.grazing ? ProjectionStatus::GRAZING
                                          : ProjectionStatus::AMBIGUOUS_PATCH;
        proj.quality.warning = fit.failure_reason;
        proj.quality.patch_roughness = fit.roughness;
        return proj;
    }

    proj.projected_point = fit.fitted_point;
    proj.quality.patch_roughness = fit.roughness;
    proj.quality.distance = (fit.fitted_point - p_nom).norm();

    if (fit.roughness > settings.max_roughness) {
        proj.quality.status = ProjectionStatus::AMBIGUOUS_PATCH;
        proj.quality.warning = "Local fit roughness " + std::to_string(fit.roughness) +
            "mm exceeds threshold " + std::to_string(settings.max_roughness) + "mm";
        return proj;
    }

    double cos_angle = std::abs(ref_normal.dot(fit.fitted_normal));
    double angle_deg = std::acos(std::min(1.0, cos_angle)) * 180.0 / kPi;
    proj.quality.normal_angle_deg = angle_deg;

    if (cos_angle < settings.min_cos_grazing) {
        proj.quality.status = ProjectionStatus::GRAZING;
        proj.quality.warning = "Nominal normal at " + std::to_string(angle_deg) +
            " deg from fitted surface normal";
        return proj;
    }

    if (fit.num_intersections > 1) {
        proj.quality.warning = "Normal line intersects fitted surface at " +
            std::to_string(fit.num_intersections) + " points (closest selected)";
    }

    if (proj.quality.distance > settings.max_projection_distance) {
        proj.quality.status = ProjectionStatus::AMBIGUOUS_PATCH;
        proj.quality.warning = "Projection distance " +
            std::to_string(proj.quality.distance) +
            "mm exceeds limit " + std::to_string(settings.max_projection_distance) + "mm";
        return proj;
    }

    proj.quality.status = ProjectionStatus::OK;
    return proj;
}

// ============================================================================
// Public API — ReferenceGeometry overloads
// ============================================================================
// Uses ref_geom.normal_at() instead of brute-force closest-triangle.
// For CadReference this gives the true analytic B-rep normal.

RPSProjectionResult project_rps_points(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
        const geometry::ReferenceGeometry& ref_geom,
        const geometry::TriangleMesh& meas_mesh,
        const geometry::RigidTransform& pre_alignment,
        const RPSProjectionSettings& settings) {

    RPSProjectionResult result;
    Eigen::Index n = ref_points.cols();
    if (n == 0) {
        result.errors.push_back("No reference points to project");
        return result;
    }

    auto aligned_meas = pre_alignment.apply_cloud(meas_mesh.vertices());
    if (aligned_meas.cols() < settings.min_neighbors) {
        result.errors.push_back("Measured mesh has only " +
            std::to_string(aligned_meas.cols()) + " vertices — insufficient for projection");
        return result;
    }

    spatial::KdTree meas_tree(aligned_meas);
    double effective_radius = settings.search_radius;
    if (effective_radius <= 0) {
        effective_radius = compute_auto_search_radius(
            aligned_meas, meas_tree, settings.search_radius_factor);
    }

    result.projections.resize(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        Eigen::Vector3d p_nom = ref_points.col(i);

        // Get normal from ReferenceGeometry (true analytic for CAD).
        auto nr = ref_geom.normal_at(p_nom);
        if (nr.status != geometry::SurfaceQueryStatus::OK) {
            auto& proj = result.projections[static_cast<std::size_t>(i)];
            proj.ref_point = p_nom;
            proj.projected_point = p_nom;
            proj.quality.status = ProjectionStatus::NO_NORMAL;
            proj.quality.warning = "ReferenceGeometry: no valid normal at this point";
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
            continue;
        }

        result.projections[static_cast<std::size_t>(i)] =
            project_single_point_with_normal(
                p_nom, nr.unit_normal, meas_tree, aligned_meas,
                effective_radius, settings);

        auto& proj = result.projections[static_cast<std::size_t>(i)];
        if (proj.quality.status == ProjectionStatus::OK) {
            result.num_ok++;
        } else {
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
        }
    }

    result.success = true;
    return result;
}

RPSProjectionResult project_rps_points(
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& ref_points,
        const geometry::ReferenceGeometry& ref_geom,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& meas_points,
        const geometry::RigidTransform& pre_alignment,
        const RPSProjectionSettings& settings) {

    RPSProjectionResult result;
    Eigen::Index n = ref_points.cols();
    if (n == 0) {
        result.errors.push_back("No reference points to project");
        return result;
    }

    auto aligned_meas = pre_alignment.apply_cloud(meas_points);
    if (aligned_meas.cols() < settings.min_neighbors) {
        result.errors.push_back("Measured cloud has only " +
            std::to_string(aligned_meas.cols()) + " points — insufficient for projection");
        return result;
    }

    spatial::KdTree meas_tree(aligned_meas);
    double effective_radius = settings.search_radius;
    if (effective_radius <= 0) {
        effective_radius = compute_auto_search_radius(
            aligned_meas, meas_tree, settings.search_radius_factor);
    }

    result.projections.resize(static_cast<std::size_t>(n));
    for (Eigen::Index i = 0; i < n; ++i) {
        Eigen::Vector3d p_nom = ref_points.col(i);

        auto nr = ref_geom.normal_at(p_nom);
        if (nr.status != geometry::SurfaceQueryStatus::OK) {
            auto& proj = result.projections[static_cast<std::size_t>(i)];
            proj.ref_point = p_nom;
            proj.projected_point = p_nom;
            proj.quality.status = ProjectionStatus::NO_NORMAL;
            proj.quality.warning = "ReferenceGeometry: no valid normal at this point";
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
            continue;
        }

        result.projections[static_cast<std::size_t>(i)] =
            project_single_point_with_normal(
                p_nom, nr.unit_normal, meas_tree, aligned_meas,
                effective_radius, settings);

        auto& proj = result.projections[static_cast<std::size_t>(i)];
        if (proj.quality.status == ProjectionStatus::OK) {
            result.num_ok++;
        } else {
            result.num_failed++;
            result.warnings.push_back("Point " + std::to_string(i) + ": " +
                proj.quality.warning);
        }
    }

    result.success = true;
    return result;
}

} // namespace alignmesh::registration
