#include "alignmesh/registration/hybrid_pipeline.h"

namespace alignmesh::registration {

using namespace geometry;

// ============================================================================
// Composition utilities
// ============================================================================

RigidTransform compose_chain(const std::vector<RigidTransform>& stages) {
    // stages[0] applied first → composed = stages[n-1] * ... * stages[0]
    RigidTransform result;  // identity
    for (const auto& T : stages) {
        result = T * result;
    }
    return result;
}

bool verify_composition(
        const std::vector<RigidTransform>& stages,
        const RigidTransform& composed,
        double tolerance) {
    auto recomposed = compose_chain(stages);
    return recomposed.is_approx(composed, tolerance);
}

// ============================================================================
// Helper: make a PipelineStage from a FineRegistrationResult
// ============================================================================

static PipelineStage make_fine_stage(
        const std::string& name,
        const FineRegistrationResult& fr) {
    PipelineStage stage;
    stage.name = name;
    stage.transform = fr.transform;
    stage.rms = fr.final_rms;
    stage.converged = fr.converged;
    stage.warnings = fr.warnings;
    return stage;
}

// ============================================================================
// Landmark-first workflow
// ============================================================================

HybridPipelineResult landmark_first(
        const PointCloud& moving,
        const PointCloud& reference,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_landmarks,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_landmarks,
        const FineRegistrationSettings& fine_settings) {
    HybridPipelineResult result;

    // Stage 1: Kabsch from landmarks.
    auto kabsch = alignment::align_landmarks(source_landmarks, target_landmarks);
    if (!kabsch.success) {
        result.errors.push_back("Landmark alignment failed");
        for (auto& e : kabsch.errors) result.errors.push_back(e);
        return result;
    }

    PipelineStage landmark_stage;
    landmark_stage.name = "landmark_kabsch";
    landmark_stage.transform = kabsch.transform;
    landmark_stage.rms = kabsch.diagnostics.unweighted_rms;
    landmark_stage.converged = true;
    result.stages.push_back(std::move(landmark_stage));

    // Stage 2: Fine ICP on the landmark-aligned clouds.
    auto moved = moving.transformed(kabsch.transform);
    auto fine_result = fine_register(moved, reference, RigidTransform(), fine_settings);

    if (fine_result.success) {
        result.stages.push_back(make_fine_stage("fine_icp", fine_result));
    } else {
        // Fine refinement failed — still return the landmark result.
        result.warnings.push_back("Fine ICP refinement failed; "
            "returning landmark-only result");
        for (auto& e : fine_result.errors) result.warnings.push_back(e);
    }

    // Compose: Final = Fine * Landmark (or just Landmark if Fine failed).
    std::vector<RigidTransform> chain;
    for (auto& s : result.stages) chain.push_back(s.transform);
    result.final_transform = compose_chain(chain);
    result.success = true;
    return result;
}

// ============================================================================
// Best-fit-first workflow
// ============================================================================

HybridPipelineResult best_fit_first(
        const PointCloud& moving,
        const PointCloud& reference,
        const RigidTransform& initial_guess,
        const FineRegistrationSettings& fine_settings,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& source_landmarks,
        const Eigen::Matrix<double, 3, Eigen::Dynamic>& target_landmarks) {
    HybridPipelineResult result;

    // Stage 1: Fine ICP best-fit.
    auto fine_result = fine_register(moving, reference, initial_guess, fine_settings);
    if (!fine_result.success) {
        result.errors.push_back("Best-fit ICP failed");
        for (auto& e : fine_result.errors) result.errors.push_back(e);
        return result;
    }
    result.stages.push_back(make_fine_stage("best_fit_icp", fine_result));

    // Stage 2: Optional landmark/datum correction.
    if (source_landmarks.cols() >= 3 && target_landmarks.cols() >= 3) {
        // Apply the best-fit transform to the source landmarks, then
        // compute a landmark correction.
        auto aligned_landmarks = fine_result.transform.apply_cloud(source_landmarks);
        auto correction = alignment::align_landmarks(aligned_landmarks, target_landmarks);
        if (correction.success) {
            PipelineStage corr_stage;
            corr_stage.name = "landmark_correction";
            corr_stage.transform = correction.transform;
            corr_stage.rms = correction.diagnostics.unweighted_rms;
            corr_stage.converged = true;
            result.stages.push_back(std::move(corr_stage));
        } else {
            result.warnings.push_back("Landmark correction failed; "
                "returning best-fit-only result");
        }
    }

    std::vector<RigidTransform> chain;
    for (auto& s : result.stages) chain.push_back(s.transform);
    result.final_transform = compose_chain(chain);
    result.success = true;
    return result;
}

// ============================================================================
// Global-first workflow
// ============================================================================

HybridPipelineResult global_first(
        const PointCloud& moving,
        const PointCloud& reference,
        const GlobalRegistrationSettings& global_settings,
        const FineRegistrationSettings& fine_settings) {
    HybridPipelineResult result;

    // Stage 1: Global coarse registration.
    auto coarse = global_register(moving, reference, global_settings);
    PipelineStage coarse_stage;
    coarse_stage.name = "global_coarse";
    coarse_stage.converged = coarse.success;
    if (coarse.success) {
        coarse_stage.transform = coarse.as_initial_guess();
    }
    coarse_stage.warnings = coarse.warnings;
    result.stages.push_back(std::move(coarse_stage));

    if (!coarse.success) {
        result.warnings.push_back("Global registration failed; "
            "attempting fine ICP from identity");
    }

    // Stage 2: Fine ICP refinement from the coarse pose.
    RigidTransform init = coarse.success ?
        coarse.as_initial_guess() : RigidTransform();
    auto moved = moving.transformed(init);
    auto fine_result = fine_register(moved, reference, RigidTransform(), fine_settings);

    if (fine_result.success) {
        result.stages.push_back(make_fine_stage("fine_icp", fine_result));
    } else {
        result.warnings.push_back("Fine ICP refinement failed");
        for (auto& e : fine_result.errors) result.warnings.push_back(e);
    }

    std::vector<RigidTransform> chain;
    for (auto& s : result.stages) chain.push_back(s.transform);
    result.final_transform = compose_chain(chain);
    result.success = (fine_result.success || coarse.success);
    return result;
}

} // namespace alignmesh::registration
