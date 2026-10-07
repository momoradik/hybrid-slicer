#pragma once

#include "alignmesh/geometry/reference_geometry.h"
#include "alignmesh/geometry/triangle_mesh.h"
#include "alignmesh/io/immutable_source_store.h"

#include <Eigen/Core>
#include <memory>
#include <string>
#include <vector>

namespace alignmesh::io {

// ============================================================================
// STEP IO — B-rep ingestion via OpenCASCADE
// ============================================================================
//
// Reads .step/.stp files (AP203, AP214, AP242) via OCCT STEPControl_Reader.
// Produces a CadReference (ReferenceGeometry) holding the true B-rep and
// a display tessellation. The B-rep feeds the conformance path; the
// tessellation is display-only.
//
// CRITICAL: units are normalized to mm on import. A unit error is a 25.4x
// or 1000x catastrophe — the reader asserts the declared unit and applies
// the correct scale factor.
//
// Failure modes: explicit errors for unsupported entity, no solid/shell,
// empty transfer, malformed file. NEVER a default sphere, NEVER a silent
// tessellation fallback.
// ============================================================================

/// Detailed STEP import result.
struct StepImportResult {
    bool success = false;

    /// The CadReference holding the B-rep (conformance path) and display
    /// tessellation. Null on failure.
    std::shared_ptr<geometry::ReferenceGeometry> cad_reference;

    /// Display-only tessellation for the viewer. Never feeds analysis.
    geometry::TriangleMesh display_mesh{
        Eigen::Matrix<double, 3, Eigen::Dynamic>(3, 0),
        Eigen::Matrix<int, 3, Eigen::Dynamic>(3, 0)};

    /// Source metadata for ImmutableSourceStore integration.
    SourceMetadata metadata;

    /// SHA-256 of original file bytes.
    std::string hash;

    /// Surface type summary: counts of each geometric surface type found.
    struct SurfaceSummary {
        int planes = 0;
        int cylinders = 0;
        int cones = 0;
        int spheres = 0;
        int tori = 0;
        int bspline_surfaces = 0;
        int other_surfaces = 0;
        int total_faces = 0;
        int total_shells = 0;
        int total_solids = 0;
        bool is_closed_solid = false;  // affects signed distance
    } surface_summary;

    /// Declared units in the STEP file (before normalization).
    std::string declared_unit;
    /// Scale factor applied: declared_unit -> mm.
    double unit_scale_to_mm = 1.0;

    /// PMI/datum information (if available via XCAF, AP242).
    struct PMIInfo {
        bool available = false;
        int num_dimensions = 0;
        int num_datums = 0;
        int num_tolerances = 0;
        std::vector<std::string> datum_labels;
        std::vector<std::string> details;
    } pmi;

    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// Import a STEP file. Returns the B-rep reference geometry + display mesh.
///
/// @param path         Path to .step or .stp file.
/// @param file_bytes   Raw file bytes (for SHA-256 and immutable store).
/// @param source_hash  Pre-computed SHA-256 hex of the file bytes.
///
/// Failure modes:
///   - File cannot be parsed -> error with reason
///   - No solid or shell found -> error
///   - Unknown/unsupported geometric entity -> warning (partial import)
///   - Unit ambiguity -> error (never guess)
StepImportResult import_step(
    const std::string& path,
    const std::vector<uint8_t>& file_bytes,
    const std::string& source_hash);

/// Detect whether raw bytes look like a STEP file.
/// Checks for "ISO-10303-21;" header.
bool looks_like_step(const uint8_t* data, std::size_t size);

} // namespace alignmesh::io
