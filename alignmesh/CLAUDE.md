# CLAUDE.md — Standing rules for alignmesh (read every session)

This is a SAFETY-RELEVANT system that decides whether aerospace parts are accepted.
The dangerous failure is a FALSE ACCEPT: a wrong result that looks right. Optimize the
whole workflow against that, not against speed or convenience.

## Four invariants — never violated, in any component
1. DOUBLE PRECISION end-to-end in every analytical path (IEEE-754 binary64). Single
   precision is allowed ONLY in display/rendering data that never feeds a result.
2. DETERMINISM: no -march=native, no -ffast-math, FMA contraction off. Use the numerics
   reduction utilities, never raw/parallel reductions, in any path that produces a result.
   Every stochastic step is seeded and the seed is recorded. Every result carries the
   EnvironmentFingerprint of the build that produced it.
3. FAIL-SAFE: any ambiguous, degenerate, under-constrained, low-overlap, non-converged,
   or low-confidence condition resolves toward WARNING / FAIL / INVALID. NEVER toward PASS.
   The absence of a detected failure is NOT evidence of conformance.
4. IMMUTABLE SOURCE: the originally imported geometry is never modified in place. All work
   is on derived copies. Original bytes + hash are preserved.

## Architecture boundary
- This core COMPUTES. The UI (drawgen) only DISPLAYS and ORCHESTRATES. No measurement
  math, no transform math, no pass/fail decision may ever live outside this core.
- reference/vatslicer is READ-ONLY. Never import, link, or #include from it. If a slicer
  approach seems reusable, tell me and justify it; do not wire into it.

## How every component is built (test-first, self-debugging)
For each component:
  (1) Write the ground-truth tests FIRST (synthetic data with a KNOWN correct answer).
  (2) Implement until they pass.
  (3) Run an ADVERSARIAL self-check pass: actively try to break the component with the
      edge/degenerate/failure cases listed in the prompt; fix what you find; add a test
      for each thing you broke.
  (4) Run the ENTIRE accumulated test suite (regression), not just the new tests, then run
      it AGAIN and confirm identical output (repro gate).
  (5) Report: what you implemented, what the adversarial pass tried, what it found, and the
      final suite + repro status. A component is not "done" until 1–5 all pass.

## What requires my sign-off (do not finalize without showing me the interpretation)
Anything that encodes a STANDARD'S MEANING rather than a computation: the uncertainty
budget (GUM), the ISO 14253-1 pass/fail guard-band logic, the datum-fitting criteria
(ISO 5459 / 1101 / ASME Y14.5), and the tolerance-feasibility logic (ISO 15530). For these,
build the component and its tests, then STOP and present the interpretation decision in
plain engineering terms for me to confirm before proceeding.

---

## Build Reference

- C++20 project using CMake (3.20+).
- Build directory: `build/`
- **Deterministic numeric profile** — all builds use locked FP flags:
  - GCC/Clang: `-O2 -fno-fast-math -ffp-contract=off -fno-finite-math-only -msse2 -mfpmath=sse`, warnings-as-errors.
  - MSVC: `/fp:strict /O2 /W4 /WX`.
  - **Never use `-ffast-math`, `-march=native`, or `/fp:fast`.** CMakeLists.txt has a guard that will `FATAL_ERROR` if fast-math is detected.
- **Dependencies** pinned via FetchContent (see `THIRD_PARTY.md`): Eigen 3.4.1, nanoflann 1.9.0, GoogleTest 1.17.0. Do not bump versions without verifying FP-determinism.
- **OpenCASCADE 8.0.0** (optional, `ALIGNMESH_WITH_STEP=ON`): STEP/BREP support. Static linked via vcpkg. LGPL-2.1 + OCCT exception (static OK). Build: `vcpkg install opencascade:x64-windows-static`, configure with `-DALIGNMESH_WITH_STEP=ON -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static`.
- **Warning flags** (`ALIGNMESH_WARNING_FLAGS`) are applied per-target, not globally, so fetched dependencies can compile without `-Werror`.

## Numerics Module (`include/alignmesh/numerics/`)

- **compensated_sum.h** — `neumaier_sum` (compensated) and `pairwise_sum` (fixed-order tree reduction with Neumaier at leaves). Use these instead of `std::accumulate` or manual `+=` loops.
- **seeded_rng.h** — `SeededRng` wrapping `mt19937_64`. No default constructor. No code may use an unseeded RNG.
- **environment_fingerprint.h** — `EnvironmentFingerprint::capture()` records compiler, flags, deps, OS, CPU brand/features. Serialize with `to_string()`.

## Geometry Module (`include/alignmesh/geometry/`)

- **rigid_transform.h** — `RigidTransform` (SE(3)) stored as 4x4 row-major matrix.
  Convention: column-vector (`p' = T*p`), world frame, right-handed.
  Compose (`*`), inverse, apply/apply_cloud, rotation/translation/quaternion accessors.
  SE(3) exp/log via Rodrigues + closed-form V matrix.
  Twist convention: `[omega(0:3), v(3:6)]` (rotation axis*angle first, translation second).
  Near-pi so(3) log uses symmetric-part extraction `(R+R^T)/2` (kNearPi = 1e-4).
  Known precision limit: exp(log(T)) round-trip error ~1e-8 Frobenius norm for rotations within 1e-4 of 180°.
- **point_cloud.h** — `PointCloud` (3xN points + optional normals + optional per-point covariances). Immutable-source-aware. `transformed()` returns new instance (points: R*p+t, normals: R*n, covariances: R*S*R^T).
- **triangle_mesh.h** — `TriangleMesh` (3xV vertices + 3xF triangles + adjacency). Vertex-triangle and triangle-triangle adjacency built at construction. `transformed()` reuses topology.
- **reference_geometry.h** — `ReferenceGeometry` abstraction consumed by deviation engine and RPS projection.
  `closest_point_on_surface(p)` → `SurfacePointResult` (point, normal, signed/unsigned distance, status).
  `normal_at(point)` → `NormalResult` (unit normal, status).
  Implementations: `MeshReference` (BVH + facet normal), `CadReference` (BVH locality + OCCT analytic refine).
  Factory: `make_mesh_reference(mesh)`. CadReference via `cad::make_cad_reference(shape, ...)` (OCCT).

## Alignment Module (`include/alignmesh/alignment/`)

- **kabsch.h** — `align_landmarks()`: weighted Kabsch/Umeyama SVD-based rigid alignment (Arun-Huang-Blostein / Umeyama).
  Scale = 1 fixed. det(R) = +1 enforced (reflection correction). All reductions via `neumaier_sum`.
  Returns `AlignmentResult` with transform, per-landmark residuals, weighted/unweighted RMS, SVD conditioning, weight imbalance.
  Fail-safe: rejects <3 points, zero/negative weights, duplicates, NaN/Inf, collinear configs (sv_mid/sv_max < 1e-8).
  Warns: coplanar (sv_min/sv_max < 1e-4), weight imbalance (max/min > 10).

## Registration Module (`include/alignmesh/registration/`)

- **fine_registration.h** — `fine_register()`: wraps small_gicp (MIT, v1.0.0) with deterministic SerialReduction.
  Methods: POINT_TO_POINT, POINT_TO_PLANE, GICP, VGICP. Default = POINT_TO_PLANE.
  Multi-resolution via voxel_schedule + distance_schedule. Convergence validated explicitly.
  Returns transform, per-level error records, final RMS, overlap ratio, correspondence count.
  small_gicp is header-only (FetchContent_Populate, no add_subdirectory — avoids Eigen target conflicts).
- **robustness.h** — Robustness layer on top of Kabsch:
  `trimmed_icp()`: keep best trim_fraction correspondences, iterate. `m_estimator_align()`: IRLS with Huber/Tukey/Geman-McClure kernels. `gnc_align()`: Graduated Non-Convexity (Yang et al. RA-L 2020) — starts convex (large µ), anneals to non-convex Geman-McClure.
  All expose: outlier%, rejected%, overlap%, per-correspondence weights/residuals.
- **global_registration.h** — `global_register()` → `CoarsePoseGuess` (NOT `RigidTransform`).
  Returns a type-safe coarse guess that MUST be converted via `.as_initial_guess()` to feed fine registration. Confidence score is correspondence quality, NOT accuracy.
  Algorithm: FPFH features (Rusu et al. ICRA 2009) + feature matching (Lowe ratio test) + GNC-TLS robust alignment (TEASER++/KISS-Matcher principles).
  Voxel sizes and feature radii tuned for mm-scale metrology. Flags ambiguity for symmetric objects.
  TEASER++ (v2.0) and KISS-Matcher (v1.0.2) are algorithmic references, not linked — their Boost/PCL deps are too heavy. See THIRD_PARTY.md.
- **observability.h** — SAFETY-CRITICAL: `analyze_observability()` catches "low RMS but meaningless pose."
  Computes point-to-plane Hessian (information matrix), eigendecomposition, per-DOF constraint quality, condition number, and 6×6 pose covariance (Censi 2007).
  FAIL-SAFE RULE: if any DOF is under-constrained (quality < threshold), result is flagged — CANNOT pass regardless of RMS.
  Validates: plane (flags in-plane trans + rot_z), cylinder (flags axis slide/rotation), sphere (flags rotations), L-bracket (all 6 DOFs constrained).
- **hybrid_pipeline.h** — Three workflows + composition algebra:
  `landmark_first()`: Kabsch → Fine ICP. `best_fit_first()`: Fine ICP → optional landmark correction. `global_first()`: Global FPFH → Fine ICP.
  `compose_chain()`: composes stages right-to-left (stage[0] first). `verify_composition()`: validates chain matches final.
  Per-stage records: name, transform, RMS, convergence, warnings. Composition order tested adversarially (reversed order gives wrong answer).

## Analysis Module (`include/alignmesh/analysis/`)

- **deviation.h** — `compute_deviation()`: point-to-mesh deviation analysis.
  Closest-point-on-triangle (Ericson), signed deviation via triangle normals, unsigned absolute deviation.
  Full statistics: min/max/mean/median/RMS/std_dev, 90th/95th/99th percentiles, percent-within-tolerance, outlier%.
  Honesty features: confidence intervals on mean with spatial-autocorrelation effective-sample-size correction; sampling-adequacy check (Nyquist-style: point density must resolve toleranced feature size).
- **precision_tier.h** — `check_precision_gate()`: input-format gate enforced BEFORE alignment.
  5 tiers: COARSE (≥100µm, STL ok), MEDIUM (25-100µm, chord check), FINE (10-25µm, no STL), PRECISE (1-10µm, CAD+double+scanner), ULTRA (<1µm, +temperature).
  `estimate_chord_error()`: dihedral-angle-based tessellation error estimate.
  FAIL-SAFE: unmet conditions → INVALID_CLAIM (not a quiet downgrade). Boundary tolerances explicit (no silent tier-flip).

## Spatial Module (`include/alignmesh/spatial/`)

- **kdtree.h** — `KdTree` wrapping nanoflann (double precision, point NN not ray-cast BVH). `nearest()`, `knn()`, `radius_search()`.
- **voxel_downsample.h** — `voxel_downsample()`: deterministic voxel-grid downsampling (fixed origin at 0, closest-to-centre rule). For coarse stages only — analysis re-refines on full data.
- **normals.h** — `estimate_normals_knn()`/`estimate_normals_radius()`: PCA over k/radius neighbourhood. Orientation: centroid-outward heuristic + BFS propagation + iterative majority-vote repair. Returns curvature and reliability per normal.
- **sampling.h** — `normal_space_sampling()` (Rusinkiewicz & Levoy 2001): uniform angular coverage via hemisphere bins. `stable_sampling()` (Gelfand et al. 2003): greedy selection maximizing min singular value of 6-DOF Jacobian.

## IO Module (`include/alignmesh/io/`)

- **sha256.h** — `sha256_hex()` returns 64-char lowercase hex string. FIPS 180-4 implementation.
- **stl_io.h** — `import_stl()` (binary + ASCII auto-detected, vertex merging by float32 bit-equality), `export_stl_binary()` (with precision-loss warning). STL coordinates are always float32 — flagged in `metadata.coordinate_precision`.
- **ply_io.h** — `import_ply()` (ASCII + binary LE, float/double coords), `export_ply_binary_double()` (full binary64 precision).
- **immutable_source_store.h** — `ImmutableSourceStore::import_file()` reads file, computes SHA-256, stores original bytes (never mutated), detects format/precision/bbox/counts. Max 100M triangles guard.
  Now detects STEP (.step/.stp) files via `looks_like_step()`. STEP imports produce a `CadReference` stored in `cad_refs_` (accessible via `cad_reference(hash)`).
- **step_io.h** — `import_step()`: OCCT STEPControl_Reader (AP203/214/242). Unit normalization to mm (CRITICAL — 25.4x/1000x check). Surface classification, bounding box, PMI/datum reading (XCAF, AP242). Failure modes: explicit errors, never default sphere, never silent tessellation.
  Conditional: compiled only when `ALIGNMESH_WITH_STEP=ON`.
- **mesh_validation.h** — `validate_mesh()` detects: empty mesh, NaN/Inf, degenerate triangles, duplicate vertices, non-manifold edges, boundary edges (holes), inconsistent winding. Reports only — never repairs.

## CAD Module (`include/alignmesh/cad/`)

- **analytic_surface.h** — `AnalyticSurface` base class (`closest_point`, `evaluate`, `normal_at`). `PlaneS`, `CylinderS`, `SphereS`. `compute_true_surface_deviation()`. `read_step_header()` (no OCCT).
- **cad_reference.h** — `CadReference` (OCCT B-rep): `ReferenceGeometry` implementation backed by `TopoDS_Shape`.
  BVH over deterministic fine tessellation for candidate-face locality.
  Analytic refinement via `BRepExtrema_DistShapeShape` (respects trim, global minimum).
  True normals via `GeomLProp_SLProps`. REVERSED faces flip outward normal.
  Signed distance: closed solids use `BRepClass3d_SolidClassifier`.
  `make_cad_reference(shape, display_mesh_out, tess_params)`. Conditional: `ALIGNMESH_WITH_STEP`.

## Testing & CI

- **Unit tests**: GoogleTest + CTest. Run with `ctest --test-dir build --build-config Release`.
  - Numerics: `test_compensated_sum`, `test_seeded_rng`, `test_environment_fingerprint`
  - Geometry: `test_rigid_transform` (19 tests including adversarial), `test_point_cloud`, `test_triangle_mesh`
- **Reproducibility gate**: `repro_manifest.cpp` generates deterministic numeric output (hexfloat). `ci/build_and_test.sh` runs it twice and asserts byte-identical output (`repro_hash`).
- Run: `bash ci/build_and_test.sh [Release|Debug]`

## Project Layout

```
alignmesh/
  ci/
    build_and_test.sh
  cmake/
    config.h.in
  include/alignmesh/
    geometry/
      rigid_transform.h
      point_cloud.h
      triangle_mesh.h
    numerics/
      compensated_sum.h
      seeded_rng.h
      environment_fingerprint.h
  src/
    geometry/
      rigid_transform.cpp
      point_cloud.cpp
      triangle_mesh.cpp
    numerics/
      environment_fingerprint.cpp
  tests/
    test_rigid_transform.cpp
    test_point_cloud.cpp
    test_triangle_mesh.cpp
    test_compensated_sum.cpp
    test_seeded_rng.cpp
    test_environment_fingerprint.cpp
    repro_manifest.cpp
  reference/
    vatslicer/  (submodule, read-only)
```
