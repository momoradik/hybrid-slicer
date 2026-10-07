#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/registration/fine_registration.h"
#include "alignmesh/registration/global_registration.h"
#include "alignmesh/registration/observability.h"
#include "alignmesh/alignment/kabsch.h"

#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Hybrid alignment pipeline + transform composition
// ============================================================================
//
// Three workflows:
//
//   LANDMARK_FIRST:
//     Kabsch(landmarks) → Fine(ICP) → Final = Fine * Landmark
//
//   BEST_FIT_FIRST:
//     Global/Fine → optional Landmark/Datum correction → Final = Correction * BestFit
//
//   GLOBAL_FIRST:
//     TEASER++/KISS-Matcher → GICP/point-to-plane → optional Landmark/Datum
//     → Final = Datum * Fine * Coarse
//
// Composition convention (column-vector):
//   Final applies as: p' = Final * p = T_n * ... * T_2 * T_1 * p
//   i.e. stages are composed right-to-left: Final = T_n * ... * T_1
// ============================================================================

/// A single stage in the composition chain.
struct PipelineStage {
    std::string name;
    geometry::RigidTransform transform;
    double rms = 0;
    bool converged = false;
    bool fully_constrained = true;
    std::vector<std::string> warnings;
};

/// Result of the hybrid pipeline.
struct HybridPipelineResult {
    bool success = false;

    /// The final composed transform: applies ALL stages in order.
    geometry::RigidTransform final_transform;

    /// Per-stage records (in application order: stage[0] applied first).
    std::vector<PipelineStage> stages;

    /// Observability of the final alignment (if computed).
    ObservabilityResult observability;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

// ============================================================================
// Composition utilities
// ============================================================================

/// Compose a chain of transforms: result = stages[n-1] * ... * stages[0].
/// Stages are in application order (stage[0] applied first to the point).
geometry::RigidTransform compose_chain(
    const std::vector<geometry::RigidTransform>& stages);

/// Verify that a composed result matches the product of individual stages.
bool verify_composition(
    const std::vector<geometry::RigidTransform>& stages,
    const geometry::RigidTransform& composed,
    double tolerance = 1e-10);

// ============================================================================
// Workflow functions
// ============================================================================

/// Landmark-first workflow:
///   1. Kabsch alignment from landmark correspondences
///   2. Fine ICP refinement on the full point clouds
///   Final = Fine * Landmark
HybridPipelineResult landmark_first(
    const geometry::PointCloud& moving,
    const geometry::PointCloud& reference,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_landmarks,
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_landmarks,
    const FineRegistrationSettings& fine_settings = {});

/// Best-fit-first workflow:
///   1. Fine ICP best-fit alignment
///   2. Optional landmark/datum correction
///   Final = Correction * BestFit
HybridPipelineResult best_fit_first(
    const geometry::PointCloud& moving,
    const geometry::PointCloud& reference,
    const geometry::RigidTransform& initial_guess,
    const FineRegistrationSettings& fine_settings = {},
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_landmarks = {},
    const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_landmarks = {});

/// Global-first workflow:
///   1. Global coarse registration (FPFH + GNC-TLS)
///   2. Fine ICP refinement
///   3. Optional landmark/datum verification
///   Final = Fine * Coarse
HybridPipelineResult global_first(
    const geometry::PointCloud& moving,
    const geometry::PointCloud& reference,
    const GlobalRegistrationSettings& global_settings = {},
    const FineRegistrationSettings& fine_settings = {});

} // namespace alignmesh::registration
