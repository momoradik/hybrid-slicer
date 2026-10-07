// DIAGNOSTIC: count what fraction of real-data points hit the BRepExtrema fallback.
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/cad/cad_reference.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/fine_registration.h"

#include <chrono>
#include <iostream>
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
    if (!std::filesystem::exists(step_path) || !std::filesystem::exists(stl_path)) {
        std::cerr << "Files not found\n"; return 1; }

    io::ImmutableSourceStore store;
    std::cerr << "Importing STEP..." << std::endl;
    auto ref = store.import_file(step_path);
    auto ref_cad = store.cad_reference(ref.hash);
    if (!ref_cad) { std::cerr << "No CadReference\n"; return 1; }

    std::cerr << "Importing STL..." << std::endl;
    auto meas_src = store.import_source(stl_path);

    // Quick alignment (same as server auto-branch)
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

    // Rotation search + GICP (same as server)
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
        if (trial.success && trial.final_rms < best_rms) { best_rms = trial.final_rms; best_T = trial.transform; any = true; }
    }
    geometry::RigidTransform T = any ? best_T : geometry::RigidTransform::from_rotation_translation(
        Eigen::Matrix3d::Identity(), ref_c - meas_c);

    registration::FineRegistrationSettings fs;
    fs.method = registration::ICPMethod::GICP;
    fs.max_correspondence_distance = diag * 0.3;
    auto fine = registration::fine_register(meas_ds, ref_ds, T, fs);
    if (fine.success) T = fine.transform;

    registration::FineRegistrationSettings final_fs;
    final_fs.method = registration::ICPMethod::POINT_TO_POINT;
    final_fs.max_correspondence_distance = diag * 0.3;
    final_fs.max_iterations = 80;
    auto reg = registration::fine_register(meas_ds, ref_ds, T, final_fs);
    if (reg.success) T = reg.transform;

    // Align full cloud
    auto aligned = T.apply_cloud(meas_src.points());
    std::cerr << "Aligned " << aligned.cols() << " points" << std::endl;

    // Measure deviation on 1000 sample points, counting fallbacks.
    int n_sample = 1000;
    int step = std::max(1, static_cast<int>(aligned.cols()) / n_sample);

    cad::reset_cad_call_counters();
    auto t0 = Clock::now();
    int count = 0;
    for (Eigen::Index i = 0; i < aligned.cols() && count < n_sample; i += step, ++count) {
        ref_cad->closest_point_on_surface(aligned.col(i));
    }
    double ms = std::chrono::duration<double,std::milli>(Clock::now()-t0).count();

    int fallbacks = cad::cad_brep_extrema_fallback_calls();
    std::cerr << "\n=== FALLBACK COUNT (1000 sample points) ===" << std::endl;
    std::cerr << "Total queries: " << count << std::endl;
    std::cerr << "BRepExtrema fallbacks: " << fallbacks << " (" << (100.0*fallbacks/count) << "%)" << std::endl;
    std::cerr << "Wall-time: " << ms << " ms (" << ms/count << " ms/pt)" << std::endl;
    std::cerr << "Projected 183K full: " << (ms/count * 183649 / 1000.0) << " s" << std::endl;

    return 0;
}
