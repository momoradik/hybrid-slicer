#pragma once

#include <string>
#include <vector>

namespace alignmesh::numerics {

// ============================================================================
// EnvironmentFingerprint
// ============================================================================
//
// Captures everything that could affect numerical results: compiler identity
// and version, FP flags, dependency versions, OS, and CPU features. Two runs
// that produce different fingerprints are NOT guaranteed to yield bit-identical
// results.
//
// Serialize with to_string() and store alongside computation outputs so that
// any discrepancy can be traced to an environment change.
// ============================================================================

struct EnvironmentFingerprint {
    // Compile-time info (from cmake/config.h.in).
    std::string project_version;
    std::string compiler_id;
    std::string compiler_version;
    std::string cpp_standard;
    std::string fp_flags;
    std::string system_name;
    std::string system_processor;

    // Pinned dependency versions.
    std::string eigen_version;
    std::string nanoflann_version;
    std::string gtest_version;
    std::string occt_version;  // Empty if STEP support not compiled in.

    // Runtime CPU identification.
    std::string cpu_brand;
    std::vector<std::string> cpu_features;  // e.g. {"SSE2", "SSE4.1", "AVX"}

    /// Capture the current environment (compile-time + runtime).
    static EnvironmentFingerprint capture();

    /// Serialize to a human-readable multi-line string suitable for logs,
    /// report headers, or file storage.
    [[nodiscard]] std::string to_string() const;
};

} // namespace alignmesh::numerics
