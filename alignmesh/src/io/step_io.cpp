#include "alignmesh/io/step_io.h"

#include <cstring>

namespace alignmesh::io {

bool looks_like_step(const uint8_t* data, std::size_t size) {
    // STEP files begin with "ISO-10303-21;" (ISO 10303-21 Part 21 format).
    // May have a UTF-8 BOM or whitespace before the header.
    if (size < 14) return false;

    // Skip UTF-8 BOM if present.
    std::size_t offset = 0;
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        offset = 3;
    }

    // Skip leading whitespace.
    while (offset < size && (data[offset] == ' ' || data[offset] == '\t' ||
                              data[offset] == '\r' || data[offset] == '\n')) {
        ++offset;
    }

    if (offset + 14 > size) return false;
    return std::memcmp(data + offset, "ISO-10303-21;", 13) == 0;
}

} // namespace alignmesh::io

// ============================================================================
// OCCT-dependent implementation — compiled only when ALIGNMESH_WITH_STEP=1
// ============================================================================

#if ALIGNMESH_WITH_STEP

#include "alignmesh/cad/cad_reference.h"
#include "alignmesh/io/sha256.h"

// OCCT headers
#include <STEPControl_Reader.hxx>
#include <StepData_StepModel.hxx>
#include <XSControl_WorkSession.hxx>
#include <Transfer_TransientProcess.hxx>
#include <Interface_Static.hxx>

#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Compound.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>

#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <Geom_Surface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_BSplineSurface.hxx>

#include <ShapeAnalysis_ShapeContents.hxx>
#include <BRepCheck_Analyzer.hxx>

#if ALIGNMESH_HAS_XCAF
#include <STEPCAFControl_Reader.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_DimTolTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFApp_Application.hxx>
#include <NCollection_Sequence.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#endif

#include <cmath>
#include <algorithm>

namespace alignmesh::io {

// Classify face surface types for the summary.
static void classify_surfaces(const TopoDS_Shape& shape,
                              StepImportResult::SurfaceSummary& summary) {
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (surf.IsNull()) continue;

        summary.total_faces++;

        if (surf->IsKind(STANDARD_TYPE(Geom_Plane)))
            summary.planes++;
        else if (surf->IsKind(STANDARD_TYPE(Geom_CylindricalSurface)))
            summary.cylinders++;
        else if (surf->IsKind(STANDARD_TYPE(Geom_ConicalSurface)))
            summary.cones++;
        else if (surf->IsKind(STANDARD_TYPE(Geom_SphericalSurface)))
            summary.spheres++;
        else if (surf->IsKind(STANDARD_TYPE(Geom_ToroidalSurface)))
            summary.tori++;
        else if (surf->IsKind(STANDARD_TYPE(Geom_BSplineSurface)))
            summary.bspline_surfaces++;
        else
            summary.other_surfaces++;
    }

    for (TopExp_Explorer ex(shape, TopAbs_SHELL); ex.More(); ex.Next())
        summary.total_shells++;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next())
        summary.total_solids++;

    summary.is_closed_solid = (summary.total_solids > 0);
}

// Detect the declared unit from the STEP file and return the scale to mm.
// CRITICAL: a unit error is a 25.4x or 1000x catastrophe.
static bool detect_unit_scale(STEPControl_Reader& /*reader*/,
                              std::string& declared_unit,
                              double& scale_to_mm,
                              std::string& /*error*/) {
    // OCCT reads the STEP file's unit context and stores the scale factor.
    // Interface_Static::RVal("xstep.cascade.unit") gives the system unit in mm.
    // The reader automatically scales to the system unit (mm by default).
    //
    // We verify by reading the unit from the STEP model.
    double system_unit = Interface_Static::RVal("xstep.cascade.unit");
    if (system_unit <= 0) {
        // Default OCCT system unit is mm (1.0).
        system_unit = 1.0;
    }

    // OCCT normalizes to its system unit on transfer. If system unit is mm,
    // the shape coordinates are already in mm after TransferRoots().
    // We record the file's original unit for traceability.

    // Try to identify the original unit from the STEP model.
    // OCCT doesn't expose the raw file unit cleanly; we rely on the
    // fact that TransferRoots applies the correct scale. Record the
    // system unit for traceability.

    // Read the length unit that OCCT detected from the file.
    // Interface_Static::CVal("read.step.unit") returns "MM", "INCH", "M", etc.
    // when available. This is set during the Read() call.
    const char* step_unit = Interface_Static::CVal("read.step.unit");
    if (step_unit && std::strlen(step_unit) > 0) {
        declared_unit = step_unit;
    } else {
        declared_unit = "MM (assumed)";
    }

    // OCCT converts to its system unit on transfer. We ensure the system
    // unit is mm (= 1.0 in OCCT's internal scale where 1.0 = mm).
    if (std::abs(system_unit - 1.0) > 1e-6) {
        // System unit is not mm — scale the result.
        scale_to_mm = system_unit;
        declared_unit += " (system unit scaled to mm)";
    } else {
        scale_to_mm = 1.0;
    }

    return true;
}

StepImportResult import_step(
        const std::string& path,
        const std::vector<uint8_t>& /*file_bytes*/,
        const std::string& source_hash) {

    StepImportResult result;
    result.hash = source_hash;

    // Ensure OCCT system unit is mm.
    Interface_Static::SetRVal("xstep.cascade.unit", 1.0);
    // Force unit conversion on read.
    Interface_Static::SetIVal("read.step.unit", 0); // 0 = use file's unit

    STEPControl_Reader reader;
    IFSelect_ReturnStatus status = reader.ReadFile(path.c_str());

    if (status != IFSelect_RetDone) {
        switch (status) {
        case IFSelect_RetError:
            result.errors.push_back("STEP reader: syntax error in file");
            break;
        case IFSelect_RetFail:
            result.errors.push_back("STEP reader: file read failed (I/O or format error)");
            break;
        case IFSelect_RetVoid:
            result.errors.push_back("STEP reader: file is empty or contains no data");
            break;
        default:
            result.errors.push_back("STEP reader: unknown error (status=" +
                std::to_string(static_cast<int>(status)) + ")");
            break;
        }
        return result;
    }

    // Check root count.
    int num_roots = reader.NbRootsForTransfer();
    if (num_roots == 0) {
        result.errors.push_back("STEP file contains no transferable roots (no geometry)");
        return result;
    }

    // Transfer all roots.
    int transferred = reader.TransferRoots();
    if (transferred == 0) {
        result.errors.push_back("STEP transfer failed: no shapes could be converted");
        return result;
    }

    // Get the combined shape.
    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        result.errors.push_back("STEP transfer produced a null shape");
        return result;
    }

    // Detect and record units.
    {
        std::string unit_error;
        if (!detect_unit_scale(reader, result.declared_unit,
                               result.unit_scale_to_mm, unit_error)) {
            result.errors.push_back("Unit detection failed: " + unit_error);
            return result;
        }
    }

    // Classify surfaces.
    classify_surfaces(shape, result.surface_summary);

    if (result.surface_summary.total_faces == 0) {
        result.errors.push_back("STEP shape has no faces — cannot be used as a reference");
        return result;
    }

    if (result.surface_summary.total_solids == 0 &&
        result.surface_summary.total_shells == 0) {
        result.warnings.push_back(
            "STEP shape has faces but no solid or shell — signed distance "
            "may be unreliable (open geometry)");
    }

    // Compute bounding box.
    Bnd_Box bbox;
    BRepBndLib::Add(shape, bbox);
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    // Build metadata.
    result.metadata.format = "step-brep";
    result.metadata.coordinate_precision = "double-analytic";
    result.metadata.vertex_count = 0;  // B-rep, not vertex-based
    result.metadata.triangle_count = 0;
    result.metadata.bbox_min = geometry::Vec3(xmin, ymin, zmin);
    result.metadata.bbox_max = geometry::Vec3(xmax, ymax, zmax);
    result.metadata.units = "mm";

    // Build CadReference + display tessellation.
    cad::CadTessellationParams tess_params;
    // Set linear deflection relative to part size for robust BVH locality.
    double diag = (result.metadata.bbox_max - result.metadata.bbox_min).norm();
    tess_params.linear_deflection = std::max(0.001, diag * 0.0005); // 0.05% of diagonal

    geometry::TriangleMesh display_mesh(
        Eigen::Matrix<double, 3, Eigen::Dynamic>(3, 0),
        Eigen::Matrix<int, 3, Eigen::Dynamic>(3, 0));

    result.cad_reference = cad::make_cad_reference(
        shape, display_mesh, tess_params, source_hash);

    if (!result.cad_reference) {
        result.errors.push_back("Failed to build CadReference from STEP shape");
        return result;
    }

    result.display_mesh = std::move(display_mesh);
    result.metadata.vertex_count = static_cast<std::size_t>(
        result.display_mesh.num_vertices());
    result.metadata.triangle_count = static_cast<std::size_t>(
        result.display_mesh.num_triangles());

    // PMI reading (optional, AP242).
#if ALIGNMESH_HAS_XCAF
    try {
        Handle(TDocStd_Document) doc;
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        app->NewDocument("MDTV-XCAF", doc);

        STEPCAFControl_Reader caf_reader;
        caf_reader.SetNameMode(true);
        caf_reader.SetGDTMode(true);

        if (caf_reader.ReadFile(path.c_str()) == IFSelect_RetDone) {
            if (caf_reader.Transfer(doc)) {
                Handle(XCAFDoc_DimTolTool) dtt =
                    XCAFDoc_DimTolTool::Set(doc->Main());
                if (!dtt.IsNull()) {
                    NCollection_Sequence<TDF_Label> dims, datums, tols;
                    dtt->GetDimensionLabels(dims);
                    dtt->GetDatumLabels(datums);
                    dtt->GetGeomToleranceLabels(tols);

                    result.pmi.available = true;
                    result.pmi.num_dimensions = dims.Length();
                    result.pmi.num_datums = datums.Length();
                    result.pmi.num_tolerances = tols.Length();

                    for (int i = 1; i <= datums.Length(); ++i) {
                        Handle(TDataStd_Name) name;
                        if (datums.Value(i).FindAttribute(TDataStd_Name::GetID(), name)) {
                            TCollection_ExtendedString es = name->Get();
                            std::string label(es.ToExtString(),
                                              es.ToExtString() + es.Length());
                            result.pmi.datum_labels.push_back(label);
                        }
                    }

                    result.pmi.details.push_back(
                        "PMI: " + std::to_string(result.pmi.num_dimensions) +
                        " dimensions, " + std::to_string(result.pmi.num_datums) +
                        " datums, " + std::to_string(result.pmi.num_tolerances) +
                        " tolerances");
                }
            }
        }
    } catch (...) {
        result.warnings.push_back("PMI reading failed (non-blocking)");
    }
#endif

    result.success = true;
    return result;
}

} // namespace alignmesh::io

#else // !ALIGNMESH_WITH_STEP

namespace alignmesh::io {

StepImportResult import_step(
        const std::string& /*path*/,
        const std::vector<uint8_t>& /*file_bytes*/,
        const std::string& /*source_hash*/) {
    StepImportResult result;
    result.errors.push_back(
        "STEP support not compiled — rebuild with -DALIGNMESH_WITH_STEP=ON "
        "and OpenCASCADE installed");
    return result;
}

} // namespace alignmesh::io

#endif // ALIGNMESH_WITH_STEP
