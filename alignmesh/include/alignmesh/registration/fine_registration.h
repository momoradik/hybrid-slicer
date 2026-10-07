#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/rigid_transform.h"

#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Fine Registration — authoritative alignment engine
// ============================================================================
//
// Wraps small_gicp (MIT) with our determinism rules:
//   - Single-threaded SerialReduction for the OFFICIAL result path
//   - No -march=native, no TBB/OpenMP
//   - Convergence validated explicitly (not trusting a "converged" flag)
//
// Algorithms:
//   POINT_TO_POINT  — ICP (baseline, use for comparison only)
//   POINT_TO_PLANE  — ICP with point-to-plane metric (default)
//   GICP            — Generalized ICP (surface covariance model)
//   VGICP           — Voxelized GICP (fast, coarse alignment)
// ============================================================================

enum class ICPMethod {
    POINT_TO_POINT,
    POINT_TO_PLANE,
    GICP,
    VGICP,
};

struct FineRegistrationSettings {
    ICPMethod method = ICPMethod::POINT_TO_PLANE;

    int max_iterations = 100;
    double max_correspondence_distance = 1.0;

    /// Convergence criteria.
    double rotation_tolerance = 1e-6;      // radians
    double translation_tolerance = 1e-6;   // same units as points

    /// Multi-resolution voxel schedule (coarse → fine).
    /// Empty = single resolution at full data.
    std::vector<double> voxel_schedule;

    /// Per-level max correspondence distance (parallel to voxel_schedule).
    /// If empty, uses max_correspondence_distance for all levels.
    std::vector<double> distance_schedule;

    /// VGICP voxel resolution (used only when method == VGICP).
    double vgicp_voxel_resolution = 1.0;

    /// Normal estimation neighbourhood size (for point-to-plane, GICP, VGICP).
    int normal_estimation_k = 20;
};

struct PerIterationRecord {
    double error = 0;
    int num_inliers = 0;
};

struct FineRegistrationResult {
    bool success = false;
    bool converged = false;

    geometry::RigidTransform transform;

    /// Per-iteration convergence record.
    std::vector<PerIterationRecord> iterations;
    int total_iterations = 0;

    double final_rms = 0;
    double overlap_ratio = 0;   // fraction of source points with correspondences
    int num_correspondences = 0;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Run fine registration.
/// @param moving     Source/moving point cloud (will be transformed).
/// @param reference  Target/fixed point cloud.
/// @param initial_guess  Initial alignment estimate.
/// @param settings   Algorithm and convergence parameters.
FineRegistrationResult fine_register(
    const geometry::PointCloud& moving,
    const geometry::PointCloud& reference,
    const geometry::RigidTransform& initial_guess,
    const FineRegistrationSettings& settings = {});

} // namespace alignmesh::registration
