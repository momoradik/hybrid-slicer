// DIAGNOSTIC ONLY — Step 1: alignment sanity check.
// Runs the same auto-branch alignment the server uses (FPFH → rotation search
// → GICP on downsampled clouds), then queries ~330 aligned downsampled points
// against CadReference.  Reports distance distribution to confirm the aligned
// cloud actually sits ON the STEP surface.  If not → alignment bug, not speed.

#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/spatial/voxel_downsample.h"
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

int main() {
    std::string step_path = "C:/Users/Sina/OneDrive/Desktop/Step Kawa.step";
    std::string stl_path  = "C:/Users/Sina/OneDrive/Desktop/V2Sina.stl";

    if (!std::filesystem::exists(step_path)) {
        std::cerr << "STEP not found: " << step_path << "\n"; return 1; }
    if (!std::filesystem::exists(stl_path)) {
        std::cerr << "STL not found: " << stl_path << "\n"; return 1; }

    io::ImmutableSourceStore store;

    std::cerr << "=== Importing STEP ===" << std::endl;
    auto t0 = Clock::now();
    auto ref = store.import_file(step_path);
    auto ref_cad = store.cad_reference(ref.hash);
    double step_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
    std::cerr << "STEP import: " << step_ms << " ms  (verts=" << ref.mesh.num_vertices()
              << " tris=" << ref.mesh.num_triangles() << ")" << std::endl;
    if (!ref_cad) { std::cerr << "ERROR: no CadReference\n"; return 1; }

    std::cerr << "=== Importing STL ===" << std::endl;
    t0 = Clock::now();
    auto meas_src = store.import_source(stl_path);
    double stl_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
    std::cerr << "STL import: " << stl_ms << " ms  (pts=" << meas_src.points().cols() << ")" << std::endl;

    // ── Point clouds + downsample (same logic as result_package.cpp) ──
    geometry::PointCloud ref_pc(ref.mesh.vertices());
    geometry::PointCloud meas_pc(meas_src.points());

    auto bbox_size = ref_pc.points().rowwise().maxCoeff() - ref_pc.points().rowwise().minCoeff();
    double diag = bbox_size.norm();
    double voxel_size = std::max(0.1, diag / 21.0);

    auto ref_idx  = spatial::voxel_downsample(ref_pc.points(), voxel_size);
    auto meas_idx = spatial::voxel_downsample(meas_pc.points(), voxel_size);

    auto ref_ds_pts  = extract_subcloud(ref_pc.points(), ref_idx);
    auto meas_ds_pts = extract_subcloud(meas_pc.points(), meas_idx);

    geometry::PointCloud ref_ds(ref_ds_pts);
    geometry::PointCloud meas_ds(meas_ds_pts);

    std::cerr << "\n=== Downsample ===" << std::endl;
    std::cerr << "ref:  " << ref_pc.points().cols() << " -> " << ref_ds_pts.cols() << std::endl;
    std::cerr << "meas: " << meas_pc.points().cols() << " -> " << meas_ds_pts.cols() << std::endl;
    std::cerr << "diag=" << diag << " voxel=" << voxel_size << std::endl;

    // ── Unit/frame check ──
    auto ref_min = ref_pc.points().rowwise().minCoeff();
    auto ref_max = ref_pc.points().rowwise().maxCoeff();
    auto meas_min = meas_pc.points().rowwise().minCoeff();
    auto meas_max = meas_pc.points().rowwise().maxCoeff();
    std::cerr << "\n=== UNITS/FRAME ===" << std::endl;
    std::cerr << "STEP bbox: [" << ref_min.transpose() << "] to [" << ref_max.transpose() << "]" << std::endl;
    std::cerr << "STL  bbox: [" << meas_min.transpose() << "] to [" << meas_max.transpose() << "]" << std::endl;
    std::cerr << "STEP diag: " << (ref_max - ref_min).norm() << " mm" << std::endl;
    std::cerr << "STL  diag: " << (meas_max - meas_min).norm() << " mm" << std::endl;

    // ── Auto-branch alignment: FPFH → rotation search → GICP ──
    std::cerr << "\n=== Alignment (auto-branch, same as server) ===" << std::endl;

    registration::GlobalRegistrationSettings gs;
    gs.voxel_size = voxel_size;
    gs.gnc_noise_bound = std::max(0.1, 0.1 * 5.0); // tolerance 0.1mm

    t0 = Clock::now();
    auto coarse = registration::global_register(meas_ds, ref_ds, gs);
    double fpfh_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
    std::cerr << "FPFH: " << fpfh_ms << " ms  success=" << coarse.success
              << " conf=" << coarse.confidence << std::endl;

    geometry::RigidTransform initial_guess;

    if (coarse.success && coarse.confidence > 0.3) {
        initial_guess = coarse.as_initial_guess();
        std::cerr << "FPFH accepted" << std::endl;
    } else {
        // Rotation search (same as server)
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

        t0 = Clock::now();
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
        double rot_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
        std::cerr << "Rotation search: " << rot_ms << " ms  trials=" << rots.size()
                  << " any_success=" << any_success
                  << " best_rms=" << best_rms << std::endl;

        if (any_success) {
            initial_guess = best_T;
        } else {
            initial_guess = geometry::RigidTransform::from_rotation_translation(
                Eigen::Matrix3d::Identity(), ref_c - meas_c);
            std::cerr << "WARNING: all rotations failed, using centroid" << std::endl;
        }
    }

    // GICP refine
    registration::FineRegistrationSettings fs;
    fs.method = registration::ICPMethod::GICP;
    fs.max_correspondence_distance = diag * 0.3;

    t0 = Clock::now();
    auto fine = registration::fine_register(meas_ds, ref_ds, initial_guess, fs);
    double gicp_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
    std::cerr << "GICP refine: " << gicp_ms << " ms  success=" << fine.success << std::endl;

    geometry::RigidTransform final_T = fine.success ? fine.transform : initial_guess;

    // Final ICP (same as server non-RPS path)
    registration::FineRegistrationSettings final_fs;
    final_fs.method = registration::ICPMethod::POINT_TO_POINT;
    final_fs.max_correspondence_distance = diag * 0.3;
    final_fs.max_iterations = 80;

    t0 = Clock::now();
    auto reg = registration::fine_register(meas_ds, ref_ds, final_T, final_fs);
    double final_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
    std::cerr << "Final ICP: " << final_ms << " ms  success=" << reg.success
              << " rms=" << (reg.success ? reg.final_rms : -1) << std::endl;

    if (reg.success) final_T = reg.transform;

    // ── Apply alignment to downsampled measured cloud ──
    auto aligned_ds = final_T.apply_cloud(meas_ds_pts);

    // ── Distance distribution: aligned downsampled pts → STEP surface ──
    std::cerr << "\n=== ALIGNMENT SANITY: " << aligned_ds.cols()
              << " aligned downsampled pts → CadReference ===" << std::endl;

    t0 = Clock::now();
    std::vector<double> dists;
    int n_ok = 0, n_fail = 0;
    for (Eigen::Index i = 0; i < aligned_ds.cols(); ++i) {
        auto r = ref_cad->closest_point_on_surface(aligned_ds.col(i));
        if (r.status == geometry::SurfaceQueryStatus::OK ||
            r.status == geometry::SurfaceQueryStatus::NOT_CONVERGED) {
            dists.push_back(r.unsigned_distance);
            ++n_ok;
        } else {
            ++n_fail;
        }
    }
    double sanity_ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();

    std::sort(dists.begin(), dists.end());
    std::cerr << "Query time: " << sanity_ms << " ms (" << aligned_ds.cols() << " pts)" << std::endl;
    std::cerr << "OK=" << n_ok << " fail=" << n_fail << std::endl;

    if (!dists.empty()) {
        double sum = std::accumulate(dists.begin(), dists.end(), 0.0);
        std::cerr << "\nDistance distribution (aligned downsampled → STEP surface):" << std::endl;
        std::cerr << "  min:    " << dists.front() << " mm" << std::endl;
        std::cerr << "  p25:    " << dists[dists.size()/4] << " mm" << std::endl;
        std::cerr << "  median: " << dists[dists.size()/2] << " mm" << std::endl;
        std::cerr << "  p75:    " << dists[dists.size()*3/4] << " mm" << std::endl;
        std::cerr << "  p95:    " << dists[dists.size()*95/100] << " mm" << std::endl;
        std::cerr << "  max:    " << dists.back() << " mm" << std::endl;
        std::cerr << "  mean:   " << sum / dists.size() << " mm" << std::endl;

        if (dists[dists.size()/2] > 10.0) {
            std::cerr << "\n*** MISALIGNED: median distance " << dists[dists.size()/2]
                      << " mm >> expected ~0.1mm. Alignment bug, not speed bug. ***" << std::endl;
        } else if (dists[dists.size()/2] < 2.0) {
            std::cerr << "\n*** ALIGNED: median distance " << dists[dists.size()/2]
                      << " mm — cloud is on the surface. Proceed to Step 2. ***" << std::endl;
        } else {
            std::cerr << "\n*** MARGINAL: median distance " << dists[dists.size()/2]
                      << " mm — partial alignment or unit issue. ***" << std::endl;
        }
    }

    // ── Also check full cloud (just first+last 5 points) for sanity ──
    auto aligned_full = final_T.apply_cloud(meas_src.points());
    std::cerr << "\n=== Spot-check: 10 full-cloud aligned pts ===" << std::endl;
    for (int i = 0; i < 5 && i < aligned_full.cols(); ++i) {
        auto r = ref_cad->closest_point_on_surface(aligned_full.col(i));
        std::cerr << "  pt[" << i << "]: dist=" << r.unsigned_distance
                  << " status=" << static_cast<int>(r.status) << std::endl;
    }
    for (Eigen::Index i = std::max(Eigen::Index(0), aligned_full.cols()-5); i < aligned_full.cols(); ++i) {
        auto r = ref_cad->closest_point_on_surface(aligned_full.col(i));
        std::cerr << "  pt[" << i << "]: dist=" << r.unsigned_distance
                  << " status=" << static_cast<int>(r.status) << std::endl;
    }

    return 0;
}
