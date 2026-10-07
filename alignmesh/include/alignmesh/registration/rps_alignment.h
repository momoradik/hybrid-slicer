#pragma once

#include "alignmesh/geometry/rigid_transform.h"

#include <Eigen/Core>
#include <Eigen/Dense>
#include <string>
#include <vector>

namespace alignmesh::registration {

// ============================================================================
// Pre-aligned RPS Alignment — Simultaneous Weighted Directional Least-Squares
// ============================================================================
//
// Finds rigid transform T = (R, t) minimizing the weighted sum of squared
// directional deviations across ALL constraint points simultaneously:
//
//   min_{R,t}  Σ_i Σ_k  w_{i,k} · [ d_{i,k}ᵀ · ((R·m_i + t) − p_i) ]²
//
// where:
//   p_i      = nominal (reference) position of point i
//   m_i      = measured (projected) position of point i
//   d_{i,k}  = locked direction k for point i (from reference geometry,
//              e.g. surface normal, or axis unit vectors X/Y/Z)
//   w_{i,k}  = user priority weight for that point/direction (LIVE: changing
//              a weight changes the result in over-determined cases)
//
// Solved by Gauss-Newton with so(3) linearization of the rotation increment:
//   R ← exp([δθ]×) · R
//
// Initialized from the Stage-1 pre-alignment; converges in a few iterations.
//
// PROHIBITED in this path: Kabsch / Procrustes / SVD point-to-point best-fit.
// That minimizes full 3D ‖R·m+t−p‖² and ignores directional locks + weights.
//
// DOF accounting: the 6-column constraint Jacobian is built from locked
// directions and point geometry. Its numerical rank determines whether the
// problem is fully constrained, under-constrained, or over-determined.
//   rank < 6 → REJECT (report which DOFs are free)
//   rank = 6, exactly determined → solve
//   rank = 6, over-determined → weighted LS (priorities govern trade-off)
// ============================================================================

/// A single directional constraint for one RPS point.
/// Each point can have 1–3 locked directions, each with its own weight.
struct RPSDirectionLock {
    Eigen::Vector3d direction;  ///< Unit direction in reference frame.
    double weight = 1.0;        ///< Priority weight (>0). Higher = more important.
};

/// Specification of one RPS constraint point.
struct RPSConstraintPoint {
    Eigen::Vector3d nominal;    ///< Nominal (reference) position.
    Eigen::Vector3d measured;   ///< Measured (projected) position.

    /// Locked directions with weights. Each entry constrains deviation
    /// along that direction. A point locked in 3 orthogonal directions
    /// is fully constrained (like a fixture ball). A point locked in 1
    /// direction (surface normal) constrains only the normal distance.
    std::vector<RPSDirectionLock> locks;
};

/// Per-point residual after RPS alignment.
struct RPSResidual {
    Eigen::Vector3d nominal;
    Eigen::Vector3d aligned_measured;
    double total_residual = 0;  ///< ‖aligned − nominal‖ (full 3D, for reporting).
    /// Per-direction residuals (same order as locks).
    std::vector<double> directional_residuals;
};

/// DOF constraint status determined by Jacobian rank analysis.
struct RPSDOFStatus {
    int rank = 0;              ///< Numerical rank of constraint Jacobian.
    int num_constraints = 0;   ///< Total number of directional constraints.
    bool fully_constrained = false; ///< rank == 6.
    bool over_determined = false;   ///< num_constraints > 6 && rank == 6.

    /// Which DOFs are unconstrained (true = free). Indices: tx,ty,tz,rx,ry,rz.
    /// Populated only when rank < 6.
    bool free_dofs[6] = {};
    std::vector<std::string> free_dof_names;
};

/// Result of the RPS alignment.
struct RPSAlignmentResult {
    bool success = false;

    /// The conformance-bearing transform (measured → reference).
    geometry::RigidTransform transform;

    /// Per-point residuals after alignment.
    std::vector<RPSResidual> residuals;

    /// Weighted RMS of directional residuals.
    double weighted_rms = 0;

    /// Maximum directional residual.
    double max_residual = 0;

    /// DOF constraint analysis.
    RPSDOFStatus dof_status;

    /// Number of Gauss-Newton iterations used.
    int iterations = 0;

    /// Redundant constraint count (num_constraints - 6, if over-determined).
    int redundant_constraints = 0;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Settings for RPS alignment.
struct RPSAlignmentSettings {
    /// Maximum Gauss-Newton iterations.
    int max_iterations = 50;

    /// Convergence threshold on pose increment norm.
    double convergence_threshold = 1e-12;

    /// Singular value threshold for rank determination (relative to largest SV).
    double rank_tolerance = 1e-6;

    /// Maximum acceptable weighted RMS after convergence (mm).
    /// Exceeding this triggers a warning (not failure — the solve is still valid).
    double max_rms_warning = 0.5;
};

/// Perform simultaneous weighted directional RPS alignment.
///
/// @param points    Constraint points with nominal, measured, and per-direction locks.
/// @param initial   Initial pose estimate (from pre-alignment).
/// @param settings  Solver parameters.
///
/// PRECONDITION: each point has at least one lock direction. Total directional
/// constraints must have Jacobian rank >= 6 for full constraint.
RPSAlignmentResult rps_align(
    const std::vector<RPSConstraintPoint>& points,
    const geometry::RigidTransform& initial,
    const RPSAlignmentSettings& settings = {});

} // namespace alignmesh::registration
