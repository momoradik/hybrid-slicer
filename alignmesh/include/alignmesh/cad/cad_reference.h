#pragma once

#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/triangle_mesh.h"

#include <memory>
#include <string>
#include <vector>

// Forward-declare OCCT types to avoid exposing OCCT headers in this header.
// The implementation (.cpp) includes the real OCCT headers.
#if ALIGNMESH_WITH_STEP
class TopoDS_Shape;
#endif

namespace alignmesh::cad {

// ============================================================================
// CadReference — ReferenceGeometry backed by an OCCT B-rep
// ============================================================================
//
// True-surface deviation: BVH over a fine deterministic tessellation for
// candidate-face locality, then analytic refinement to the true trimmed
// face. This removes tessellation discretization bias for tight tolerances.
//
// True-surface normals: analytic B-rep face normal at any point, used by
// RPS for the nominal-normal direction (instead of facet normals).
//
// The tessellation is produced at construction with explicit, auditable
// metrology parameters (linear deflection, angular deflection). It is
// NEVER used for conformance — only for BVH locality and display.
//
// PERFORMANCE: closest_point_on_surface uses BVH triangle→face localization
// to project onto a SINGLE candidate face per query. No whole-shape OCCT
// operations (BRepExtrema on shape_, BRepClass3d_SolidClassifier) are called
// per point. Sign is determined from the localized face's outward normal.
//
// SAFETY: REVERSED faces flip the outward normal. The closest point must
// respect the face's trim boundaries.
// ============================================================================

/// Tessellation parameters for the BVH locality mesh.
/// These control the density of the internal tessellation used to find
/// the candidate face quickly. They do NOT affect the analytic result.
struct CadTessellationParams {
    /// Maximum chord deviation from the true surface (mm).
    /// Smaller = denser mesh, better BVH locality, slower build.
    double linear_deflection = 0.01;  // 10 µm

    /// Maximum angular deviation between adjacent facet normals (radians).
    double angular_deflection = 0.5;  // ~29°

    /// If true, use relative deflection (fraction of edge length).
    bool relative = false;
};

/// Create a CadReference from an OCCT shape.
///
/// @param shape              The OCCT TopoDS_Shape (must be a solid or shell).
/// @param display_mesh       Output: display tessellation for the viewer.
/// @param tess_params        Tessellation parameters for BVH locality mesh.
/// @param source_hash        SHA-256 of the original file for provenance.
///
/// Returns nullptr if the shape has no faces.
#if ALIGNMESH_WITH_STEP
std::shared_ptr<geometry::ReferenceGeometry> make_cad_reference(
    const TopoDS_Shape& shape,
    geometry::TriangleMesh& display_mesh_out,
    const CadTessellationParams& tess_params = {},
    const std::string& source_hash = {});

// ── Test / debug instrumentation ─────────────────────────────────────────

/// Test-only oracle: whole-shape closest-point query (the old O(N_faces) path).
/// NEVER called from production paths. Exists only for T1 numerical-equivalence
/// testing against the localized path. The ref must be a CadReference.
geometry::SurfacePointResult cad_closest_point_wholeshape_oracle(
    const geometry::ReferenceGeometry& cad_ref, const geometry::Vec3& p);

/// Call counters for T2 (mechanism gate). Count how many times whole-shape
/// OCCT operations are invoked. Reset before running a test, then assert == 0.
int cad_wholeshape_closest_calls();
int cad_wholeshape_classifier_calls();
/// Count of per-face BRepExtrema fallback calls (local refine failed).
/// Should be a small fraction of total queries — not zero (edge cases), not all.
int cad_brep_extrema_fallback_calls();
void reset_cad_call_counters();
#endif

} // namespace alignmesh::cad
