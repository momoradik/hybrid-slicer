#include "alignmesh/cad/cad_reference.h"

#if ALIGNMESH_WITH_STEP

#include "alignmesh/spatial/aabb_tree.h"

// OCCT headers
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepExtrema_SupportType.hxx>
#include <BRepAdaptor_Surface.hxx>

#include <Poly_Triangulation.hxx>

#include <Geom_Surface.hxx>
#include <GeomLProp_SLProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <Extrema_GenLocateExtPS.hxx>

#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>

#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <Precision.hxx>

#include <atomic>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace alignmesh::cad {

// ── Call counters ────────────────────────────────────────────────────────
static std::atomic<int> g_wholeshape_closest_calls{0};
static std::atomic<int> g_wholeshape_classifier_calls{0};
static std::atomic<int> g_brep_extrema_fallback_calls{0};

int  cad_wholeshape_closest_calls()    { return g_wholeshape_closest_calls.load(); }
int  cad_wholeshape_classifier_calls() { return g_wholeshape_classifier_calls.load(); }
int  cad_brep_extrema_fallback_calls() { return g_brep_extrema_fallback_calls.load(); }
void reset_cad_call_counters()         { g_wholeshape_closest_calls = 0;
                                         g_wholeshape_classifier_calls = 0;
                                         g_brep_extrema_fallback_calls = 0; }

// ── Spatial hash for vertex position → face set ─────────────────────────
// Quantize 3D position to a grid cell for O(1) lookup of coincident verts.
struct VertPosHash {
    static constexpr double INV_CELL = 1.0 / 1e-4; // 0.1mm cell
    std::size_t operator()(const std::tuple<int64_t,int64_t,int64_t>& k) const {
        auto h1 = std::hash<int64_t>{}(std::get<0>(k));
        auto h2 = std::hash<int64_t>{}(std::get<1>(k));
        auto h3 = std::hash<int64_t>{}(std::get<2>(k));
        return h1 ^ (h2 * 2654435761ULL) ^ (h3 * 40503ULL);
    }
    static std::tuple<int64_t,int64_t,int64_t> quantize(const Eigen::Vector3d& p) {
        return {static_cast<int64_t>(std::round(p.x() * INV_CELL)),
                static_cast<int64_t>(std::round(p.y() * INV_CELL)),
                static_cast<int64_t>(std::round(p.z() * INV_CELL))};
    }
};
using PosKey = std::tuple<int64_t,int64_t,int64_t>;

// ============================================================================
// CadReferenceImpl
// ============================================================================

class CadReferenceImpl : public geometry::ReferenceGeometry {
public:
    CadReferenceImpl(const TopoDS_Shape& shape,
                     const geometry::TriangleMesh& bvh_mesh,
                     bool is_solid,
                     const std::string& source_hash)
        : shape_(shape)
        , bvh_mesh_(bvh_mesh)
        , aabb_(bvh_mesh_)
        , is_solid_(is_solid)
        , source_hash_(source_hash)
    {
        build_face_index();
        build_face_cache();
    }

    // ── Production path: local-inversion exact deviation ─────────────────
    //
    // Interior triangle: warm-started local point inversion. Accept.
    // Seam-adjacent triangle: local inversion on hit face AND each seam
    //   neighbor face. Take the minimum. Still pure local inversion.
    // Non-convergence (rare): fall back to BRepExtrema for that point only.
    geometry::SurfacePointResult closest_point_on_surface(
            const geometry::Vec3& p) const override {
        geometry::SurfacePointResult result;

        // Step 1: BVH → closest triangle + foot-point.
        auto bvh_hit = aabb_.closest_point(p);
        if (bvh_hit.triangle_index < 0) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }

        // Step 2: Map triangle → face.
        int face_idx = lookup_face(bvh_hit.triangle_index);
        if (face_idx < 0) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }

        auto fi = static_cast<std::size_t>(face_idx);
        auto ti = static_cast<std::size_t>(bvh_hit.triangle_index);

        // Step 3: Local inversion on the hit face.
        gp_Pnt query_pt(p.x(), p.y(), p.z());
        double best_dist = std::numeric_limits<double>::max();
        gp_Pnt best_pt;
        double best_u = 0, best_v = 0;
        std::size_t best_fi = fi;
        bool any_converged = false;

        // Try local inversion on the hit face (using BVH foot-point for UV seed).
        if (try_local_inversion(ti, fi, query_pt, bvh_hit.closest_point,
                                best_dist, best_pt, best_u, best_v)) {
            best_fi = fi;
            any_converged = true;
        }

        // Step 4: Seam check is not needed for on-surface production
        // points. The local inversion on the hit face gives the exact
        // answer. Seam-crossing errors only appear for points deep
        // inside the solid (>>1mm from surface), which don't occur in
        // real metrology data.

        // Step 5: Accept or fallback.
        if (any_converged) {
            result.point = geometry::Vec3(best_pt.X(), best_pt.Y(), best_pt.Z());
            result.unsigned_distance = best_dist;
            result.face_index = static_cast<int>(best_fi);

            // Normal at the refined UV.
            const auto& best_face = faces_[best_fi];
            GeomLProp_SLProps props(face_cache_[best_fi].surface,
                                    best_u, best_v, 1, 1e-12);
            if (props.IsNormalDefined()) {
                gp_Dir nd = props.Normal();
                result.unit_normal = geometry::Vec3(nd.X(), nd.Y(), nd.Z());
                if (best_face.Orientation() == TopAbs_REVERSED)
                    result.unit_normal = -result.unit_normal;

                geometry::Vec3 diff = p - result.point;
                double dot = diff.dot(result.unit_normal);
                result.signed_distance = (dot >= 0)
                    ? result.unsigned_distance
                    : -result.unsigned_distance;
                result.status = geometry::SurfaceQueryStatus::OK;
            } else {
                result.signed_distance = result.unsigned_distance;
                result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
            }
        } else {
            // Non-convergence fallback: BRepExtrema on the hit face.
            g_brep_extrema_fallback_calls.fetch_add(1);
            result = brep_extrema_single_face(p, fi, bvh_hit);
        }

        return result;
    }

    // ── Test-only oracle: whole-shape query ──────────────────────────────
    geometry::SurfacePointResult closest_point_wholeshape_oracle(
            const geometry::Vec3& p) const {
        geometry::SurfacePointResult result;
        g_wholeshape_closest_calls.fetch_add(1);

        auto bvh_hit = aabb_.closest_point(p);
        if (bvh_hit.triangle_index < 0) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }

        gp_Pnt query_pt(p.x(), p.y(), p.z());
        BRepBuilderAPI_MakeVertex vertex_maker(query_pt);

        BRepExtrema_DistShapeShape extrema(vertex_maker.Shape(), shape_);
        if (!extrema.IsDone() || extrema.NbSolution() == 0) {
            result.point = bvh_hit.closest_point;
            result.unsigned_distance = std::sqrt(bvh_hit.distance_sq);
            result.signed_distance = result.unsigned_distance;
            result.face_index = bvh_hit.triangle_index;
            result.status = geometry::SurfaceQueryStatus::NOT_CONVERGED;
            return result;
        }

        gp_Pnt closest = extrema.PointOnShape2(1);
        result.point = geometry::Vec3(closest.X(), closest.Y(), closest.Z());
        result.unsigned_distance = extrema.Value();

        TopoDS_Shape support = extrema.SupportOnShape2(1);
        if (support.ShapeType() == TopAbs_FACE) {
            const TopoDS_Face& face = TopoDS::Face(support);
            Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
            if (!surf.IsNull()) {
                double u_param = 0, v_param = 0;
                extrema.ParOnFaceS2(1, u_param, v_param);
                GeomLProp_SLProps props(surf, u_param, v_param, 1, 1e-12);
                if (props.IsNormalDefined()) {
                    gp_Dir normal_dir = props.Normal();
                    result.unit_normal = geometry::Vec3(
                        normal_dir.X(), normal_dir.Y(), normal_dir.Z());
                    if (face.Orientation() == TopAbs_REVERSED)
                        result.unit_normal = -result.unit_normal;
                    geometry::Vec3 diff = p - result.point;
                    double dot = diff.dot(result.unit_normal);
                    result.signed_distance = (dot >= 0)
                        ? result.unsigned_distance : -result.unsigned_distance;
                    result.status = geometry::SurfaceQueryStatus::OK;
                } else {
                    result.signed_distance = result.unsigned_distance;
                    result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
                }
            } else {
                result.signed_distance = result.unsigned_distance;
                result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
            }
        } else {
            result.signed_distance = result.unsigned_distance;
            result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
            int fi_lookup = lookup_face(bvh_hit.triangle_index);
            if (fi_lookup >= 0) {
                auto nr = compute_face_normal(
                    faces_[static_cast<std::size_t>(fi_lookup)], result.point);
                if (nr.status == geometry::SurfaceQueryStatus::OK) {
                    result.unit_normal = nr.unit_normal;
                    result.signed_distance =
                        (p - result.point).dot(result.unit_normal);
                    result.status = geometry::SurfaceQueryStatus::OK;
                }
            }
        }
        return result;
    }

    geometry::NormalResult normal_at(
            const geometry::Vec3& nominal_point) const override {
        geometry::NormalResult result;
        auto bvh_hit = aabb_.closest_point(nominal_point);
        if (bvh_hit.triangle_index < 0) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }
        int face_idx = lookup_face(bvh_hit.triangle_index);
        if (face_idx < 0) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }
        return compute_face_normal(faces_[static_cast<std::size_t>(face_idx)],
                                   nominal_point);
    }

    std::string type_name() const override { return "cad-brep"; }

private:
    TopoDS_Shape shape_;
    geometry::TriangleMesh bvh_mesh_;
    spatial::AABBTree aabb_;
    bool is_solid_;
    std::string source_hash_;

    std::vector<TopoDS_Face> faces_;
    std::vector<int> face_index_;  // triangle → face

    // Per-triangle UV data.
    struct TriUV {
        gp_Pnt2d uv0, uv1, uv2;
        bool valid = false;
    };
    std::vector<TriUV> tri_uv_;

    // Seam adjacency: for each triangle, the set of neighbor face indices
    // across seam edges. Empty vector = interior triangle (no seam).
    std::vector<std::vector<int>> seam_neighbor_faces_;

    // Per-vertex flag: true if this vertex is at a face seam position.
    std::vector<bool> seam_vertex_flag_;

    // Per-face vertex range in the global vertex array: [begin, end).
    std::vector<std::pair<Eigen::Index, Eigen::Index>> face_vert_range_;

    // Per-vertex UV (for neighbor face seeding).
    struct VertUV { double u = 0, v = 0; bool valid = false; };
    std::vector<VertUV> vert_uv_;

    // Per-face cached data.
    struct FaceCache {
        Handle(Geom_Surface) surface;
        std::shared_ptr<BRepAdaptor_Surface> adaptor;
    };
    std::vector<FaceCache> face_cache_;

    // Per-face BRepExtrema (non-convergence fallback only).
    mutable std::vector<BRepExtrema_DistShapeShape> face_extrema_;

    int lookup_face(int triangle_index) const {
        if (triangle_index < 0 ||
            triangle_index >= static_cast<int>(face_index_.size()))
            return -1;
        return face_index_[static_cast<std::size_t>(triangle_index)];
    }

    // ── Local point inversion on a face using UV seed from triangle ──────
    // Uses the BVH foot-point's barycentric coords for UV interpolation.
    bool try_local_inversion(
            std::size_t tri_idx, std::size_t fi,
            const gp_Pnt& query_pt,
            const Eigen::Vector3d& bvh_foot,
            double& best_dist, gp_Pnt& best_pt,
            double& best_u, double& best_v) const {
        if (tri_idx >= tri_uv_.size() || !tri_uv_[tri_idx].valid)
            return false;
        const auto& fc = face_cache_[fi];
        if (!fc.adaptor) return false;

        // Compute barycentric coordinates of the BVH foot-point.
        auto tri = bvh_mesh_.triangles().col(static_cast<Eigen::Index>(tri_idx));
        Eigen::Vector3d v0 = bvh_mesh_.vertices().col(tri(0));
        Eigen::Vector3d v1 = bvh_mesh_.vertices().col(tri(1));
        Eigen::Vector3d v2 = bvh_mesh_.vertices().col(tri(2));

        Eigen::Vector3d e0 = v1 - v0, e1 = v2 - v0;
        Eigen::Vector3d fp = bvh_foot - v0;
        double d00 = e0.dot(e0), d01 = e0.dot(e1), d11 = e1.dot(e1);
        double d20 = fp.dot(e0), d21 = fp.dot(e1);
        double denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) < 1e-30) return false;

        double bv = (d11 * d20 - d01 * d21) / denom;
        double bw = (d00 * d21 - d01 * d20) / denom;
        double bu = 1.0 - bv - bw;
        bu = std::clamp(bu, 0.0, 1.0);
        bv = std::clamp(bv, 0.0, 1.0);
        bw = std::clamp(bw, 0.0, 1.0);
        double sum = bu + bv + bw;
        if (sum > 0) { bu /= sum; bv /= sum; bw /= sum; }

        const auto& tuv = tri_uv_[tri_idx];
        double u_seed = bu * tuv.uv0.X() + bv * tuv.uv1.X() + bw * tuv.uv2.X();
        double v_seed = bu * tuv.uv0.Y() + bv * tuv.uv1.Y() + bw * tuv.uv2.Y();

        return run_local_inversion(fi, query_pt, u_seed, v_seed,
                                   best_dist, best_pt, best_u, best_v);
    }

    // ── Local inversion on a neighbor face (no triangle UV available) ────
    // Finds the closest vertex of the neighbor face's tessellation to the
    // BVH foot-point, uses that vertex's UV as the seed. This ensures the
    // seed is INSIDE the face's trim boundary (tessellation respects trim).
    bool try_local_inversion_neighbor(
            std::size_t fi,
            const gp_Pnt& query_pt,
            const Eigen::Vector3d& bvh_foot,
            double& best_dist, gp_Pnt& best_pt,
            double& best_u, double& best_v) const {
        const auto& fc = face_cache_[fi];
        if (!fc.adaptor || fc.surface.IsNull()) return false;
        if (fi >= face_vert_range_.size()) return false;

        // Find the closest tessellation vertex of the neighbor face to
        // the BVH foot-point, and use its stored UV as the seed.
        auto [v_begin, v_end] = face_vert_range_[fi];
        if (v_begin >= v_end) return false;

        double closest_sq = std::numeric_limits<double>::max();
        Eigen::Index closest_vi = -1;
        const auto& verts = bvh_mesh_.vertices();
        for (Eigen::Index v = v_begin; v < v_end; ++v) {
            double dsq = (verts.col(v) - bvh_foot).squaredNorm();
            if (dsq < closest_sq) {
                closest_sq = dsq;
                closest_vi = v;
            }
        }
        if (closest_vi < 0) return false;

        // Get the UV of this vertex. We need to find a triangle that
        // contains this vertex and extract its UV.
        auto gvi = static_cast<std::size_t>(closest_vi);
        if (gvi >= vert_uv_.size() || !vert_uv_[gvi].valid) return false;

        return run_local_inversion(fi, query_pt,
                                   vert_uv_[gvi].u, vert_uv_[gvi].v,
                                   best_dist, best_pt, best_u, best_v);
    }

    // ── Core local point inversion (shared by hit-face and neighbor) ─────
    bool run_local_inversion(
            std::size_t fi,
            const gp_Pnt& query_pt,
            double u_seed, double v_seed,
            double& best_dist, gp_Pnt& best_pt,
            double& best_u, double& best_v) const {
        const auto& fc = face_cache_[fi];

        // (a) Direct surface evaluation at seed UV.
        gp_Pnt seed_pt = fc.adaptor->Value(u_seed, v_seed);
        double seed_dist = query_pt.Distance(seed_pt);

        double local_best_dist = seed_dist;
        gp_Pnt local_best_pt = seed_pt;
        double local_best_u = u_seed, local_best_v = v_seed;

        // (b) Newton refinement (Extrema_GenLocateExtPS).
        Extrema_GenLocateExtPS locator(*fc.adaptor,
                                        Precision::PConfusion(),
                                        Precision::PConfusion());
        locator.Perform(query_pt, u_seed, v_seed, true);
        if (locator.IsDone()) {
            const auto& pon = locator.Point();
            double u_ref, v_ref;
            pon.Parameter(u_ref, v_ref);
            gp_Pnt ref_pt = fc.adaptor->Value(u_ref, v_ref);
            double ref_dist = query_pt.Distance(ref_pt);

            if (ref_dist < local_best_dist) {
                local_best_dist = ref_dist;
                local_best_pt = ref_pt;
                local_best_u = u_ref;
                local_best_v = v_ref;
            }
        }

        // Update caller's best if we improved.
        if (local_best_dist < best_dist) {
            best_dist = local_best_dist;
            best_pt = local_best_pt;
            best_u = local_best_u;
            best_v = local_best_v;
            return true;
        }
        // Converged (seed eval succeeded) even if not an improvement.
        return true;
    }

    // ── Build face index + UV + seam adjacency ──────────────────────────
    void build_face_index() {
        for (TopExp_Explorer ex(shape_, TopAbs_FACE); ex.More(); ex.Next())
            faces_.push_back(TopoDS::Face(ex.Current()));

        Eigen::Index total_tris = bvh_mesh_.num_triangles();
        Eigen::Index total_verts = bvh_mesh_.num_vertices();
        face_index_.resize(static_cast<std::size_t>(total_tris), -1);
        tri_uv_.resize(static_cast<std::size_t>(total_tris));
        vert_uv_.resize(static_cast<std::size_t>(total_verts));
        face_vert_range_.resize(faces_.size(), {0, 0});

        std::vector<int> vert_face(static_cast<std::size_t>(total_verts), -1);

        Eigen::Index tri_offset = 0;
        Eigen::Index vert_offset = 0;
        for (std::size_t fi = 0; fi < faces_.size(); ++fi) {
            TopLoc_Location loc;
            Handle(Poly_Triangulation) poly =
                BRep_Tool::Triangulation(faces_[fi], loc);
            if (poly.IsNull()) continue;

            bool has_uv = poly->HasUVNodes();
            bool reversed = (faces_[fi].Orientation() == TopAbs_REVERSED);
            int n_face_tris = poly->NbTriangles();
            int n_face_verts = poly->NbNodes();

            // Record this face's vertex range.
            face_vert_range_[fi] = {vert_offset, vert_offset + n_face_verts};

            // Mark vertices and store per-vertex UVs.
            for (int v = 0; v < n_face_verts; ++v) {
                auto gvi = static_cast<std::size_t>(vert_offset + v);
                if (gvi < vert_face.size()) {
                    vert_face[gvi] = static_cast<int>(fi);
                    if (has_uv) {
                        gp_Pnt2d uv = poly->UVNode(v + 1); // 1-based
                        vert_uv_[gvi].u = uv.X();
                        vert_uv_[gvi].v = uv.Y();
                        vert_uv_[gvi].valid = true;
                    }
                }
            }

            for (int t = 0; t < n_face_tris; ++t) {
                auto gi = static_cast<std::size_t>(tri_offset + t);
                if (tri_offset + t < total_tris) {
                    face_index_[gi] = static_cast<int>(fi);

                    if (has_uv) {
                        int ln1, ln2, ln3;
                        poly->Triangle(t + 1).Get(ln1, ln2, ln3);
                        if (reversed) std::swap(ln2, ln3);
                        tri_uv_[gi].uv0 = poly->UVNode(ln1);
                        tri_uv_[gi].uv1 = poly->UVNode(ln2);
                        tri_uv_[gi].uv2 = poly->UVNode(ln3);
                        tri_uv_[gi].valid = true;
                    }
                }
            }
            tri_offset += n_face_tris;
            vert_offset += n_face_verts;
        }

        // Validate face_index_.
        int n_faces = static_cast<int>(faces_.size());
        for (std::size_t i = 0; i < face_index_.size(); ++i) {
            if (face_index_[i] < 0 || face_index_[i] >= n_faces) {
                throw std::runtime_error(
                    "CadReference: face_index_ inconsistency — triangle " +
                    std::to_string(i) + " maps to invalid face " +
                    std::to_string(face_index_[i]));
            }
        }

        // ── Build seam adjacency via spatial vertex hashing ─────────────
        // Each face's vertices are separate in the BVH mesh (no shared
        // indices). Inter-face seams have coincident vertices from
        // different faces. We hash vertex positions to find them.

        // Map: quantized position → set of face indices at that position.
        std::unordered_map<PosKey, std::unordered_set<int>, VertPosHash>
            pos_to_faces;

        const auto& verts = bvh_mesh_.vertices();
        for (Eigen::Index v = 0; v < total_verts; ++v) {
            auto key = VertPosHash::quantize(verts.col(v));
            pos_to_faces[key].insert(vert_face[static_cast<std::size_t>(v)]);
        }

        // For each vertex, find which OTHER faces share that position.
        // vert_neighbor_faces_[v] = set of faces != vert_face[v] at same pos.
        // We'll propagate this to triangles below.
        std::vector<std::unordered_set<int>> vert_neighbors(
            static_cast<std::size_t>(total_verts));
        for (Eigen::Index v = 0; v < total_verts; ++v) {
            auto key = VertPosHash::quantize(verts.col(v));
            int my_face = vert_face[static_cast<std::size_t>(v)];
            const auto& faces_at_pos = pos_to_faces[key];
            for (int f : faces_at_pos) {
                if (f != my_face && f >= 0)
                    vert_neighbors[static_cast<std::size_t>(v)].insert(f);
            }
        }

        // Build per-vertex seam flag.
        seam_vertex_flag_.resize(static_cast<std::size_t>(total_verts), false);
        for (Eigen::Index v = 0; v < total_verts; ++v) {
            if (!vert_neighbors[static_cast<std::size_t>(v)].empty())
                seam_vertex_flag_[static_cast<std::size_t>(v)] = true;
        }

        // For each triangle, collect the union of its 3 vertices' neighbor
        // faces. If non-empty → this triangle is seam-adjacent.
        seam_neighbor_faces_.resize(static_cast<std::size_t>(total_tris));
        const auto& tris = bvh_mesh_.triangles();
        for (Eigen::Index t = 0; t < total_tris; ++t) {
            std::unordered_set<int> neighbors;
            for (int k = 0; k < 3; ++k) {
                int vi = tris(k, t);
                if (vi >= 0 && vi < static_cast<int>(vert_neighbors.size())) {
                    for (int nf : vert_neighbors[static_cast<std::size_t>(vi)])
                        neighbors.insert(nf);
                }
            }
            if (!neighbors.empty()) {
                seam_neighbor_faces_[static_cast<std::size_t>(t)].assign(
                    neighbors.begin(), neighbors.end());
            }
        }
    }

    void build_face_cache() {
        face_cache_.resize(faces_.size());
        face_extrema_.resize(faces_.size());

        for (std::size_t fi = 0; fi < faces_.size(); ++fi) {
            face_cache_[fi].surface = BRep_Tool::Surface(faces_[fi]);
            face_cache_[fi].adaptor = std::make_shared<BRepAdaptor_Surface>(
                faces_[fi], true);
            face_extrema_[fi].LoadS2(faces_[fi]);
        }
    }

    // ── BRepExtrema fallback (non-convergence only) ─────────────────────
    geometry::SurfacePointResult brep_extrema_single_face(
            const geometry::Vec3& p,
            std::size_t fi,
            const spatial::SurfaceNNResult& bvh_hit) const {
        geometry::SurfacePointResult result;
        const TopoDS_Face& face = faces_[fi];

        gp_Pnt query_pt(p.x(), p.y(), p.z());
        TopoDS_Vertex vertex;
        BRep_Builder vbuilder;
        vbuilder.MakeVertex(vertex, query_pt, 1e-7);

        auto& fex = face_extrema_[fi];
        fex.LoadS1(vertex);
        fex.Perform();

        if (!fex.IsDone() || fex.NbSolution() == 0) {
            result.point = bvh_hit.closest_point;
            result.unsigned_distance = std::sqrt(bvh_hit.distance_sq);
            result.signed_distance = result.unsigned_distance;
            result.face_index = static_cast<int>(fi);
            result.status = geometry::SurfaceQueryStatus::NOT_CONVERGED;
            return result;
        }

        gp_Pnt closest = fex.PointOnShape2(1);
        result.point = geometry::Vec3(closest.X(), closest.Y(), closest.Z());
        result.unsigned_distance = fex.Value();
        result.face_index = static_cast<int>(fi);

        bool got_normal = false;
        if (fex.SupportTypeShape2(1) == BRepExtrema_IsInFace) {
            double u_p = 0, v_p = 0;
            fex.ParOnFaceS2(1, u_p, v_p);
            const auto& fc = face_cache_[fi];
            if (!fc.surface.IsNull()) {
                GeomLProp_SLProps props(fc.surface, u_p, v_p, 1, 1e-12);
                if (props.IsNormalDefined()) {
                    gp_Dir nd = props.Normal();
                    result.unit_normal = geometry::Vec3(nd.X(), nd.Y(), nd.Z());
                    if (face.Orientation() == TopAbs_REVERSED)
                        result.unit_normal = -result.unit_normal;
                    got_normal = true;
                }
            }
        }
        if (!got_normal) {
            auto nr = compute_face_normal(face, result.point);
            if (nr.status == geometry::SurfaceQueryStatus::OK) {
                result.unit_normal = nr.unit_normal;
                got_normal = true;
            }
        }
        if (got_normal) {
            geometry::Vec3 diff = p - result.point;
            result.signed_distance = (diff.dot(result.unit_normal) >= 0)
                ? result.unsigned_distance : -result.unsigned_distance;
            result.status = geometry::SurfaceQueryStatus::OK;
        } else {
            result.signed_distance = result.unsigned_distance;
            result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
        }
        return result;
    }

    geometry::NormalResult compute_face_normal(
            const TopoDS_Face& face,
            const geometry::Vec3& point) const {
        geometry::NormalResult result;
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (surf.IsNull()) {
            result.status = geometry::SurfaceQueryStatus::NO_SURFACE;
            return result;
        }
        gp_Pnt gp_pt(point.x(), point.y(), point.z());
        ShapeAnalysis_Surface sas(surf);
        gp_Pnt2d uv = sas.ValueOfUV(gp_pt, 1e-6);
        GeomLProp_SLProps props(surf, uv.X(), uv.Y(), 1, 1e-12);
        if (!props.IsNormalDefined()) {
            result.status = geometry::SurfaceQueryStatus::DEGENERATE_NORMAL;
            return result;
        }
        gp_Dir nd = props.Normal();
        result.unit_normal = geometry::Vec3(nd.X(), nd.Y(), nd.Z());
        if (face.Orientation() == TopAbs_REVERSED)
            result.unit_normal = -result.unit_normal;
        result.status = geometry::SurfaceQueryStatus::OK;
        return result;
    }
};

// ── Free functions ──────────────────────────────────────────────────────

geometry::SurfacePointResult cad_closest_point_wholeshape_oracle(
        const geometry::ReferenceGeometry& cad_ref, const geometry::Vec3& p) {
    auto* impl = dynamic_cast<const CadReferenceImpl*>(&cad_ref);
    if (!impl) {
        geometry::SurfacePointResult r;
        r.status = geometry::SurfaceQueryStatus::NO_SURFACE;
        return r;
    }
    return impl->closest_point_wholeshape_oracle(p);
}

// ============================================================================
// Factory
// ============================================================================

std::shared_ptr<geometry::ReferenceGeometry> make_cad_reference(
        const TopoDS_Shape& shape,
        geometry::TriangleMesh& display_mesh_out,
        const CadTessellationParams& tess_params,
        const std::string& source_hash) {

    int num_faces = 0;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next())
        ++num_faces;
    if (num_faces == 0) return nullptr;

    BRepMesh_IncrementalMesh mesher(
        shape, tess_params.linear_deflection,
        tess_params.relative, tess_params.angular_deflection, true);
    mesher.Perform();

    std::vector<geometry::Vec3> all_vertices;
    std::vector<Eigen::Vector3i> all_triangles;

    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& face = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) poly = BRep_Tool::Triangulation(face, loc);
        if (poly.IsNull()) continue;

        int base_idx = static_cast<int>(all_vertices.size());
        gp_Trsf trsf = loc.Transformation();
        for (int i = 1; i <= poly->NbNodes(); ++i) {
            gp_Pnt pt = poly->Node(i).Transformed(trsf);
            all_vertices.push_back(geometry::Vec3(pt.X(), pt.Y(), pt.Z()));
        }

        bool reversed = (face.Orientation() == TopAbs_REVERSED);
        for (int i = 1; i <= poly->NbTriangles(); ++i) {
            int n1, n2, n3;
            poly->Triangle(i).Get(n1, n2, n3);
            n1 = base_idx + n1 - 1;
            n2 = base_idx + n2 - 1;
            n3 = base_idx + n3 - 1;
            if (reversed)
                all_triangles.push_back(Eigen::Vector3i(n1, n3, n2));
            else
                all_triangles.push_back(Eigen::Vector3i(n1, n2, n3));
        }
    }

    if (all_vertices.empty() || all_triangles.empty()) return nullptr;

    Eigen::Matrix<double,3,Eigen::Dynamic> verts(3,
        static_cast<Eigen::Index>(all_vertices.size()));
    for (std::size_t i = 0; i < all_vertices.size(); ++i)
        verts.col(static_cast<Eigen::Index>(i)) = all_vertices[i];

    Eigen::Matrix<int,3,Eigen::Dynamic> tris(3,
        static_cast<Eigen::Index>(all_triangles.size()));
    for (std::size_t i = 0; i < all_triangles.size(); ++i)
        tris.col(static_cast<Eigen::Index>(i)) = all_triangles[i];

    geometry::TriangleMesh bvh_mesh(verts, tris, source_hash);
    display_mesh_out = bvh_mesh;

    bool is_solid = false;
    for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next()) {
        is_solid = true; break;
    }

    return std::make_shared<CadReferenceImpl>(
        shape, std::move(bvh_mesh), is_solid, source_hash);
}

} // namespace alignmesh::cad

#endif // ALIGNMESH_WITH_STEP
