#include "alignmesh/numerics/environment_fingerprint.h"
#include "alignmesh/config.h"

#include <array>
#include <cstring>
#include <sstream>

// Platform-specific CPUID headers.
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    #define ALIGNMESH_X86 1
    #ifdef _MSC_VER
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#else
    #define ALIGNMESH_X86 0
#endif

namespace alignmesh::numerics {

// ---- x86 CPUID helpers (compile to nothing on non-x86) --------------------

#if ALIGNMESH_X86

namespace {

struct CpuidRegs {
    std::uint32_t eax, ebx, ecx, edx;
};

CpuidRegs cpuid(std::uint32_t leaf, std::uint32_t subleaf = 0) {
    CpuidRegs r{};
#ifdef _MSC_VER
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    r.eax = static_cast<std::uint32_t>(regs[0]);
    r.ebx = static_cast<std::uint32_t>(regs[1]);
    r.ecx = static_cast<std::uint32_t>(regs[2]);
    r.edx = static_cast<std::uint32_t>(regs[3]);
#else
    __cpuid_count(leaf, subleaf, r.eax, r.ebx, r.ecx, r.edx);
#endif
    return r;
}

std::string detect_cpu_brand() {
    auto ext = cpuid(0x80000000);
    if (ext.eax < 0x80000004) return "unknown";

    std::array<char, 49> brand{};  // 48 chars + null
    for (std::uint32_t i = 0; i < 3; ++i) {
        auto r = cpuid(0x80000002 + i);
        std::memcpy(&brand[i * 16 +  0], &r.eax, 4);
        std::memcpy(&brand[i * 16 +  4], &r.ebx, 4);
        std::memcpy(&brand[i * 16 +  8], &r.ecx, 4);
        std::memcpy(&brand[i * 16 + 12], &r.edx, 4);
    }

    std::string result(brand.data());
    while (!result.empty() && (result.back() == ' ' || result.back() == '\0')) {
        result.pop_back();
    }
    return result;
}

std::vector<std::string> detect_cpu_features() {
    std::vector<std::string> features;

    // Leaf 1: basic feature flags.
    auto leaf1 = cpuid(1);

    // EDX flags
    if (leaf1.edx & (1u << 25)) features.emplace_back("SSE");
    if (leaf1.edx & (1u << 26)) features.emplace_back("SSE2");

    // ECX flags
    if (leaf1.ecx & (1u <<  0)) features.emplace_back("SSE3");
    if (leaf1.ecx & (1u <<  9)) features.emplace_back("SSSE3");
    if (leaf1.ecx & (1u << 19)) features.emplace_back("SSE4.1");
    if (leaf1.ecx & (1u << 20)) features.emplace_back("SSE4.2");
    if (leaf1.ecx & (1u << 12)) features.emplace_back("FMA");
    if (leaf1.ecx & (1u << 28)) features.emplace_back("AVX");

    // Leaf 7, subleaf 0: extended feature flags.
    auto base = cpuid(0);
    if (base.eax >= 7) {
        auto leaf7 = cpuid(7, 0);
        if (leaf7.ebx & (1u <<  5)) features.emplace_back("AVX2");
        if (leaf7.ebx & (1u << 16)) features.emplace_back("AVX-512F");
    }

    return features;
}

} // anonymous namespace

#endif // ALIGNMESH_X86

// ---- EnvironmentFingerprint implementation --------------------------------

EnvironmentFingerprint EnvironmentFingerprint::capture() {
    EnvironmentFingerprint fp;

    // Compile-time info injected by cmake/config.h.in.
    fp.project_version  = ALIGNMESH_VERSION;
    fp.compiler_id      = ALIGNMESH_COMPILER_ID;
    fp.compiler_version = ALIGNMESH_COMPILER_VERSION;
    fp.cpp_standard     = ALIGNMESH_CXX_STANDARD;
    fp.fp_flags         = ALIGNMESH_FP_FLAGS;
    fp.system_name      = ALIGNMESH_SYSTEM_NAME;
    fp.system_processor = ALIGNMESH_SYSTEM_PROCESSOR;

    fp.eigen_version    = ALIGNMESH_EIGEN_VERSION;
    fp.nanoflann_version = ALIGNMESH_NANOFLANN_VERSION;
    fp.gtest_version    = ALIGNMESH_GTEST_VERSION;
    fp.occt_version     = ALIGNMESH_OCCT_VERSION;

    // Runtime CPU detection.
#if ALIGNMESH_X86
    fp.cpu_brand    = detect_cpu_brand();
    fp.cpu_features = detect_cpu_features();
#else
    fp.cpu_brand    = "non-x86 (unknown)";
#endif

    return fp;
}

std::string EnvironmentFingerprint::to_string() const {
    std::ostringstream os;

    os << "=== alignmesh environment fingerprint ===\n";
    os << "project_version : " << project_version  << '\n';
    os << "compiler        : " << compiler_id << ' ' << compiler_version << '\n';
    os << "c++ standard    : " << cpp_standard      << '\n';
    os << "fp flags        : " << fp_flags           << '\n';
    os << "os              : " << system_name        << '\n';
    os << "arch            : " << system_processor   << '\n';
    os << "cpu             : " << cpu_brand           << '\n';

    os << "cpu features    :";
    for (const auto& f : cpu_features) {
        os << ' ' << f;
    }
    os << '\n';

    os << "eigen           : " << eigen_version      << '\n';
    os << "nanoflann       : " << nanoflann_version   << '\n';
    os << "gtest           : " << gtest_version       << '\n';
    if (!occt_version.empty())
        os << "occt            : " << occt_version     << '\n';

    return os.str();
}

} // namespace alignmesh::numerics
