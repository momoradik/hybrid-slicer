// DIAGNOSTIC: Measure mesh spacing and RPS point distances for Sinaa.stl
#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/spatial/kdtree.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <cmath>
#include <numeric>
#include <filesystem>

using namespace alignmesh;

int main() {
    std::string step_path = "C:/Users/Sina/OneDrive/Desktop/Step Kawa.step";
    std::string stl_path  = "C:/Users/Sina/OneDrive/Desktop/Sinaa.stl";
    if (!std::filesystem::exists(step_path) || !std::filesystem::exists(stl_path)) {
        std::cerr << "Files not found\n"; return 1; }

    io::ImmutableSourceStore store;
    std::cerr << "Importing STEP..." << std::endl;
    auto ref = store.import_file(step_path);
    std::cerr << "Importing STL..." << std::endl;
    auto meas = store.import_source(stl_path);

    auto meas_pts = meas.as_mesh().mesh.vertices();
    std::cerr << "STL unique vertices: " << meas_pts.cols() << std::endl;

    // Build KD-tree on measured vertices
    spatial::KdTree tree(meas_pts);

    // Sample 2000 points for NN distance distribution
    int n_sample = std::min(Eigen::Index(2000), meas_pts.cols());
    int step = std::max(Eigen::Index(1), meas_pts.cols() / n_sample);
    std::vector<double> nn_dists;
    std::vector<int> knn_idx;
    std::vector<double> knn_dsq;
    for (Eigen::Index i = 0; i < meas_pts.cols() && static_cast<int>(nn_dists.size()) < n_sample; i += step) {
        tree.knn(meas_pts.col(i), 2, knn_idx, knn_dsq);
        if (knn_dsq.size() >= 2 && knn_dsq[1] > 0)
            nn_dists.push_back(std::sqrt(knn_dsq[1]));
    }
    std::sort(nn_dists.begin(), nn_dists.end());
    std::cerr << "\n=== NN distance distribution (2000 samples) ===" << std::endl;
    std::cerr << "  min:    " << nn_dists.front() << " mm" << std::endl;
    std::cerr << "  p10:    " << nn_dists[nn_dists.size()/10] << " mm" << std::endl;
    std::cerr << "  p25:    " << nn_dists[nn_dists.size()/4] << " mm" << std::endl;
    std::cerr << "  median: " << nn_dists[nn_dists.size()/2] << " mm" << std::endl;
    std::cerr << "  p75:    " << nn_dists[nn_dists.size()*3/4] << " mm" << std::endl;
    std::cerr << "  p90:    " << nn_dists[nn_dists.size()*9/10] << " mm" << std::endl;
    std::cerr << "  max:    " << nn_dists.back() << " mm" << std::endl;
    std::cerr << "  Current auto radius (median*5): " << nn_dists[nn_dists.size()/2]*5.0 << " mm" << std::endl;

    // What radius would give >= 20 neighbors for most points?
    // Use p75 * 5 instead of median * 5
    std::cerr << "  p75*5 radius: " << nn_dists[nn_dists.size()*3/4]*5.0 << " mm" << std::endl;
    std::cerr << "  p90*5 radius: " << nn_dists[nn_dists.size()*9/10]*5.0 << " mm" << std::endl;

    // Bbox diagonal
    auto bbox_min = meas_pts.rowwise().minCoeff();
    auto bbox_max = meas_pts.rowwise().maxCoeff();
    double diag = (bbox_max - bbox_min).norm();
    std::cerr << "  bbox diag: " << diag << " mm" << std::endl;
    std::cerr << "  diag/500 (sane floor): " << diag/500 << " mm" << std::endl;

    // STEP ref mesh spacing
    auto ref_pts = ref.mesh.vertices();
    spatial::KdTree ref_tree(ref_pts);
    std::vector<double> ref_nn;
    int ref_step = std::max(Eigen::Index(1), ref_pts.cols() / 2000);
    for (Eigen::Index i = 0; i < ref_pts.cols() && static_cast<int>(ref_nn.size()) < 2000; i += ref_step) {
        ref_tree.knn(ref_pts.col(i), 2, knn_idx, knn_dsq);
        if (knn_dsq.size() >= 2 && knn_dsq[1] > 0)
            ref_nn.push_back(std::sqrt(knn_dsq[1]));
    }
    std::sort(ref_nn.begin(), ref_nn.end());
    std::cerr << "\n=== REF (STEP) NN distance ===" << std::endl;
    std::cerr << "  vertices: " << ref_pts.cols() << std::endl;
    std::cerr << "  median: " << ref_nn[ref_nn.size()/2] << " mm" << std::endl;
    std::cerr << "  p90:    " << ref_nn[ref_nn.size()*9/10] << " mm" << std::endl;

    return 0;
}
