#pragma once

#include "alignmesh/geometry/point_cloud.h"
#include "alignmesh/geometry/rigid_transform.h"
#include "alignmesh/numerics/seeded_rng.h"

#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// CoarsePoseGuess — type-safe initial guess (NOT an official result)
// ============================================================================
//
// The output of global/coarse registration is structurally separated from
// the fine-registration result. A CoarsePoseGuess CANNOT be implicitly
// used where a RigidTransform is expected — you must explicitly call
// as_initial_guess() to acknowledge that this is an unrefined estimate.
//
// TEASER++'s optimality certificate certifies the CORRESPONDENCE PROBLEM,
// NOT metrological accuracy. The confidence score here is a quality
// indicator for the correspondence matching, not a dimensional accuracy.
// ============================================================================

struct CoarsePoseGuess {
    /// Explicit conversion to initial guess for fine registration.
    /// This is the ONLY way to use the coarse result downstream.
    geometry::RigidTransform as_initial_guess() const { return transform_; }

    bool success = false;

    /// Quality indicator (0–1). Measures correspondence consistency,
    /// NOT dimensional accuracy. Do not surface as an accuracy guarantee.
    double confidence = 0;

    /// True if multiple valid global poses were detected (symmetric object).
    /// The returned transform is ONE of them; the user must verify.
    bool is_ambiguous = false;

    /// Number of putative / inlier correspondences.
    int num_putative_correspondences = 0;
    int num_inlier_correspondences = 0;
    double inlier_ratio = 0;

    /// The RNG seed used (for reproducibility).
    uint64_t seed = 0;

    /// Method used.
    std::string method;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    /// For internal / testing use only.
    const geometry::RigidTransform& raw_transform() const { return transform_; }
    void set_transform(const geometry::RigidTransform& T) { transform_ = T; }

private:
    geometry::RigidTransform transform_;
};

// ============================================================================
// Global registration settings
// ============================================================================

struct GlobalRegistrationSettings {
    /// Voxel size for downsampling before feature computation.
    /// Default tuned for mm-scale metrology parts (NOT LiDAR/metre scale).
    double voxel_size = 0.5;

    /// FPFH feature computation radius (relative to voxel_size).
    double fpfh_radius_multiplier = 5.0;

    /// Normal estimation neighbours for feature computation.
    int normal_k = 30;

    /// Max correspondence distance in feature space.
    double feature_match_ratio = 0.9;

    /// GNC parameters for robust correspondence alignment.
    double gnc_initial_mu = 100.0;
    double gnc_mu_factor = 1.4;
    double gnc_noise_bound = 0.1;  // expected inlier noise (mm)

    /// RNG seed for deterministic behaviour.
    uint64_t seed = 42;
};

/// Run global registration to produce a coarse initial guess.
/// The result is a CoarsePoseGuess, NOT an official measurement result.
///
/// Algorithm: FPFH feature matching + GNC-TLS robust correspondence
/// alignment (following TEASER++/KISS-Matcher algorithmic principles).
/// Voxel sizes and feature radii are tuned for metrology scale.
CoarsePoseGuess global_register(
    const geometry::PointCloud& source,
    const geometry::PointCloud& target,
    const GlobalRegistrationSettings& settings = {});

} // namespace alignmesh::registration
