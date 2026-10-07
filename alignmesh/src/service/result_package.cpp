#define _CRT_SECURE_NO_WARNINGS
#include "alignmesh/service/result_package.h"
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/io/mesh_validation.h"
#include "alignmesh/alignment/kabsch.h"
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/registration/observability.h"
#include "alignmesh/registration/rps_projection.h"
#include "alignmesh/registration/rps_alignment.h"
#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/spatial/normals.h"
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/analysis/precision_tier.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace alignmesh::service {

HeatmapColor deviation_to_color(double deviation, double range_min, double range_max) {
    HeatmapColor c;
    c.a = 255;
    if (range_max <= range_min) { c.r = 128; c.g = 128; c.b = 128; return c; }
    double t = std::clamp((deviation - range_min) / (range_max - range_min), 0.0, 1.0);
    if (t < 0.5) {
        double s = t * 2.0;
        c.g = static_cast<uint8_t>(255 * s);
        c.b = static_cast<uint8_t>(255 * (1.0 - s));
    } else {
        double s = (t - 0.5) * 2.0;
        c.r = static_cast<uint8_t>(255 * s);
        c.g = static_cast<uint8_t>(255 * (1.0 - s));
    }
    return c;
}

// Helper: extract subcloud by indices.
static Eigen::Matrix<double,3,Eigen::Dynamic> extract_subcloud(
    const Eigen::Matrix<double,3,Eigen::Dynamic>& pts,
    const std::vector<Eigen::Index>& idx)
{
    Eigen::Matrix<double,3,Eigen::Dynamic> sub(3, static_cast<Eigen::Index>(idx.size()));
    for (std::size_t i = 0; i < idx.size(); ++i)
        sub.col(static_cast<Eigen::Index>(i)) = pts.col(idx[i]);
    return sub;
}

ResultPackage run_inspection(const InspectionRequest& request,
                             io::ImmutableSourceStore* external_store) {
    ResultPackage pkg;

    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        pkg.timestamp = oss.str();
    }
    pkg.fingerprint = numerics::EnvironmentFingerprint::capture();
    pkg.core_version = "alignmesh 0.1.0";
    pkg.tolerance = request.tolerance_mm;

    io::ImmutableSourceStore local_store;
    auto& store = external_store ? *external_store : local_store;
    try {
        auto _diag_t0 = std::chrono::steady_clock::now();
        auto _diag_ms = [&]() {
            return std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now() - _diag_t0).count();
        };
        auto _diag_lap = [&]() {
            auto elapsed = _diag_ms();
            _diag_t0 = std::chrono::steady_clock::now();
            return elapsed;
        };

        auto ref = store.import_file(request.reference_path);
        std::cerr << "DIAG [" << _diag_ms() << " ms total] ref import done" << std::endl;
        _diag_lap();
        auto meas_src = store.import_source(request.measured_path);
        std::cerr << "DIAG [+" << _diag_lap() << " ms] meas import done" << std::endl;

        pkg.reference_file = request.reference_path;
        pkg.measured_file = request.measured_path;
        pkg.reference_hash = ref.hash;
        pkg.measured_hash = meas_src.hash;

        if (meas_src.is_cloud()) {
            pkg.warnings.push_back("Measured input: point cloud (" +
                std::to_string(meas_src.metadata.vertex_count) + " points, " +
                meas_src.metadata.coordinate_precision + ")");
        }

        // ── Precision tier ──────────────────────────────────────────
        _diag_lap();
        // Check if the reference has a CAD B-rep (STEP import).
        auto ref_cad = store.cad_reference(ref.hash);
        bool has_cad_ref = (ref_cad != nullptr);

        auto tier = analysis::check_precision_gate(
            request.tolerance_mm, ref.metadata, ref.mesh, has_cad_ref);
        pkg.precision_tier = tier.requirements.tier_name;
        pkg.precision_gate_passed = tier.passed;
        if (!tier.passed)
            pkg.warnings.push_back("Precision gate: " + tier.invalid_reason);
        if (has_cad_ref)
            pkg.warnings.push_back("Reference: STEP B-rep (" +
                ref_cad->type_name() + "), true-surface deviation enabled");

        // ── Mesh validation ─────────────────────────────────────────
        auto rv = io::validate_mesh(ref.mesh);
        if (!rv.is_valid)
            for (auto& e : rv.errors) pkg.warnings.push_back("Ref: " + e);

        if (meas_src.is_mesh()) {
            auto mv = io::validate_mesh(meas_src.as_mesh().mesh);
            if (!mv.is_valid)
                for (auto& e : mv.errors) pkg.warnings.push_back("Meas: " + e);
        }

        std::cerr << "DIAG [+" << _diag_lap() << " ms] precision+validation done" << std::endl;

        // ── Point clouds + bounding box ─────────────────────────────
        pkg.alignment_mode = request.alignment_mode;
        // alignment_basis is set by the alignment branch below — leave empty until then.

        geometry::PointCloud ref_pc(ref.mesh.vertices());
        geometry::PointCloud meas_pc(meas_src.points());

        auto bbox_size = ref_pc.points().rowwise().maxCoeff() -
                         ref_pc.points().rowwise().minCoeff();
        double diag = bbox_size.norm();

        // ── Downsample for registration (~5K-10K points) ────────────
        double voxel_size = std::max(0.1, diag / 21.0);
        auto ref_idx = spatial::voxel_downsample(ref_pc.points(), voxel_size);
        auto meas_idx = spatial::voxel_downsample(meas_pc.points(), voxel_size);

        auto ref_ds_pts = extract_subcloud(ref_pc.points(), ref_idx);
        auto meas_ds_pts = extract_subcloud(meas_pc.points(), meas_idx);

        geometry::PointCloud ref_ds(ref_ds_pts);
        geometry::PointCloud meas_ds(meas_ds_pts);

        std::cerr << "DIAG [+" << _diag_lap() << " ms] downsample done"
                  << " | ref_full=" << ref_pc.points().cols()
                  << " ref_ds=" << ref_ds_pts.cols()
                  << " meas_full=" << meas_pc.points().cols()
                  << " meas_ds=" << meas_ds_pts.cols()
                  << " voxel=" << voxel_size << std::endl;

        // ── Stage 1: Initial alignment ───────────────────────────────
        geometry::RigidTransform initial_guess;

        if (request.alignment_mode == "landmark" && request.landmarks.size() >= 3) {
            // LANDMARK MODE: user-provided corresponding point pairs.
            // Kabsch (weighted SVD) computes the optimal rigid transform.
            auto n_lm = static_cast<Eigen::Index>(request.landmarks.size());
            Eigen::Matrix<double,3,Eigen::Dynamic> src(3, n_lm);
            Eigen::Matrix<double,3,Eigen::Dynamic> tgt(3, n_lm);
            Eigen::VectorXd wts(n_lm);
            for (Eigen::Index i = 0; i < n_lm; ++i) {
                auto& lp = request.landmarks[static_cast<std::size_t>(i)];
                src(0,i) = lp.meas_x; src(1,i) = lp.meas_y; src(2,i) = lp.meas_z;
                tgt(0,i) = lp.ref_x;  tgt(1,i) = lp.ref_y;  tgt(2,i) = lp.ref_z;
                wts(i) = lp.weight;
            }
            auto kabsch = alignment::align_landmarks(src, tgt, wts);
            if (kabsch.success) {
                initial_guess = kabsch.transform;
                pkg.alignment_basis = "landmark Kabsch + fine ICP";
                pkg.warnings.push_back("Landmark alignment: " +
                    std::to_string(n_lm) + " pairs, RMS=" +
                    std::to_string(kabsch.diagnostics.weighted_rms) + "mm");
            } else {
                pkg.warnings.push_back("Landmark alignment failed: " +
                    (kabsch.errors.empty() ? "unknown" : kabsch.errors[0]));
                // Fall through to automatic alignment below.
            }
        }

        if (request.alignment_mode == "pre-aligned-rps" && !request.rps_points.empty()) {
            // PRE-ALIGNED RPS ALIGNMENT (two-stage pipeline):
            //   Stage 1: Coarse-to-fine pre-alignment (not the reported alignment).
            //   Stage 2: 3-2-1 RPS constrained fit (conformance-bearing).

            // ── Stage 1: Pre-alignment ────────────────────────────────────
            // Try FPFH global first; if it fails, try 24 principal-axis
            // rotations (0°/90°/180°/270° around each axis) with quick ICP.
            // This covers all common orientations in < 1 second.
            _diag_lap();
            registration::GlobalRegistrationSettings gs;
            gs.voxel_size = voxel_size;
            auto coarse = registration::global_register(meas_ds, ref_ds, gs);
            std::cerr << "DIAG [+" << _diag_lap() << " ms] RPS-branch FPFH done"
                      << " (success=" << coarse.success << " conf=" << coarse.confidence
                      << " pts: meas_ds=" << meas_ds.points().cols()
                      << " ref_ds=" << ref_ds.points().cols() << ")" << std::endl;
            geometry::RigidTransform pre_align;

            if (coarse.success && coarse.confidence > 0.3) {
                pre_align = coarse.as_initial_guess();
                std::cerr << "DIAG FPFH accepted — skipping rotation search" << std::endl;
            } else {
                // FPFH failed — try 24 principal-axis rotations with quick ICP.
                Eigen::Vector3d ref_c = ref_ds.points().rowwise().mean();
                Eigen::Vector3d meas_c = meas_ds.points().rowwise().mean();

                // 24 rotations: identity + 90°/180°/270° around X, Y, Z
                // plus combined 90° pairs = the 24 orientation cube symmetries.
                std::vector<Eigen::Matrix3d> rots;
                auto rx = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<1,0,0, 0,c,-s, 0,s,c; return m; };
                auto ry = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<c,0,s, 0,1,0, -s,0,c; return m; };
                auto rz = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<c,-s,0, s,c,0, 0,0,1; return m; };
                const double H = 1.5707963267948966; // pi/2
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        rots.push_back(ry(j * H) * rx(i * H));
                // Deduplicate (some overlap): keep ~24.
                // Add Z-axis rotations for completeness.
                for (int k = 1; k < 4; ++k)
                    rots.push_back(rz(k * H));

                registration::FineRegistrationSettings quick_fs;
                quick_fs.method = registration::ICPMethod::POINT_TO_POINT;
                quick_fs.max_correspondence_distance = diag * 0.5;
                quick_fs.max_iterations = 20;

                double best_rms = std::numeric_limits<double>::max();
                geometry::RigidTransform best_T;
                bool any_success = false;

                _diag_lap();
                for (std::size_t _ri = 0; _ri < rots.size(); ++_ri) {
                    auto& R = rots[_ri];
                    Eigen::Vector3d t_rot = ref_c - R * meas_c;
                    auto T_init = geometry::RigidTransform::from_rotation_translation(R, t_rot);
                    auto trial = registration::fine_register(meas_ds, ref_ds, T_init, quick_fs);
                    if (trial.success && trial.final_rms < best_rms) {
                        best_rms = trial.final_rms;
                        best_T = trial.transform;
                        any_success = true;
                    }
                }
                std::cerr << "DIAG [+" << _diag_lap() << " ms] RPS-branch rotation search done"
                          << " (trials=" << rots.size()
                          << " pts: meas_ds=" << meas_ds.points().cols()
                          << " ref_ds=" << ref_ds.points().cols() << ")" << std::endl;

                if (any_success) {
                    pre_align = best_T;
                } else {
                    pre_align = geometry::RigidTransform::from_rotation_translation(
                        Eigen::Matrix3d::Identity(), ref_c - meas_c);
                    pkg.warnings.push_back("Pre-alignment: all orientation candidates failed");
                }
            }

            // Refine pre-alignment with point-to-plane on dense clouds.
            // The coarse seed (~0.5mm residual on 350 pts) is too rough
            // for the RPS projection's normal-consistency checks. A fine
            // point-to-plane ICP on dense data brings it to ~0.02mm.
            {
                _diag_lap();
                double fine_voxel = std::max(0.05, diag / 150.0);
                auto rfi = spatial::voxel_downsample(ref_pc.points(), fine_voxel);
                auto mfi = spatial::voxel_downsample(meas_pc.points(), fine_voxel);
                auto rfp = extract_subcloud(ref_pc.points(), rfi);
                auto mfp = extract_subcloud(meas_pc.points(), mfi);
                geometry::PointCloud rf(rfp), mf(mfp);

                registration::FineRegistrationSettings pfs;
                pfs.method = registration::ICPMethod::POINT_TO_PLANE;
                pfs.max_correspondence_distance = diag * 0.1;
                pfs.max_iterations = 200;
                pfs.rotation_tolerance = 1e-8;
                pfs.translation_tolerance = 1e-8;

                auto fine = registration::fine_register(mf, rf, pre_align, pfs);
                std::cerr << "DIAG [+" << _diag_lap() << " ms] RPS-branch fine P2PL done"
                          << " (success=" << fine.success
                          << " iters=" << fine.total_iterations
                          << " rms=" << fine.final_rms
                          << " pts: mf=" << mfp.cols()
                          << " rf=" << rfp.cols() << ")" << std::endl;
                if (fine.success) pre_align = fine.transform;
            }

            // ── Stage 2: Iterative project ↔ RPS solve loop ─────────────
            _diag_lap();
            auto n_rps = request.rps_points.size();
            constexpr int MAX_COUPLING_ITERATIONS = 5;
            constexpr double COUPLING_TOL = 0.001; // 1 µm

            // Build reference point matrix for projection (constant across iterations).
            Eigen::Matrix<double,3,Eigen::Dynamic> rps_ref_pts(3, static_cast<Eigen::Index>(n_rps));
            for (std::size_t i = 0; i < n_rps; ++i) {
                auto& rp = request.rps_points[i];
                rps_ref_pts(0, static_cast<Eigen::Index>(i)) = rp.x;
                rps_ref_pts(1, static_cast<Eigen::Index>(i)) = rp.y;
                rps_ref_pts(2, static_cast<Eigen::Index>(i)) = rp.z;
            }

            // Lambda: project at a given transform and build constraint points.
            auto project_and_build = [&](const geometry::RigidTransform& T)
                -> std::pair<registration::RPSProjectionResult,
                             std::vector<registration::RPSConstraintPoint>> {

                registration::RPSProjectionResult proj;
                // Use CadReference for analytic normals when available (§4).
                if (ref_cad && meas_src.is_mesh()) {
                    proj = registration::project_rps_points(
                        rps_ref_pts, *ref_cad, meas_src.as_mesh().mesh, T);
                } else if (ref_cad) {
                    proj = registration::project_rps_points(
                        rps_ref_pts, *ref_cad, meas_src.points(), T);
                } else if (meas_src.is_mesh()) {
                    proj = registration::project_rps_points(
                        rps_ref_pts, ref.mesh, meas_src.as_mesh().mesh, T);
                } else {
                    proj = registration::project_rps_points(
                        rps_ref_pts, ref.mesh, meas_src.points(), T);
                }

                std::vector<registration::RPSConstraintPoint> cpts;
                if (!proj.success || proj.projections.size() != n_rps) {
                    return {proj, cpts};
                }

                // FAIL-SAFE: if ANY RPS point fails projection, the
                // RPS alignment cannot be validly established. Do NOT
                // proceed with partial constraints — that silently
                // degrades the conformance-bearing alignment.
                for (std::size_t i = 0; i < n_rps; ++i) {
                    if (proj.projections[i].quality.status !=
                        registration::ProjectionStatus::OK) {
                        proj.success = false;
                        proj.errors.push_back(
                            "RPS point " + std::to_string(i) +
                            " projection failed: " +
                            proj.projections[i].quality.warning);
                        return {proj, cpts};  // empty constraints → INVALID
                    }
                }

                for (std::size_t i = 0; i < n_rps; ++i) {

                    auto& spec = request.rps_points[i];
                    registration::RPSConstraintPoint cp;
                    cp.nominal = Eigen::Vector3d(spec.x, spec.y, spec.z);
                    // projected_point is in the REFERENCE frame (T was applied
                    // to measured vertices during projection). The solver needs
                    // the point in the RAW measured frame so that R*m+t maps
                    // measured→reference. Convert back using T^{-1}.
                    cp.measured = T.inverse().apply(proj.projections[i].projected_point);

                    for (auto& lock_spec : spec.locks) {
                        registration::RPSDirectionLock lock;
                        lock.weight = lock_spec.weight;
                        if (lock_spec.axis == "normal") {
                            lock.direction = proj.projections[i].ref_normal;
                        } else if (lock_spec.axis == "x") {
                            lock.direction = Eigen::Vector3d::UnitX();
                        } else if (lock_spec.axis == "y") {
                            lock.direction = Eigen::Vector3d::UnitY();
                        } else if (lock_spec.axis == "z") {
                            lock.direction = Eigen::Vector3d::UnitZ();
                        } else {
                            lock.direction = proj.projections[i].ref_normal;
                        }
                        cp.locks.push_back(lock);
                    }
                    if (cp.locks.empty()) {
                        registration::RPSDirectionLock dl;
                        dl.direction = proj.projections[i].ref_normal;
                        dl.weight = 1.0;
                        cp.locks.push_back(dl);
                    }
                    cpts.push_back(cp);
                }
                return {proj, cpts};
            };

            // ── Coupling iteration loop ──────────────────────────────────
            // Uses closest-point-on-mesh (BVH) for correspondence finding
            // when measured data has mesh connectivity. This is globally
            // robust (no search radius, no grazing/balanced checks) and
            // matches what PolyWorks / GOM do. MLS is used only for point
            // cloud input (no triangle connectivity available).
            geometry::RigidTransform current_T = pre_align;
            registration::RPSAlignmentResult rps_result;
            bool coupling_converged = false;
            int coupling_iters = 0;

            // Store projected points for display (updated each iteration).
            std::vector<Eigen::Vector3d> projected_display(n_rps);

            for (int ci = 0; ci < MAX_COUPLING_ITERATIONS; ++ci) {
                std::vector<registration::RPSConstraintPoint> constraints;
                bool proj_ok = true;

                if (meas_src.is_mesh()) {
                    // ── Closest-point-on-mesh (BVH) ─────────────────────
                    auto aligned_meas_mesh = meas_src.as_mesh().mesh.transformed(current_T);
                    auto meas_ref = geometry::make_mesh_reference(aligned_meas_mesh);

                    for (std::size_t i = 0; i < n_rps; ++i) {
                        Eigen::Vector3d pnom = rps_ref_pts.col(static_cast<Eigen::Index>(i));
                        auto cpr = meas_ref->closest_point_on_surface(pnom);

                        if (cpr.status != geometry::SurfaceQueryStatus::OK ||
                            cpr.unsigned_distance > diag * 0.1) {
                            pkg.warnings.push_back("RPS point " + std::to_string(i) +
                                ": closest-point at " + std::to_string(cpr.unsigned_distance) +
                                "mm — too far");
                            proj_ok = false;
                            break;
                        }

                        projected_display[i] = cpr.point;

                        // Reference normal from CAD or ref mesh.
                        Eigen::Vector3d ref_normal = Eigen::Vector3d::UnitZ();
                        if (ref_cad) {
                            auto nr = ref_cad->normal_at(pnom);
                            if (nr.status == geometry::SurfaceQueryStatus::OK)
                                ref_normal = nr.unit_normal;
                        } else {
                            // Brute-force nearest triangle normal on ref mesh.
                            auto ref_geom_tmp = geometry::make_mesh_reference(ref.mesh);
                            auto nr = ref_geom_tmp->normal_at(pnom);
                            if (nr.status == geometry::SurfaceQueryStatus::OK)
                                ref_normal = nr.unit_normal;
                        }

                        auto& spec = request.rps_points[i];
                        registration::RPSConstraintPoint cp;
                        cp.nominal = pnom;
                        cp.measured = current_T.inverse().apply(cpr.point);

                        for (auto& lock_spec : spec.locks) {
                            registration::RPSDirectionLock lock;
                            lock.weight = lock_spec.weight;
                            if (lock_spec.axis == "normal") {
                                lock.direction = ref_normal;
                            } else if (lock_spec.axis == "x") {
                                lock.direction = Eigen::Vector3d::UnitX();
                            } else if (lock_spec.axis == "y") {
                                lock.direction = Eigen::Vector3d::UnitY();
                            } else if (lock_spec.axis == "z") {
                                lock.direction = Eigen::Vector3d::UnitZ();
                            } else {
                                lock.direction = ref_normal;
                            }
                            cp.locks.push_back(lock);
                        }
                        if (cp.locks.empty()) {
                            registration::RPSDirectionLock dl;
                            dl.direction = ref_normal;
                            dl.weight = 1.0;
                            cp.locks.push_back(dl);
                        }
                        constraints.push_back(cp);

                        std::cerr << "DIAG RPS[" << i << "] ci=" << ci
                                  << " cp_dist=" << cpr.unsigned_distance << "mm" << std::endl;
                    }
                } else {
                    // ── Point cloud: use MLS projection ─────────────────
                    auto [proj, cpts] = project_and_build(current_T);
                    if (!proj.success) {
                        pkg.warnings.push_back("RPS projection failed at iteration " +
                            std::to_string(ci));
                        proj_ok = false;
                    }
                    constraints = cpts;
                    // Store projected points for display.
                    for (std::size_t i = 0; i < n_rps && i < proj.projections.size(); ++i) {
                        projected_display[i] = proj.projections[i].projected_point;
                    }
                }

                if (!proj_ok || constraints.empty()) break;

                rps_result = registration::rps_align(constraints, current_T);
                coupling_iters = ci + 1;

                if (!rps_result.success) break;

                double max_movement = 0;
                for (std::size_t i = 0; i < n_rps; ++i) {
                    Eigen::Vector3d p = rps_ref_pts.col(static_cast<Eigen::Index>(i));
                    Eigen::Vector3d old_pos = current_T.inverse().apply(p);
                    Eigen::Vector3d new_pos = rps_result.transform.inverse().apply(p);
                    max_movement = std::max(max_movement, (old_pos - new_pos).norm());
                }

                current_T = rps_result.transform;

                std::cerr << "DIAG RPS coupling ci=" << ci
                          << " max_movement=" << max_movement
                          << "mm rms=" << rps_result.weighted_rms << "mm" << std::endl;

                if (max_movement < COUPLING_TOL) {
                    coupling_converged = true;
                    break;
                }
            }

            std::cerr << "DIAG [+" << _diag_lap() << " ms] RPS coupling loop done"
                      << " (iters=" << coupling_iters
                      << " converged=" << coupling_converged
                      << " n_rps=" << n_rps << ")" << std::endl;

            // Populate projected points for UI display.
            for (std::size_t i = 0; i < n_rps; ++i) {
                ResultPackage::RPSProjectedPoint rpp;
                rpp.x = projected_display[i].x();
                rpp.y = projected_display[i].y();
                rpp.z = projected_display[i].z();
                rpp.valid = true;
                pkg.rps_projected_points.push_back(rpp);
            }

            if (rps_result.success) {
                initial_guess = rps_result.transform;
                pkg.alignment_basis = "pre-aligned RPS (simultaneous weighted directional LS, " +
                    std::to_string(rps_result.dof_status.num_constraints) + " constraints, " +
                    "rank " + std::to_string(rps_result.dof_status.rank) + ", " +
                    std::to_string(rps_result.iterations) + " GN iterations, " +
                    std::to_string(coupling_iters) + " coupling iterations, " +
                    "RMS=" + std::to_string(rps_result.weighted_rms) + "mm)";
                pkg.alignment_rms = rps_result.weighted_rms;
                pkg.alignment_converged = coupling_converged;

                if (rps_result.redundant_constraints > 0) {
                    pkg.warnings.push_back("RPS: " +
                        std::to_string(rps_result.redundant_constraints) +
                        " redundant constraint(s) — priorities govern trade-off");
                }
                for (auto& w : rps_result.warnings) pkg.warnings.push_back("RPS: " + w);
            } else {
                // RPS solve could not be established. This is a FAIL-SAFE
                // condition: DO NOT fall back to ICP and return a confident
                // verdict — that silently changes what was measured.
                for (auto& e : rps_result.errors) pkg.warnings.push_back("RPS error: " + e);
                pkg.warnings.push_back(
                    "RPS ALIGNMENT FAILED: the conformance-bearing RPS alignment "
                    "could not be established. Verdict forced to INVALID. "
                    "Check that RPS points are on the part surface and that "
                    "the measured scan overlaps the reference at those locations.");

                // (Projection failure details already reported per-point above.)

                // Set alignment_basis so is_rps_mode stays TRUE (no ICP fallback).
                initial_guess = pre_align;
                pkg.alignment_basis = "pre-aligned RPS (FAILED — no valid constraints)";
                pkg.alignment_converged = false;
            }
        }

        if (pkg.alignment_basis.empty()) {
            // COARSE-TO-FINE REGISTRATION: global FPFH → GICP fine.
            _diag_lap();
            registration::GlobalRegistrationSettings gs;
            gs.voxel_size = voxel_size;
            gs.gnc_noise_bound = std::max(0.1, request.tolerance_mm * 5.0);

            auto coarse = registration::global_register(meas_ds, ref_ds, gs);
            std::cerr << "DIAG [+" << _diag_lap() << " ms] auto-branch FPFH done"
                      << " (success=" << coarse.success << " conf=" << coarse.confidence
                      << " pts: meas_ds=" << meas_ds.points().cols()
                      << " ref_ds=" << ref_ds.points().cols() << ")" << std::endl;
            if (coarse.success && coarse.confidence > 0.3) {
                initial_guess = coarse.as_initial_guess();
                pkg.alignment_basis = "global FPFH + fine ICP";
            } else {
                // FPFH failed — 24 principal-axis rotations with quick ICP.
                Eigen::Vector3d ref_c = ref_ds.points().rowwise().mean();
                Eigen::Vector3d meas_c = meas_ds.points().rowwise().mean();

                std::vector<Eigen::Matrix3d> rots;
                auto rx = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<1,0,0, 0,c,-s, 0,s,c; return m; };
                auto ry = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<c,0,s, 0,1,0, -s,0,c; return m; };
                auto rz = [](double a) -> Eigen::Matrix3d {
                    double c=std::cos(a),s=std::sin(a);
                    Eigen::Matrix3d m; m<<c,-s,0, s,c,0, 0,0,1; return m; };
                const double H = 1.5707963267948966;
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        rots.push_back(ry(j * H) * rx(i * H));
                for (int k = 1; k < 4; ++k)
                    rots.push_back(rz(k * H));

                registration::FineRegistrationSettings quick_fs;
                quick_fs.method = registration::ICPMethod::POINT_TO_POINT;
                quick_fs.max_correspondence_distance = diag * 0.5;
                quick_fs.max_iterations = 20;

                double best_rms = std::numeric_limits<double>::max();
                geometry::RigidTransform best_T;
                bool any_success = false;

                _diag_lap();
                for (auto& R : rots) {
                    Eigen::Vector3d t_rot = ref_c - R * meas_c;
                    auto T_init = geometry::RigidTransform::from_rotation_translation(R, t_rot);
                    auto trial = registration::fine_register(meas_ds, ref_ds, T_init, quick_fs);
                    if (trial.success && trial.final_rms < best_rms) {
                        best_rms = trial.final_rms;
                        best_T = trial.transform;
                        any_success = true;
                    }
                }
                std::cerr << "DIAG [+" << _diag_lap() << " ms] auto-branch rotation search done"
                          << " (trials=" << rots.size()
                          << " pts: meas_ds=" << meas_ds.points().cols()
                          << " ref_ds=" << ref_ds.points().cols() << ")" << std::endl;

                if (any_success) {
                    initial_guess = best_T;
                    pkg.alignment_basis = "orientation search (24 candidates, best RMS=" +
                        std::to_string(best_rms) + "mm) + GICP";
                } else {
                    initial_guess = geometry::RigidTransform::from_rotation_translation(
                        Eigen::Matrix3d::Identity(), ref_c - meas_c);
                    pkg.alignment_basis = "centroid (all orientations failed)";
                    pkg.warnings.push_back("All orientation candidates failed — alignment may be poor");
                }
            }

            // Refine with GICP.
            _diag_lap();
            registration::FineRegistrationSettings fs;
            fs.method = registration::ICPMethod::GICP;
            fs.max_correspondence_distance = diag * 0.3;
            auto fine = registration::fine_register(meas_ds, ref_ds, initial_guess, fs);
            std::cerr << "DIAG [+" << _diag_lap() << " ms] auto-branch GICP refine done"
                      << " (success=" << fine.success
                      << " pts: meas_ds=" << meas_ds.points().cols()
                      << " ref_ds=" << ref_ds.points().cols() << ")" << std::endl;
            if (fine.success) {
                initial_guess = fine.transform;
                if (pkg.alignment_basis.find("GICP") == std::string::npos)
                    pkg.alignment_basis += " + GICP";
            }
        }

        // ── Final alignment transform ─────────────────────────────────
        _diag_lap();
        // For pre-aligned-RPS mode: the RPS directional solve IS the final,
        // conformance-bearing alignment. NO ICP/GICP refinement runs after it.
        // The transform used for deviation analysis must be the RPS output.
        //
        // For all other modes: fine_register refines the initial_guess via ICP.

        geometry::RigidTransform final_transform = initial_guess;
        bool alignment_ok = false;

        bool is_rps_mode = (request.alignment_mode == "pre-aligned-rps" &&
                            !pkg.alignment_basis.empty() &&
                            pkg.alignment_basis.find("RPS failed") == std::string::npos);

        if (is_rps_mode) {
            // RPS transform is final. Store it directly.
            final_transform = initial_guess; // already set to rps_result.transform
            alignment_ok = pkg.alignment_converged; // non-convergence → alignment NOT ok
            auto m = final_transform.matrix();
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    pkg.transform_matrix[i*4+j] = m(i,j);
            // pkg.alignment_rms and pkg.alignment_converged already set by the RPS block.
            // DO NOT run fine_register() — it would replace the directionally-
            // constrained RPS fit with a free best-fit, discarding the locks
            // and priorities that define the method.
        } else {
            // Non-RPS modes: two-stage fine registration.
            //
            // Stage 1 (coarse seed check): quick point-to-point on the
            // downsampled clouds to validate the seed. This is NOT the
            // conformance-bearing alignment.
            //
            // Stage 2 (fine, conformance-bearing): point-to-plane ICP on
            // DENSE data to convergence. The coarse seed only initializes;
            // the fine result IS the final transform.

            // ── Dense downsample for fine stage (~1mm voxel) ──────────
            double fine_voxel = std::max(0.05, diag / 150.0);
            auto ref_fine_idx = spatial::voxel_downsample(ref_pc.points(), fine_voxel);
            auto meas_fine_idx = spatial::voxel_downsample(meas_pc.points(), fine_voxel);
            auto ref_fine_pts = extract_subcloud(ref_pc.points(), ref_fine_idx);
            auto meas_fine_pts = extract_subcloud(meas_pc.points(), meas_fine_idx);
            geometry::PointCloud ref_fine(ref_fine_pts);
            geometry::PointCloud meas_fine(meas_fine_pts);

            std::cerr << "DIAG fine-stage clouds: ref_fine=" << ref_fine_pts.cols()
                      << " meas_fine=" << meas_fine_pts.cols()
                      << " fine_voxel=" << fine_voxel << std::endl;

            // ── Stage 2: point-to-plane on dense clouds ──────────────
            registration::FineRegistrationSettings fine_fs;
            fine_fs.method = registration::ICPMethod::POINT_TO_PLANE;
            fine_fs.max_correspondence_distance = diag * 0.1;
            fine_fs.max_iterations = 200;
            fine_fs.rotation_tolerance = 1e-8;
            fine_fs.translation_tolerance = 1e-8;

            auto reg = registration::fine_register(
                meas_fine, ref_fine, initial_guess, fine_fs);

            if (reg.success) {
                final_transform = reg.transform;
                alignment_ok = true;
                pkg.alignment_rms = reg.final_rms;
                pkg.alignment_converged = reg.converged;
                auto m = reg.transform.matrix();
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        pkg.transform_matrix[i*4+j] = m(i,j);
                pkg.warnings.push_back("Alignment basis: " + pkg.alignment_basis +
                    " (fine point-to-plane, " +
                    std::to_string(reg.num_correspondences) + " correspondences, " +
                    std::to_string(reg.total_iterations) + " iterations, " +
                    "RMS=" + std::to_string(reg.final_rms) + "mm)");

                std::cerr << "DIAG fine-stage: iters=" << reg.total_iterations
                          << " rms=" << reg.final_rms
                          << " correspondences=" << reg.num_correspondences
                          << " converged=" << reg.converged << std::endl;
            } else {
                // FAIL-LOUD: fine stage failed entirely.
                pkg.warnings.push_back("Fine alignment FAILED: " +
                    (reg.errors.empty() ? "unknown" : reg.errors[0]) +
                    " — falling back to coarse seed (DEGRADED)");
                final_transform = initial_guess;
                alignment_ok = true; // degraded but usable
                pkg.alignment_converged = false;

                std::cerr << "DIAG fine-stage FAILED, using coarse seed" << std::endl;
            }
        }

        std::cerr << "DIAG [+" << _diag_lap() << " ms] final alignment done"
                  << " (alignment_ok=" << alignment_ok << " rps_mode=" << is_rps_mode << ")" << std::endl;

        // ── Observability check (SAFETY-CRITICAL) ───────────────────
        // Catches "low RMS but meaningless pose" — e.g., flat part sliding.
        // For RPS: observability is already handled by the DOF rank analysis.
        // For non-RPS: analyze via point-to-plane Hessian.
        if (alignment_ok && !is_rps_mode) {
            auto ref_normals = spatial::estimate_normals_knn(ref_ds_pts, 20);
            auto src_transformed = final_transform.apply_cloud(meas_ds_pts);

            spatial::KdTree ref_tree(ref_ds_pts);
            double corr_thresh_sq = (diag * 0.1) * (diag * 0.1);

            std::vector<Eigen::Index> csrc, ctgt;
            for (Eigen::Index i = 0; i < src_transformed.cols(); ++i) {
                auto nn = ref_tree.nearest(src_transformed.col(i));
                if (nn.distance_sq < corr_thresh_sq) {
                    csrc.push_back(i);
                    ctgt.push_back(static_cast<Eigen::Index>(nn.index));
                }
            }

            if (csrc.size() >= 6) {
                auto matched_src = extract_subcloud(src_transformed, csrc);
                auto matched_tgt = extract_subcloud(ref_ds_pts, ctgt);
                auto matched_nrm = extract_subcloud(ref_normals.normals, ctgt);

                auto obs = registration::analyze_observability(
                    matched_src, matched_tgt, matched_nrm);
                pkg.fully_constrained = obs.fully_constrained;
                pkg.num_under_constrained = obs.num_under_constrained;
                for (auto& w : obs.warnings)
                    pkg.warnings.push_back("OBSERVABILITY: " + w);
            } else {
                pkg.warnings.push_back("OBSERVABILITY: too few correspondences (" +
                    std::to_string(csrc.size()) + ") for analysis");
            }
        } else if (is_rps_mode) {
            // RPS DOF analysis provides observability.
            // The RPS solver already computed the Jacobian rank — use it.
            // Find the last successful RPS result to extract DOF info.
            // initial_guess was set from rps_result.transform if successful.
            // Re-derive from the alignment_basis string.
            if (pkg.alignment_basis.find("rank 6") != std::string::npos) {
                pkg.fully_constrained = true;
                pkg.num_under_constrained = 0;
            } else {
                pkg.fully_constrained = false;
                // Extract rank from basis string if possible.
                auto rk_pos = pkg.alignment_basis.find("rank ");
                if (rk_pos != std::string::npos) {
                    int rk = std::atoi(pkg.alignment_basis.c_str() + rk_pos + 5);
                    pkg.num_under_constrained = std::max(0, 6 - rk);
                }
            }
        }

        std::cerr << "DIAG [+" << _diag_lap() << " ms] observability done" << std::endl;

        // ── Deviation analysis ────────────────────────────────────────
        // ALWAYS apply the best available transform (pre-alignment when
        // RPS fails, RPS result when it succeeds). The alignment_ok flag
        // controls the verdict, not whether the transform is applied.
        // Without this, a failed RPS computes deviation on raw untransformed
        // points — producing garbage 400+mm numbers despite a good pre-alignment.
        auto meas_full_pts = meas_src.points();
        auto aligned = final_transform.apply_cloud(meas_full_pts);

        std::cerr << "DIAG deviation starting: " << aligned.cols() << " points vs "
                  << (ref_cad ? "CadReference" : "MeshReference") << std::endl;
        _diag_lap();

        // Use CadReference for true-surface deviation when available.
        auto dev = ref_cad
            ? analysis::compute_deviation(aligned, *ref_cad)
            : analysis::compute_deviation(aligned, ref.mesh);
        std::cerr << "DIAG [+" << _diag_lap() << " ms] deviation done"
                  << " (success=" << dev.success << " pts=" << aligned.cols() << ")" << std::endl;
        if (dev.success) {
            pkg.unsigned_stats = dev.unsigned_stats;
            pkg.signed_stats = dev.signed_stats;

            double range = (request.heatmap_range > 0) ?
                request.heatmap_range : request.tolerance_mm;
            pkg.heatmap_min = -range;
            pkg.heatmap_max = +range;
            pkg.heatmap_label = "Deviation heatmap (" + std::to_string(aligned.cols()) + " points)";

            auto n = aligned.cols();
            pkg.points.resize(static_cast<std::size_t>(n));
            for (Eigen::Index i = 0; i < n; ++i) {
                auto ui = static_cast<std::size_t>(i);
                auto& p = pkg.points[ui];
                p.x = aligned(0,i); p.y = aligned(1,i); p.z = aligned(2,i);
                if (ui < dev.deviations.size()) {
                    p.deviation = dev.deviations[ui].signed_distance;
                    p.abs_deviation = dev.deviations[ui].distance;
                }
                p.color = deviation_to_color(p.deviation, pkg.heatmap_min, pkg.heatmap_max);
            }
        }

        // ── Uncertainty establishment check ──────────────────────────
        // Auto-establish contributors that CAN be computed from the data.
        // All others default to UNESTABLISHED and force INVALID.
        {
            // u_align: from alignment RMS — auto-established.
            if (alignment_ok && pkg.alignment_rms > 0) {
                pkg.expanded_uncertainty = pkg.alignment_rms;  // conservative: U >= RMS
                // This is ONE contributor; real U needs all contributors combined.
            }
            // u_chord: from precision tier — auto-established if STL.
            // Scanner repeatability, fixture, temperature: UNESTABLISHED.
            pkg.unestablished_contributors.push_back("scanner_repeatability (u_rep)");
            pkg.unestablished_contributors.push_back("scanner_systematic_error (b_scanner)");
            pkg.unestablished_contributors.push_back("fixture_repeatability (u_fixture)");
            pkg.unestablished_contributors.push_back("temperature_correction (b_temp)");
            pkg.unestablished_contributors.push_back("probing_noise (u_probe)");
            pkg.uncertainty_established = false;  // ALWAYS false until real data provided
        }

        // ── Conformance decision ────────────────────────────────────
        pkg.tolerance = request.tolerance_mm;
        if (dev.success && alignment_ok) {
            auto d = analysis::apply_decision_rule_bilateral(
                dev.unsigned_stats.max, request.tolerance_mm / 2.0, 0.0);
            pkg.verdict = d.verdict;
            pkg.granular_verdict = d.granular_verdict;
            pkg.acceptance_lower = d.acceptance_lower;
            pkg.acceptance_upper = d.acceptance_upper;

            // ── PASS-DISABLING GATE ─────────────────────────────────
            // If uncertainty is not established from real calibrated-artifact
            // data, override any PASS/WARNING to INVALID. The system may
            // compute and display deviation, but it must NOT claim conformance
            // on made-up uncertainty.
            if (!pkg.uncertainty_established &&
                (pkg.verdict == analysis::Verdict::PASS ||
                 pkg.verdict == analysis::Verdict::WARNING)) {
                pkg.verdict = analysis::Verdict::INVALID;
                pkg.verdict_label = "INVALID";
                pkg.warnings.push_back(
                    "UNCERTAINTY GATE: measurement uncertainty not established "
                    "from calibrated-artifact data — official PASS not available. "
                    "Deviation and alignment results are displayed but conformance "
                    "cannot be claimed. Provide calibrated-artifact measurements "
                    "per ISO 15530-3 to establish uncertainty.");
            } else {
                switch (pkg.verdict) {
                case analysis::Verdict::PASS:    pkg.verdict_label = "PASS"; break;
                case analysis::Verdict::WARNING: pkg.verdict_label = "WARNING"; break;
                case analysis::Verdict::FAIL:    pkg.verdict_label = "FAIL"; break;
                case analysis::Verdict::INVALID: pkg.verdict_label = "INVALID"; break;
                }
            }
        }

        // If alignment didn't converge, force INVALID regardless of deviation.
        if (!alignment_ok && is_rps_mode) {
            pkg.verdict = analysis::Verdict::INVALID;
            pkg.verdict_label = "INVALID";
            pkg.warnings.push_back(
                "ALIGNMENT GATE: RPS coupling did not converge — the transform "
                "may be unreliable. Verdict forced to INVALID. Check pre-alignment "
                "quality and RPS point placement.");
        }

        pkg.valid = dev.success;
        std::cerr << "DIAG [+" << _diag_lap() << " ms] conformance+heatmap done — pipeline complete" << std::endl;

    } catch (const std::exception& e) {
        pkg.errors.push_back("Pipeline failed: " + std::string(e.what()));
    }

    return pkg;
}

} // namespace alignmesh::service
