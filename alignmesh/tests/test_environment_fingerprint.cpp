#include <gtest/gtest.h>
#include "alignmesh/numerics/environment_fingerprint.h"

using namespace alignmesh::numerics;

TEST(EnvironmentFingerprint, CapturePopulatesFields) {
    auto fp = EnvironmentFingerprint::capture();

    EXPECT_FALSE(fp.project_version.empty());
    EXPECT_FALSE(fp.compiler_id.empty());
    EXPECT_FALSE(fp.compiler_version.empty());
    EXPECT_FALSE(fp.cpp_standard.empty());
    EXPECT_FALSE(fp.system_name.empty());
    EXPECT_FALSE(fp.system_processor.empty());
    EXPECT_FALSE(fp.eigen_version.empty());
    EXPECT_FALSE(fp.nanoflann_version.empty());
    EXPECT_FALSE(fp.cpu_brand.empty());
}

TEST(EnvironmentFingerprint, ToStringContainsKey) {
    auto fp = EnvironmentFingerprint::capture();
    auto s = fp.to_string();

    EXPECT_NE(s.find("compiler"), std::string::npos);
    EXPECT_NE(s.find("fp flags"), std::string::npos);
    EXPECT_NE(s.find("cpu"), std::string::npos);
    EXPECT_NE(s.find("eigen"), std::string::npos);
}

TEST(EnvironmentFingerprint, CaptureDeterministic) {
    auto a = EnvironmentFingerprint::capture();
    auto b = EnvironmentFingerprint::capture();

    EXPECT_EQ(a.to_string(), b.to_string());
}
