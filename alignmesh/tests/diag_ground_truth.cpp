// DIAGNOSTIC: Ground-truth alignment test.
// Reference = STEP, measured = STL export of same part.
// Reports all six diagnostic items requested.

#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/cad/cad_reference.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include <filesystem>

using namespace alignmesh;
using Clock = std::chrono::steady_clock;

static Eigen::Matrix<double,3,Eigen::Dynamic> extract_subcloud(
    const Eigen::Matrix<double,3,Eigen::Dynamic>& pts,
    const std::vector<Eigen::Index>& idx)
{
    Eigen::Matrix<double,3,Eigen::Dynamic> sub(3, static_cast<Eigen::Index>(idx.size()));
    for (std::size_t i = 0; i < idx.size(); ++i)
        sub.col(static_cast<Eigen::Index>(i)) = pts.col(idx[i]);
    return sub;
}

static void report_dist(const std::vector<double>& dists, const char* label) {
    if (dists.empty()) { std::cerr << label << ": (empty)\n"; return; }
    auto sorted = dists;
    std::sort(sorted.begin(), sorted.end());
    double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double mean = sum / sorted.size();
    double sq_sum = 0;
    for (double d : sorted) sq_sum += d * d;
    double rms = std::sqrt(sq_sum / sorted.size());
    std::cerr << label << ":\n"
              << "  min:    " << sorted.front() << " mm\n"
              << "  p25:    " << sorted[sorted.size()/4] << " mm\n"
              << "  median: " << sorted[sorted.size()/2] << " mm\n"
              << "  p75:    " << sorted[sorted.size()*3/4] << " mm\n"
              << "  p95:    " << sorted[sorted.size()*95/100] << " mm\n"
              << "  max:    " << sorted.back() << " mm\n"
              << "  mean:   " << mean << " mm\n"
              << "  RMS:    " << rms << " mm\n"
              << "  N:      " << sorted.size() << "\n";
}

int main() {
    std::string step_path = "C:/Users/Sina/OneDrive/Desktop/Step Kawa.step";
    std::string stl_path  = "C:/Users/Sina/OneDrive/Desktop/Sinaa.stl";

    if (!std::filesystem::exists(step_path)) {
        std::cerr << "STEP not found: " << step_path << "\n"; return 1; }
    if (!std::filesystem::exists(stl_path)) {
        std::cerr << "STL not found: " << stl_path << "\n"; return 1; }

    io::ImmutableSourceStore store;

    std::cerr << "=== Importing STEP ===" << std::endl;
    auto ref = store.import_file(step_path);
    auto ref_cad = store.cad_reference(ref.hash);
    if (!ref_cad) { std::cerr << "ERROR: no CadReference\n"; return 1; }

    std::cerr << "=== Importing STL (Sinaa.stl) ===" << std::endl;
    auto meas_src = store.import_source(stl_path);
    auto meas_pts = meas_src.points();
    std::cerr << "STL pts: " << meas_pts.cols() << std::endl;

    // ================================================================
    // 1) UNITS / FRAME
    // ================================================================
    std::cerr << "\n========== 1) UNITS / FRAME ==========" << std::endl;
    auto ref_verts = ref.mesh.vertices();
    auto ref_min = ref_verts.rowwise().minCoeff();
    auto ref_max = ref_verts.rowwise().maxCoeff();
    auto ref_size = ref_max - ref_min;
    auto ref_centroid = (ref_min + ref_max) / 2.0;

    auto meas_min = meas_pts.rowwise().minCoeff();
    auto meas_max = meas_pts.rowwise().maxCoeff();
    auto meas_size = meas_max - meas_min;
    auto meas_centroid = (meas_min + meas_max) / 2.0;

    std::cerr << "STEP bbox size: [" << ref_size.transpose() << "] mm"
              << "  diag=" << ref_size.norm() << std::endl;
    std::cerr << "STL  bbox size: [" << meas_size.transpose() << "] mm"
              << "  diag=" << meas_size.norm() << std::endl;
    std::cerr << "Size ratio (STL/STEP): ["
              << meas_size(0)/ref_size(0) << ", "
              << meas_size(1)/ref_size(1) << ", "
              << meas_size(2)/ref_size(2) << "]" << std::endl;
    std::cerr << "STEP centroid: [" << ref_centroid.transpose() << "]" << std::endl;
    std::cerr << "STL  centroid: [" << meas_centroid.transpose() << "]" << std::endl;
    std::cerr << "Centroid offset: [" << (meas_centroid - ref_centroid).transpose()
              << "]  dist=" << (meas_centroid - ref_centroid).norm() << " mm" << std::endl;

    // ================================================================
    // 2) ALIGNMENT PATH + 3) DOWNSAMPLE CONFIRMATION + 6) DOWNSAMPLE INFO
    // ================================================================
    std::cerr << "\n========== 2) ALIGNMENT PATH ==========" << std::endl;
    std::cerr << "alignment_mode: (not set — default auto/coarse-to-fine)" << std::endl;
    std::cerr << "RPS points: none provided → RPS did NOT run" << std::endl;

    geometry::PointCloud ref_pc(ref_verts);
    geometry::PointCloud meas_pc(meas_pts);

    auto bbox_size = ref_pc.points().rowwise().maxCoeff() - ref_pc.points().rowwise().minCoeff();
    double diag = bbox_size.norm();
    double voxel_size = std::max(0.1, diag / 21.0);

    auto ref_idx = spatial::voxel_downsample(ref_pc.points(), voxel_size);
    auto meas_idx = spatial::voxel_downsample(meas_pc.points(), voxel_size);
    auto ref_ds_pts = extract_subcloud(ref_pc.points(), ref_idx);
    auto meas_ds_pts = extract_subcloud(meas_pc.points(), meas_idx);
    geometry::PointCloud ref_ds(ref_ds_pts);
    geometry::PointCloud meas_ds(meas_ds_pts);

    std::cerr << "\n========== 3) DOWNSAMPLE ==========" << std::endl;
    std::cerr << "voxel_size: " << voxel_size << " mm" << std::endl;
    std::cerr << "ref:  " << ref_verts.cols() << " → " << ref_ds_pts.cols() << std::endl;
    std::cerr << "meas: " << meas_pts.cols() << " → " << meas_ds_pts.cols() << std::endl;

    std::cerr << "\n========== 6) DOWNSAMPLE INFO ==========" << std::endl;
    std::cerr << "diag=" << diag << " mm  voxel=" << voxel_size << " mm" << std::endl;
    std::cerr << "ref_ds=" << ref_ds_pts.cols() << "  meas_ds=" << meas_ds_pts.cols() << std::endl;

    // Run the same alignment the server uses (auto-branch).
    std::cerr << "\n--- FPFH ---" << std::endl;
    registration::GlobalRegistrationSettings gs;
    gs.voxel_size = voxel_size;
    gs.gnc_noise_bound = std::max(0.1, 0.1 * 5.0);
    auto coarse = registration::global_register(meas_ds, ref_ds, gs);
    std::cerr << "FPFH success=" << coarse.success
              << " confidence=" << coarse.confidence << std::endl;

    geometry::RigidTransform initial_guess;
    std::string branch_name;

    if (coarse.success && coarse.confidence > 0.3) {
        initial_guess = coarse.as_initial_guess();
        branch_name = "FPFH accepted";
    } else {
        std::cerr << "FPFH rejected (conf <= 0.3) → rotation search" << std::endl;
        Eigen::Vector3d ref_c = ref_ds.points().rowwise().mean();
        Eigen::Vector3d meas_c = meas_ds.points().rowwise().mean();

        std::vector<Eigen::Matrix3d> rots;
        auto rx = [](double a) -> Eigen::Matrix3d {
            double c=std::cos(a),s=std::sin(a);
            Eigen::Matrix3d m; m<<1,0,0,0,c,-s,0,s,c; return m; };
        auto ry = [](double a) -> Eigen::Matrix3d {
            double c=std::cos(a),s=std::sin(a);
            Eigen::Matrix3d m; m<<c,0,s,0,1,0,-s,0,c; return m; };
        auto rz = [](double a) -> Eigen::Matrix3d {
            double c=std::cos(a),s=std::sin(a);
            Eigen::Matrix3d m; m<<c,-s,0,s,c,0,0,0,1; return m; };
        const double H = 1.5707963267948966;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) rots.push_back(ry(j*H)*rx(i*H));
        for (int k = 1; k < 4; ++k) rots.push_back(rz(k*H));

        registration::FineRegistrationSettings quick_fs;
        quick_fs.method = registration::ICPMethod::POINT_TO_POINT;
        quick_fs.max_correspondence_distance = diag * 0.5;
        quick_fs.max_iterations = 20;
        double best_rms = 1e18;
        geometry::RigidTransform best_T;
        bool any = false;
        for (auto& R : rots) {
            Eigen::Vector3d t = ref_c - R * meas_c;
            auto T_init = geometry::RigidTransform::from_rotation_translation(R, t);
            auto trial = registration::fine_register(meas_ds, ref_ds, T_init, quick_fs);
            if (trial.success && trial.final_rms < best_rms) {
                best_rms = trial.final_rms; best_T = trial.transform; any = true;
            }
        }
        if (any) {
            initial_guess = best_T;
            branch_name = "rotation search (best_rms=" + std::to_string(best_rms) + ")";
        } else {
            initial_guess = geometry::RigidTransform::from_rotation_translation(
                Eigen::Matrix3d::Identity(), ref_c - meas_c);
            branch_name = "centroid (all failed)";
        }
        std::cerr << "Rotation search: trials=" << rots.size()
                  << " best_rms=" << best_rms << std::endl;
    }

    // Coarse seed = initial_guess from rotation search.
    geometry::RigidTransform coarse_T = initial_guess;
    std::cerr << "Coarse seed branch: " << branch_name << std::endl;

    // ================================================================
    // BEFORE: residual with coarse-only alignment
    // ================================================================
    std::cerr << "\n========== BEFORE (coarse seed only) ==========" << std::endl;
    {
        auto aligned = coarse_T.apply_cloud(meas_ds_pts);
        std::vector<double> dists;
        for (Eigen::Index i = 0; i < aligned.cols(); ++i) {
            auto r = ref_cad->closest_point_on_surface(aligned.col(i));
            if (r.status == geometry::SurfaceQueryStatus::OK ||
                r.status == geometry::SurfaceQueryStatus::NOT_CONVERGED)
                dists.push_back(r.unsigned_distance);
        }
        report_dist(dists, "Coarse-only residual (ds → STEP)");
    }

    // ================================================================
    // FINE STAGE: point-to-plane on dense clouds
    // ================================================================
    std::cerr << "\n========== FINE STAGE ==========" << std::endl;
    double fine_voxel = std::max(0.05, diag / 150.0);
    auto ref_fine_idx = spatial::voxel_downsample(ref_pc.points(), fine_voxel);
    auto meas_fine_idx = spatial::voxel_downsample(meas_pc.points(), fine_voxel);
    auto ref_fine_pts = extract_subcloud(ref_pc.points(), ref_fine_idx);
    auto meas_fine_pts = extract_subcloud(meas_pc.points(), meas_fine_idx);
    geometry::PointCloud ref_fine(ref_fine_pts);
    geometry::PointCloud meas_fine(meas_fine_pts);

    std::cerr << "fine_voxel=" << fine_voxel << " mm" << std::endl;
    std::cerr << "ref_fine=" << ref_fine_pts.cols()
              << " meas_fine=" << meas_fine_pts.cols() << std::endl;

    registration::FineRegistrationSettings fine_fs;
    fine_fs.method = registration::ICPMethod::POINT_TO_PLANE;
    fine_fs.max_correspondence_distance = diag * 0.1;
    fine_fs.max_iterations = 200;
    fine_fs.rotation_tolerance = 1e-8;
    fine_fs.translation_tolerance = 1e-8;

    auto t0 = Clock::now();
    auto reg = registration::fine_register(meas_fine, ref_fine, coarse_T, fine_fs);
    double fine_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    std::cerr << "Fine P2PL: success=" << reg.success
              << " converged=" << reg.converged
              << " iters=" << reg.total_iterations
              << " rms=" << reg.final_rms
              << " correspondences=" << reg.num_correspondences
              << " wall=" << fine_ms << " ms" << std::endl;

    geometry::RigidTransform fine_T = reg.success ? reg.transform : coarse_T;

    // ================================================================
    // AFTER: residual with fine alignment
    // ================================================================
    std::cerr << "\n========== AFTER (fine point-to-plane) ==========" << std::endl;
    {
        auto aligned = fine_T.apply_cloud(meas_ds_pts);
        std::vector<double> dists;
        for (Eigen::Index i = 0; i < aligned.cols(); ++i) {
            auto r = ref_cad->closest_point_on_surface(aligned.col(i));
            if (r.status == geometry::SurfaceQueryStatus::OK ||
                r.status == geometry::SurfaceQueryStatus::NOT_CONVERGED)
                dists.push_back(r.unsigned_distance);
        }
        report_dist(dists, "Fine-stage residual (ds → STEP)");
    }

    return 0;
}
