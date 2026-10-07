cmake_minimum_required(VERSION 3.20)
project(alignmesh
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Deterministic metrology core for aerospace dimensional inspection"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT to match vcpkg x64-windows-static OCCT build.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# DETERMINISTIC NUMERIC BUILD PROFILE
# =============================================================================
#
# This is a safety-relevant aerospace metrology system. Every floating-point
# operation must produce bit-identical results across builds, machines, and
# compiler versions. DO NOT weaken or remove any of these flags.
#
# If you are tempted to add -ffast-math, -march=native, or /fp:fast to
# "improve performance": DON'T. Those flags allow the compiler to reorder,
# fuse, or approximate FP operations, which breaks reproducibility and can
# silently corrupt dimensional-inspection results.
# =============================================================================

# Record the active FP flags so cmake/config.h.in can embed them in the
# EnvironmentFingerprint for traceability.
if(MSVC)
    set(ALIGNMESH_FP_FLAGS "/fp:strict /O2")
    # FP determinism flags — global (must apply to ALL compiled code,
    # including fetched dependencies, to prevent any object file from
    # introducing non-deterministic FP semantics into the link).
    add_compile_options(
        # /fp:strict — IEEE 754 strict mode. Disables FP reordering,
        #   contraction, and reciprocal approximations. Required for
        #   bit-reproducible results across builds.
        /fp:strict

        # /O2 — full optimization within the constraints of /fp:strict.
        /O2
    )

    # Warning flags — applied only to OUR targets (not third-party code)
    # via target_compile_options. Safety-critical code must compile clean.
    # /wd5054: suppress Eigen's deprecated enum bitwise-op warnings (third-party).
    # /wd4127: nanoflann's conditional expression is constant.
    # /wd4324: nanoflann's structure padded due to alignment specifier.
    set(ALIGNMESH_WARNING_FLAGS /W4 /WX /wd5054 /wd4127 /wd4324)
else()
    set(ALIGNMESH_FP_FLAGS "-O2 -fno-fast-math -ffp-contract=off -fno-finite-math-only -msse2 -mfpmath=sse")
    # FP determinism flags — global (must apply to ALL compiled code,
    # including fetched dependencies, to prevent any object file from
    # introducing non-deterministic FP semantics into the link).
    # GCC and Clang share these flags.
    add_compile_options(
        # -O2 — optimize for speed without enabling any unsafe FP transforms.
        #   -O3 is intentionally avoided: it can enable vectorization patterns
        #   that change FP evaluation order on some compilers.
        -O2

        # -fno-fast-math — CRITICAL. Explicitly disables the umbrella flag
        #   that would allow FP reordering, NaN/Inf removal, reciprocal
        #   approximations, and other transforms that destroy reproducibility.
        #   Set explicitly so that no parent build system or toolchain file
        #   can silently inject -ffast-math.
        -fno-fast-math

        # -ffp-contract=off — prohibits fused multiply-add (FMA) contraction.
        #   FMA produces a different (more accurate) result than separate
        #   multiply + add. Allowing it means the same source code produces
        #   different results depending on whether the target has FMA hardware.
        -ffp-contract=off

        # -fno-finite-math-only — do not assume that NaN and Inf cannot occur.
        #   Removing NaN/Inf checks would mask numerical failures that a
        #   metrology system must detect and report.
        -fno-finite-math-only

        # -msse2 -mfpmath=sse — force all FP math through the SSE unit.
        #   The x87 FPU uses 80-bit extended precision internally, which
        #   produces different rounding than the IEEE 64-bit results from SSE.
        #   Pinning to SSE2 guarantees consistent 64-bit evaluation.
        #   NOTE: -march=native is deliberately NOT used — it would enable
        #   AVX/FMA instructions that vary by CPU and break cross-machine
        #   reproducibility.
        -msse2
        -mfpmath=sse
    )

    # Warning flags — applied only to OUR targets (not third-party code)
    # via target_compile_options. Safety-critical code must compile clean.
    set(ALIGNMESH_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

# Guard: ensure no fast-math crept in via toolchain or environment flags.
if(CMAKE_CXX_FLAGS MATCHES "fast-math" OR CMAKE_C_FLAGS MATCHES "fast-math")
    message(FATAL_ERROR
        "fast-math detected in CMAKE_CXX_FLAGS or CMAKE_C_FLAGS. "
        "This MUST NOT be used — it destroys FP reproducibility. "
        "Remove it from your toolchain file or environment."
    )
endif()

# =============================================================================

# --- Dependencies (pinned exact versions) ------------------------------------
# See THIRD_PARTY.md for license details.
# Versions are locked — do not bump without verifying FP-determinism is preserved.
include(FetchContent)

# Eigen 3.4.1 — linear algebra (MPL2)
FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.1
    GIT_SHALLOW    ON
)

# nanoflann 1.9.0 — KD-tree nearest-neighbour search (BSD)
FetchContent_Declare(
    nanoflann
    GIT_REPOSITORY https://github.com/jlblancoc/nanoflann.git
    GIT_TAG        v1.9.0
    GIT_SHALLOW    ON
)

# GoogleTest 1.17.0 — testing framework (BSD-3-Clause)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
    GIT_SHALLOW    ON
)

# Prevent GoogleTest from being installed alongside our project.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# Disable nanoflann's own tests and examples — they are not ours and their
# build flags can conflict with our global FP-determinism flags.
set(NANOFLANN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NANOFLANN_BUILD_TESTS    OFF CACHE BOOL "" FORCE)

# small_gicp 1.0.0 — fine registration (ICP/GICP/VGICP) (MIT)
# Header-only usage: we download the source and include the headers directly.
# This avoids CMake conflicts with our existing Eigen target and ensures
# small_gicp is built WITHOUT -march=native, TBB, or OpenMP.
FetchContent_Declare(
    small_gicp
    GIT_REPOSITORY https://github.com/koide3/small_gicp.git
    GIT_TAG        v1.0.0
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(eigen nanoflann googletest)

# --- OpenCASCADE (STEP/BREP support, optional) --------------------------------
# OCCT 8.0.0 — LGPL-2.1 with OCCT Public License Exception (static linking OK).
# Pinned version; do not change without verifying FP-determinism.
# Install via vcpkg: vcpkg install opencascade:x64-windows-static
# Then pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#           -DVCPKG_TARGET_TRIPLET=x64-windows-static
option(ALIGNMESH_WITH_STEP "Enable STEP/BREP support via OpenCASCADE" OFF)

set(ALIGNMESH_OCCT_VERSION "")
if(ALIGNMESH_WITH_STEP)
    find_package(OpenCASCADE 8.0.0 REQUIRED)
    set(ALIGNMESH_OCCT_VERSION "${OpenCASCADE_VERSION}")
    message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} found: ${OpenCASCADE_INSTALL_PREFIX}")

    # Collect the specific OCCT modules we need for STEP reading + B-rep queries.
    # Minimal set: kernel, math, geometry, topology, STEP reader, BRep extrema,
    # mesh (display tessellation only).
    set(ALIGNMESH_OCCT_LIBS
        TKernel TKMath
        TKG2d TKG3d TKGeomBase TKGeomAlgo
        TKBRep TKTopAlgo TKShHealing
        TKDESTEP TKDE          # OCCT 8.0: TKSTEP* merged into TKDESTEP + TKDE
        TKXSBase
        TKMesh          # display tessellation (never feeds conformance path)
    )

    # Optional: XCAF for PMI/datum reading (AP242).
    # OCCT 8.0: TKXDEStep merged into TKDESTEP; check for TKLCAF as proxy.
    find_library(OCCT_TKLCAF TKLCAF PATHS "${OpenCASCADE_LIBRARY_DIR}" NO_DEFAULT_PATH)
    if(OCCT_TKLCAF)
        list(APPEND ALIGNMESH_OCCT_LIBS TKLCAF TKXCAF TKV3d TKService)
        set(ALIGNMESH_HAS_XCAF TRUE)
        message(STATUS "OCCT XCAF (PMI/datum) support: available")
    else()
        set(ALIGNMESH_HAS_XCAF FALSE)
        message(STATUS "OCCT XCAF (PMI/datum) support: not available (non-blocking)")
    endif()
endif()

# Populate small_gicp (download only, no add_subdirectory).
FetchContent_GetProperties(small_gicp)
if(NOT small_gicp_POPULATED)
    FetchContent_Populate(small_gicp)
endif()

# --- Generated config header -------------------------------------------------
# Embeds compiler, flags, and version info for EnvironmentFingerprint.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${CMAKE_BINARY_DIR}/generated/alignmesh/config.h"
)

# --- Source library -----------------------------------------------------------
add_library(alignmesh_core STATIC)

target_include_directories(alignmesh_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
)

file(GLOB_RECURSE ALIGNMESH_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
# Exclude http_server.cpp — it contains main() and is built separately as
# alignmesh_server. Including it in the static library causes every test
# executable to inherit the server's main() instead of gtest_main's.
list(FILTER ALIGNMESH_SOURCES EXCLUDE REGEX "http_server\\.cpp$")
target_sources(alignmesh_core PRIVATE ${ALIGNMESH_SOURCES})

target_compile_options(alignmesh_core PRIVATE ${ALIGNMESH_WARNING_FLAGS})

target_include_directories(alignmesh_core
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${small_gicp_SOURCE_DIR}/include>
)

target_link_libraries(alignmesh_core
    PUBLIC
        Eigen3::Eigen
        nanoflann::nanoflann
)

if(ALIGNMESH_WITH_STEP)
    target_include_directories(alignmesh_core SYSTEM PUBLIC ${OpenCASCADE_INCLUDE_DIR})
    target_link_directories(alignmesh_core PUBLIC ${OpenCASCADE_LIBRARY_DIR})
    target_link_libraries(alignmesh_core PUBLIC ${ALIGNMESH_OCCT_LIBS})
    # Static OCCT→freetype→zlib/bzip2/png/brotli: explicit transitive deps.
    set(_vcpkg_lib "${CMAKE_PREFIX_PATH}/lib")
    target_link_libraries(alignmesh_core PUBLIC
        "${_vcpkg_lib}/freetype.lib"
        "${_vcpkg_lib}/zs.lib"
        "${_vcpkg_lib}/bz2.lib"
        "${_vcpkg_lib}/libpng16.lib"
        "${_vcpkg_lib}/brotlidec.lib"
        "${_vcpkg_lib}/brotlicommon.lib"
    )
    target_compile_definitions(alignmesh_core PUBLIC ALIGNMESH_WITH_STEP=1)
    # OCCT CMake config sets _CRT_SECURE_NO_WARNINGS at directory level,
    # which conflicts with sources that also #define it. Suppress C4005.
    if(MSVC)
        target_compile_options(alignmesh_core PRIVATE /wd4005)
    endif()
    if(ALIGNMESH_HAS_XCAF)
        target_compile_definitions(alignmesh_core PRIVATE ALIGNMESH_HAS_XCAF=1)
    endif()
endif()

# --- HTTP service executable --------------------------------------------------
add_executable(alignmesh_server src/service/http_server.cpp)
target_link_libraries(alignmesh_server PRIVATE alignmesh_core)
target_compile_options(alignmesh_server PRIVATE ${ALIGNMESH_WARNING_FLAGS})
if(MSVC)
    # Debug info + map file for crash diagnosis.
    target_compile_options(alignmesh_server PRIVATE /Zi)
    target_compile_options(alignmesh_core PRIVATE /Zi)
    target_link_options(alignmesh_server PRIVATE /DEBUG /MAP)
endif()
if(WIN32)
    target_link_libraries(alignmesh_server PRIVATE ws2_32 dbghelp)
endif()

# --- Tests --------------------------------------------------------------------
option(ALIGNMESH_BUILD_TESTS "Build tests" ON)
if(ALIGNMESH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
