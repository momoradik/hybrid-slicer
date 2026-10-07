// DIAGNOSTIC: Print 6 STEP surface vertices suitable as RPS nominal points,
// and check the neighbor count at the chosen radius after pre-alignment.
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/spatial/kdtree.h"
#include "alignmesh/spatial/voxel_downsample.h"
#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/fine_registration.h"

#include <iostream>
#include <cmath>
#include <vector>
#include <filesystem>

using namespace alignmesh;

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
    std::string stl_path  = "C:/Users/Sina/OneDrive/Desktop/Sinaa.stl";
    if (!std::filesystem::exists(step_path) || !std::filesystem::exists(stl_path)) {
        std::cerr << "Files not found\n"; return 1; }

    io::ImmutableSourceStore store;
    std::cerr << "Importing..." << std::endl;
    auto ref = store.import_file(step_path);
    auto meas = store.import_source(stl_path);
    auto ref_cad = store.cad_reference(ref.hash);

    auto ref_pts = ref.mesh.vertices();
    auto meas_pts = meas.as_mesh().mesh.vertices();

    // Pick 6 well-spread vertices from the STEP mesh as RPS candidates.
    // Choose vertices at different spatial regions.
    auto bbox_min = ref_pts.rowwise().minCoeff();
    auto bbox_max = ref_pts.rowwise().maxCoeff();
    auto bbox_mid = (bbox_min + bbox_max) / 2.0;

    std::cerr << "STEP bbox: [" << bbox_min.transpose() << "] to [" << bbox_max.transpose() << "]" << std::endl;

    // Pick 6 vertices at specific regions
    struct Pick { Eigen::Vector3d target; Eigen::Index best_vi; double best_dist; };
    std::vector<Pick> picks = {
        {Eigen::Vector3d(bbox_min(0)+5, bbox_mid(1), bbox_mid(2)), -1, 1e18},  // near -X face
        {Eigen::Vector3d(bbox_max(0)-5, bbox_mid(1), bbox_mid(2)), -1, 1e18},  // near +X face
        {Eigen::Vector3d(bbox_mid(0), bbox_min(1)+2, bbox_mid(2)), -1, 1e18},  // near -Y face
        {Eigen::Vector3d(bbox_mid(0), bbox_max(1)-2, bbox_mid(2)), -1, 1e18},  // near +Y face
        {Eigen::Vector3d(bbox_mid(0), bbox_mid(1), bbox_min(2)+5), -1, 1e18},  // near -Z face
        {Eigen::Vector3d(bbox_mid(0), bbox_mid(1), bbox_max(2)-5), -1, 1e18},  // near +Z face
    };

    for (Eigen::Index v = 0; v < ref_pts.cols(); ++v) {
        for (auto& pk : picks) {
            double d = (ref_pts.col(v) - pk.target).norm();
            if (d < pk.best_dist) { pk.best_dist = d; pk.best_vi = v; }
        }
    }

    std::cerr << "\n=== 6 RPS candidate vertices (STEP frame) ===" << std::endl;
    for (int i = 0; i < 6; ++i) {
        auto p = ref_pts.col(picks[i].best_vi);
        std::cerr << "  RPS[" << i << "]: (" << p(0) << ", " << p(1) << ", " << p(2)
                  << ")  dist_to_target=" << picks[i].best_dist << std::endl;
    }

    // Run pre-alignment (same as server)
    geometry::PointCloud ref_pc(ref_pts);
    geometry::PointCloud meas_pc(meas_pts);
    double diag = (ref_pts.rowwise().maxCoeff() - ref_pts.rowwise().minCoeff()).norm();
    double voxel_size = std::max(0.1, diag / 21.0);
    auto ref_idx = spatial::voxel_downsample(ref_pc.points(), voxel_size);
    auto meas_idx = spatial::voxel_downsample(meas_pc.points(), voxel_size);
    geometry::PointCloud ref_ds(extract_subcloud(ref_pc.points(), ref_idx));
    geometry::PointCloud meas_ds(extract_subcloud(meas_pc.points(), meas_idx));

    // Rotation search
    Eigen::Vector3d ref_c = ref_ds.points().rowwise().mean();
    Eigen::Vector3d meas_c = meas_ds.points().rowwise().mean();
    std::vector<Eigen::Matrix3d> rots;
    auto rx = [](double a) -> Eigen::Matrix3d { double c=std::cos(a),s=std::sin(a); Eigen::Matrix3d m; m<<1,0,0,0,c,-s,0,s,c; return m; };
    auto ry = [](double a) -> Eigen::Matrix3d { double c=std::cos(a),s=std::sin(a); Eigen::Matrix3d m; m<<c,0,s,0,1,0,-s,0,c; return m; };
    auto rz = [](double a) -> Eigen::Matrix3d { double c=std::cos(a),s=std::sin(a); Eigen::Matrix3d m; m<<c,-s,0,s,c,0,0,0,1; return m; };
    const double H = 1.5707963267948966;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) rots.push_back(ry(j*H)*rx(i*H));
    for (int k = 1; k < 4; ++k) rots.push_back(rz(k*H));

    registration::FineRegistrationSettings qfs;
    qfs.method = registration::ICPMethod::POINT_TO_POINT;
    qfs.max_correspondence_distance = diag * 0.5;
    qfs.max_iterations = 20;
    double best_rms = 1e18;
    geometry::RigidTransform best_T;
    bool any = false;
    for (auto& R : rots) {
        auto T = geometry::RigidTransform::from_rotation_translation(R, ref_c - R*meas_c);
        auto trial = registration::fine_register(meas_ds, ref_ds, T, qfs);
        if (trial.success && trial.final_rms < best_rms) { best_rms = trial.final_rms; best_T = trial.transform; any = true; }
    }
    // GICP refine
    registration::FineRegistrationSettings gfs;
    gfs.method = registration::ICPMethod::GICP;
    gfs.max_correspondence_distance = diag * 0.3;
    auto gicp = registration::fine_register(meas_ds, ref_ds, best_T, gfs);
    geometry::RigidTransform pre_T = gicp.success ? gicp.transform : best_T;

    std::cerr << "\nPre-alignment done (rot search rms=" << best_rms << ")" << std::endl;

    // Apply pre-alignment to measured
    auto aligned_meas = pre_T.apply_cloud(meas_pts);

    // Build KD-tree on aligned measured
    spatial::KdTree meas_tree(aligned_meas);

    // For each RPS point, check: nearest measured point distance & neighbor count at various radii
    std::cerr << "\n=== Per-RPS-point diagnostics ===" << std::endl;
    for (int i = 0; i < 6; ++i) {
        auto p = ref_pts.col(picks[i].best_vi);
        auto nn = meas_tree.nearest(p);
        double nn_dist = std::sqrt(nn.distance_sq);

        std::vector<int> nbrs; std::vector<double> dsq;
        meas_tree.radius_search(p, 0.31, nbrs, dsq);
        int n031 = static_cast<int>(nbrs.size());
        meas_tree.radius_search(p, 1.0, nbrs, dsq);
        int n1 = static_cast<int>(nbrs.size());
        meas_tree.radius_search(p, 3.0, nbrs, dsq);
        int n3 = static_cast<int>(nbrs.size());

        std::cerr << "  RPS[" << i << "] (" << p(0) << "," << p(1) << "," << p(2) << ")"
                  << " → nn_dist=" << nn_dist << "mm"
                  << " nbrs@0.31mm=" << n031
                  << " nbrs@1mm=" << n1
                  << " nbrs@3mm=" << n3 << std::endl;
    }

    // Print JSON-ready RPS points for curl
    std::cerr << "\n=== JSON for curl ===" << std::endl;
    std::cerr << "[";
    for (int i = 0; i < 6; ++i) {
        auto p = ref_pts.col(picks[i].best_vi);
        const char* axis = (i < 3) ? "normal" : (i < 5 ? "x" : "y");
        if (i > 0) std::cerr << ",";
        std::cerr << "{\"x\":" << p(0) << ",\"y\":" << p(1) << ",\"z\":" << p(2)
                  << ",\"locks\":[{\"axis\":\"" << axis << "\",\"weight\":1}]}";
    }
    std::cerr << "]" << std::endl;

    return 0;
}
