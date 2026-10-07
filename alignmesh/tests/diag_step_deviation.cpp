// DIAGNOSTIC ONLY — no logic changes, no fixes.
// Instruments the per-query cost of CadReference deviation to prove the
// mechanism of the ~10ms/point cost.

#include "alignmesh/io/immutable_source_store.h"
#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/spatial/aabb_tree.h"
#include "alignmesh/analysis/deviation.h"
#include "alignmesh/cad/cad_reference.h"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>
#include <filesystem>

using namespace alignmesh;
using Clock = std::chrono::steady_clock;

static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main(int argc, char* argv[]) {
    std::string step_path = "C:/Users/Sina/OneDrive/Desktop/Step Kawa.step";
    std::string stl_path  = "C:/Users/Sina/OneDrive/Desktop/V2Sina.stl";

    if (!std::filesystem::exists(step_path)) {
        std::cerr << "STEP file not found: " << step_path << "\n";
        return 1;
    }
    if (!std::filesystem::exists(stl_path)) {
        std::cerr << "STL file not found: " << stl_path << "\n";
        return 1;
    }

    io::ImmutableSourceStore store;

    std::cerr << "=== Importing STEP ===\n";
    auto t0 = Clock::now();
    auto ref = store.import_file(step_path);
    std::cerr << "STEP import: " << ms_since(t0) << " ms"
              << " (verts=" << ref.mesh.num_vertices()
              << " tris=" << ref.mesh.num_triangles() << ")\n";

    auto ref_cad = store.cad_reference(ref.hash);
    if (!ref_cad) {
        std::cerr << "ERROR: no CadReference from STEP\n";
        return 1;
    }

    std::cerr << "=== Importing STL (measured) ===\n";
    t0 = Clock::now();
    auto meas = store.import_source(stl_path);
    auto meas_pts = meas.points();
    std::cerr << "STL import: " << ms_since(t0) << " ms"
              << " (pts=" << meas_pts.cols() << ")\n";

    // ================================================================
    // 1) UNITS / SCALE CHECK
    // ================================================================
    auto ref_min = ref.mesh.vertices().rowwise().minCoeff();
    auto ref_max = ref.mesh.vertices().rowwise().maxCoeff();
    auto meas_min = meas_pts.rowwise().minCoeff();
    auto meas_max = meas_pts.rowwise().maxCoeff();

    std::cerr << "\n=== UNITS/SCALE ===\n";
    std::cerr << "STEP bbox: [" << ref_min.transpose() << "] to [" << ref_max.transpose() << "]\n";
    std::cerr << "STL  bbox: [" << meas_min.transpose() << "] to [" << meas_max.transpose() << "]\n";
    std::cerr << "STEP diag: " << (ref_max - ref_min).norm() << " mm\n";
    std::cerr << "STL  diag: " << (meas_max - meas_min).norm() << " mm\n";
    std::cerr << "Centroid STEP: " << ((ref_min + ref_max) / 2).transpose() << "\n";
    std::cerr << "Centroid STL:  " << ((meas_min + meas_max) / 2).transpose() << "\n";
    std::cerr << "Centroid dist: " << (((ref_min+ref_max)/2) - ((meas_min+meas_max)/2)).norm() << " mm\n";

    // ================================================================
    // 3) ALIGNMENT SANITY: distance distribution from measured to STEP surface
    //    Sample 200 random measured points, compute distance to STEP surface.
    // ================================================================
    std::cerr << "\n=== ALIGNMENT SANITY (200 sample points) ===\n";
    int n_sample = std::min(200, static_cast<int>(meas_pts.cols()));
    int step_sample = std::max(1, static_cast<int>(meas_pts.cols()) / n_sample);
    std::vector<double> dists;
    std::vector<double> query_times;

    for (int i = 0; i < meas_pts.cols() && static_cast<int>(dists.size()) < n_sample; i += step_sample) {
        auto t1 = Clock::now();
        auto r = ref_cad->closest_point_on_surface(meas_pts.col(i));
        double dt = ms_since(t1);
        query_times.push_back(dt);
        if (r.status == geometry::SurfaceQueryStatus::OK ||
            r.status == geometry::SurfaceQueryStatus::NOT_CONVERGED) {
            dists.push_back(r.unsigned_distance);
        }
    }

    std::sort(dists.begin(), dists.end());
    if (!dists.empty()) {
        std::cerr << "Distance distribution (measured → STEP surface):\n";
        std::cerr << "  min:    " << dists.front() << " mm\n";
        std::cerr << "  p25:    " << dists[dists.size()/4] << " mm\n";
        std::cerr << "  median: " << dists[dists.size()/2] << " mm\n";
        std::cerr << "  p75:    " << dists[dists.size()*3/4] << " mm\n";
        std::cerr << "  max:    " << dists.back() << " mm\n";
        double mean = std::accumulate(dists.begin(), dists.end(), 0.0) / dists.size();
        std::cerr << "  mean:   " << mean << " mm\n";
    }

    std::sort(query_times.begin(), query_times.end());
    if (!query_times.empty()) {
        std::cerr << "\nPer-query time (FAR/misaligned measured points):\n";
        std::cerr << "  min:    " << query_times.front() << " ms\n";
        std::cerr << "  median: " << query_times[query_times.size()/2] << " ms\n";
        std::cerr << "  p95:    " << query_times[query_times.size()*95/100] << " ms\n";
        std::cerr << "  max:    " << query_times.back() << " ms\n";
    }

    // ================================================================
    // 2) NEAR vs FAR: sample points ON the tessellation (near=0 distance)
    // ================================================================
    std::cerr << "\n=== NEAR vs FAR: points ON the tessellation ===\n";
    std::vector<double> near_times;
    std::vector<double> near_dists;
    int near_n = std::min(200, static_cast<int>(ref.mesh.num_vertices()));
    int near_step = std::max(1, static_cast<int>(ref.mesh.num_vertices()) / near_n);

    for (Eigen::Index i = 0; i < ref.mesh.num_vertices() && static_cast<int>(near_times.size()) < near_n; i += near_step) {
        Eigen::Vector3d p = ref.mesh.vertices().col(i);
        auto t1 = Clock::now();
        auto r = ref_cad->closest_point_on_surface(p);
        double dt = ms_since(t1);
        near_times.push_back(dt);
        if (r.status == geometry::SurfaceQueryStatus::OK)
            near_dists.push_back(r.unsigned_distance);
    }

    std::sort(near_times.begin(), near_times.end());
    std::cerr << "Per-query time (NEAR/on-surface points):\n";
    std::cerr << "  min:    " << near_times.front() << " ms\n";
    std::cerr << "  median: " << near_times[near_times.size()/2] << " ms\n";
    std::cerr << "  p95:    " << near_times[near_times.size()*95/100] << " ms\n";
    std::cerr << "  max:    " << near_times.back() << " ms\n";

    if (!near_dists.empty()) {
        std::sort(near_dists.begin(), near_dists.end());
        std::cerr << "Distance (should be ~0): median=" << near_dists[near_dists.size()/2]
                  << " max=" << near_dists.back() << " mm\n";
    }

    // ================================================================
    // 1) PER-QUERY BREAKDOWN: instrument sub-steps on 50 points
    // ================================================================
    std::cerr << "\n=== PER-QUERY SUB-STEP BREAKDOWN (50 near-surface points) ===\n";
    // Use tessellation vertices (near surface) for clean measurement.
    std::vector<double> t_bvh, t_face_lookup, t_vertex_make, t_extrema, t_normal, t_total;
    int breakdown_n = std::min(50, static_cast<int>(ref.mesh.num_vertices()));
    int breakdown_step = std::max(1, static_cast<int>(ref.mesh.num_vertices()) / breakdown_n);

    // We can't instrument inside CadReferenceImpl without changing it.
    // Instead, replicate the query steps externally to measure sub-step times.
    // This is a diagnostic tool, NOT a production code change.

    // Get the mesh reference for BVH access
    auto mesh_ref = geometry::make_mesh_reference(ref.mesh);

    for (Eigen::Index i = 0; i < ref.mesh.num_vertices() && static_cast<int>(t_total.size()) < breakdown_n; i += breakdown_step) {
        Eigen::Vector3d p = ref.mesh.vertices().col(i);
        auto tt0 = Clock::now();

        // Sub-step: full CadReference query (includes BVH + face + extrema + normal)
        auto r = ref_cad->closest_point_on_surface(p);
        double total = ms_since(tt0);
        t_total.push_back(total);

        // For comparison: mesh-only BVH query (no OCCT)
        auto tt1 = Clock::now();
        auto mr = mesh_ref->closest_point_on_surface(p);
        double mesh_time = ms_since(tt1);
        t_bvh.push_back(mesh_time);  // approximate BVH cost

        t_extrema.push_back(total - mesh_time);  // OCCT overhead = total - BVH
    }

    auto report_dist = [](const std::vector<double>& v, const char* label) {
        auto sorted = v;
        std::sort(sorted.begin(), sorted.end());
        double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
        std::cerr << label << ":"
                  << " min=" << sorted.front()
                  << " med=" << sorted[sorted.size()/2]
                  << " mean=" << (sum/sorted.size())
                  << " p95=" << sorted[sorted.size()*95/100]
                  << " max=" << sorted.back() << " ms\n";
    };

    report_dist(t_total, "  CadRef total (BVH+OCCT)");
    report_dist(t_bvh, "  MeshRef total (BVH only)");
    report_dist(t_extrema, "  OCCT overhead (CadRef - MeshRef)");

    std::cerr << "\n=== FACE_INDEX SANITY ===\n";
    // Count faces and triangles per face
    // We can't access face_index_ directly, but we can count OCCT faces.
    int n_faces = 0;
    // Just report the mesh stats
    std::cerr << "Tessellation: " << ref.mesh.num_triangles() << " tris, "
              << ref.mesh.num_vertices() << " verts\n";
    std::cerr << "Avg tris/face: (would need face_index_ access — not available externally)\n";
    std::cerr << "Per-face Geom_Surface: constructed fresh each call (BRep_Tool::Surface)\n";
    std::cerr << "ShapeFix/healing: done ONCE at import, not per query\n";

    // ================================================================
    // FULL-CLOUD DEVIATION WALL-TIME (all 183K points, no alignment)
    // ================================================================
    std::cerr << "\n=== FULL-CLOUD DEVIATION: RAW (no alignment) ===\n";
    std::cerr << "Running " << meas_pts.cols() << " points against CadReference...\n";
    t0 = Clock::now();
    auto dev_raw = analysis::compute_deviation(meas_pts, *ref_cad);
    double raw_walltime_ms = ms_since(t0);
    std::cerr << "Wall-time: " << raw_walltime_ms << " ms ("
              << raw_walltime_ms / 1000.0 << " s, "
              << raw_walltime_ms / 60000.0 << " min)\n";
    std::cerr << "Per-point: " << raw_walltime_ms / meas_pts.cols() << " ms\n";
    std::cerr << "Success: " << dev_raw.success << "\n";
    if (dev_raw.success) {
        std::cerr << "Mean unsigned dist: " << dev_raw.unsigned_stats.mean << " mm\n";
        std::cerr << "Max unsigned dist:  " << dev_raw.unsigned_stats.max << " mm\n";
    }

    // ================================================================
    // FULL-CLOUD DEVIATION: WITH CENTROID ALIGNMENT
    // ================================================================
    std::cerr << "\n=== FULL-CLOUD DEVIATION: CENTROID-ALIGNED ===\n";
    Eigen::Vector3d ref_centroid = ref.mesh.vertices().rowwise().mean();
    Eigen::Vector3d meas_centroid = meas_pts.rowwise().mean();
    Eigen::Vector3d shift = ref_centroid - meas_centroid;
    std::cerr << "Centroid shift: [" << shift.transpose() << "] ("
              << shift.norm() << " mm)\n";

    Eigen::Matrix3Xd aligned_pts = meas_pts.colwise() + shift;
    std::cerr << "Running " << aligned_pts.cols() << " aligned points...\n";

    // Distance distribution after centroid alignment (sample)
    std::vector<double> aligned_dists;
    int align_sample_step = std::max(1, static_cast<int>(aligned_pts.cols()) / 500);
    for (Eigen::Index i = 0; i < aligned_pts.cols() && static_cast<int>(aligned_dists.size()) < 500; i += align_sample_step) {
        auto r = ref_cad->closest_point_on_surface(aligned_pts.col(i));
        if (r.status == geometry::SurfaceQueryStatus::OK)
            aligned_dists.push_back(r.unsigned_distance);
    }
    std::sort(aligned_dists.begin(), aligned_dists.end());
    if (!aligned_dists.empty()) {
        std::cerr << "Distance after centroid alignment (500 sample):\n";
        std::cerr << "  min:    " << aligned_dists.front() << " mm\n";
        std::cerr << "  median: " << aligned_dists[aligned_dists.size()/2] << " mm\n";
        std::cerr << "  p95:    " << aligned_dists[aligned_dists.size()*95/100] << " mm\n";
        std::cerr << "  max:    " << aligned_dists.back() << " mm\n";
    }

    // Full deviation on centroid-aligned cloud
    t0 = Clock::now();
    auto dev_aligned = analysis::compute_deviation(aligned_pts, *ref_cad);
    double aligned_walltime_ms = ms_since(t0);
    std::cerr << "Wall-time: " << aligned_walltime_ms << " ms ("
              << aligned_walltime_ms / 1000.0 << " s, "
              << aligned_walltime_ms / 60000.0 << " min)\n";
    std::cerr << "Per-point: " << aligned_walltime_ms / aligned_pts.cols() << " ms\n";
    if (dev_aligned.success) {
        std::cerr << "Mean unsigned dist: " << dev_aligned.unsigned_stats.mean << " mm\n";
        std::cerr << "Max unsigned dist:  " << dev_aligned.unsigned_stats.max << " mm\n";
    }

    // ================================================================
    // VERDICT
    // ================================================================
    std::cerr << "\n=== VERDICT ===\n";
    double median_far = query_times[query_times.size()/2];
    double median_near = near_times[near_times.size()/2];
    double mean_dist = dists.empty() ? -1 : std::accumulate(dists.begin(), dists.end(), 0.0) / dists.size();

    std::cerr << "Far-query median:  " << median_far << " ms\n";
    std::cerr << "Near-query median: " << median_near << " ms\n";
    std::cerr << "Ratio far/near:    " << (median_near > 0 ? median_far/median_near : -1) << "x\n";
    std::cerr << "Mean distance measured→surface (raw): " << mean_dist << " mm\n";
    std::cerr << "Full-cloud deviation wall-time (raw):     " << raw_walltime_ms/60000 << " min\n";
    std::cerr << "Full-cloud deviation wall-time (aligned): " << aligned_walltime_ms/60000 << " min\n";

    if (mean_dist > 10.0) {
        std::cerr << "PRIMARY ISSUE: measured cloud is FAR from STEP surface (mean "
                  << mean_dist << "mm). Alignment/frame mismatch is the real bug.\n";
    }

    return 0;
}
