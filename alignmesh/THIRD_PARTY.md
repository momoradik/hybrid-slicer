# Third-Party Dependencies

All dependencies are fetched via CMake `FetchContent` with pinned versions.
Do not bump versions without verifying that FP-determinism is preserved.

| Name        | Version | License        | Purpose                              |
|-------------|---------|----------------|--------------------------------------|
| Eigen       | 3.4.1   | MPL-2.0        | Linear algebra, matrix operations    |
| nanoflann   | 1.9.0   | BSD-2-Clause   | KD-tree nearest-neighbour search     |
| GoogleTest  | 1.17.0  | BSD-3-Clause   | Unit testing framework               |
| small_gicp  | 1.0.0   | MIT            | Fine registration (ICP/GICP/VGICP)   |

## Build notes

- **small_gicp**: built WITHOUT `-march=native`, TBB, and OpenMP. The OFFICIAL
  registration path uses single-threaded, fixed-order reduction for deterministic
  results. A parallel preview path (TBB) may be added later, clearly separated
  and never the recorded result.

## Algorithmic references (implemented in-house, not linked)

| Name         | Version | License | Use                                       |
|--------------|---------|---------|-------------------------------------------|
| TEASER++     | 2.0     | MIT     | GNC-TLS decoupled pose estimation concept |
| KISS-Matcher | 1.0.2   | MIT     | Feature matching / k-core pruning concept |

These libraries are NOT linked as dependencies due to heavy transitive deps
(Boost, PCL). The core algorithms (FPFH features, GNC-TLS robust alignment)
are implemented directly in `registration/global_registration.cpp` following
the published algorithmic principles. **FLAG FOR LICENSE REVIEW:** while the
code is original, the algorithm design follows these MIT-licensed works.

## OpenCASCADE (STEP/BREP/NURBS — conditional, behind ALIGNMESH_WITH_STEP)

| Name          | Version | License                                  | Use                                    |
|---------------|---------|------------------------------------------|----------------------------------------|
| OpenCASCADE   | 8.0.0   | LGPL-2.1 + OCCT Public License Exception | STEP AP203/214/242 BREP/NURBS ingestion|

**LICENSE REVIEW COMPLETED.** The OCCT Public License Exception explicitly
permits static linking without copyleft obligations on alignmesh. Obligations:
- Acknowledge OCCT in distribution materials.
- Provide OCCT source or a link to it.
- Modifications to OCCT itself must be shared under LGPL-2.1.

**Linking:** Static (pinned x64-windows-static via vcpkg). OCCT modules linked:
TKernel, TKMath, TKG2d, TKG3d, TKGeomBase, TKGeomAlgo, TKBRep, TKTopAlgo,
TKShHealing, TKSTEP, TKSTEPBase, TKSTEPAttr, TKSTEP209, TKXSBase, TKMesh.
Optional XCAF modules (PMI/datum): TKXDEStep, TKLCAF, TKXCAF, TKV3d, TKService.

**Build flags:** OCCT is compiled under the project's `/fp:strict` (MSVC) global
flags. Same-binary bit-reproducibility is required and tested via repro-gate.
Cross-machine bit-identity is NOT in scope (OCCT internal FP paths vary by CPU).

**Install:** `vcpkg install opencascade:x64-windows-static` then configure with
`-DALIGNMESH_WITH_STEP=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`.
