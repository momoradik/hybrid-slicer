#include <gtest/gtest.h>
#include "alignmesh/reporting/report.h"

using namespace alignmesh::reporting;
using namespace alignmesh::service;

namespace {

ResultPackage make_test_result() {
    ResultPackage pkg;
    pkg.valid = true;
    pkg.core_version = "alignmesh 0.1.0";
    pkg.reference_hash = "aabbccdd00112233";
    pkg.measured_hash = "44556677889900aa";
    pkg.alignment_mode = "best-fit";
    pkg.alignment_rms = 0.005;
    pkg.verdict = alignmesh::analysis::Verdict::PASS;
    pkg.verdict_label = "PASS";
    pkg.tolerance = 0.100;
    pkg.heatmap_label = "CORROBORATING — not authoritative";
    pkg.unsigned_stats.mean = 0.010;
    pkg.unsigned_stats.rms = 0.012;
    pkg.unsigned_stats.max = 0.025;
    pkg.timestamp = "2026-06-04T12:00:00Z";
    return pkg;
}

} // namespace

// ============================================================================
// Audit trail
// ============================================================================

TEST(AuditTrail, AppendAndVerify) {
    AuditTrail trail;
    trail.append("INSPECTION_RUN", "system", "tolerance=0.1mm");
    trail.append("REPORT_GENERATED", "system", "format=JSON");
    trail.append("APPROVED", "Sina", "reviewed and approved");

    EXPECT_EQ(trail.entries.size(), 3u);
    EXPECT_TRUE(trail.verify_chain());
}

TEST(AuditTrail, TamperDetected) {
    AuditTrail trail;
    trail.append("RUN", "sys", "detail1");
    trail.append("APPROVE", "user", "detail2");

    ASSERT_TRUE(trail.verify_chain());

    // Tamper with the first entry.
    trail.entries[0].detail = "TAMPERED";

    EXPECT_FALSE(trail.verify_chain())
        << "Tampered audit trail must be detected";
}

TEST(AuditTrail, OutOfOrderInjection) {
    AuditTrail trail;
    trail.append("STEP1", "sys", "first");
    trail.append("STEP2", "sys", "second");

    // Try to inject an entry between existing entries by modifying the chain.
    AuditEntry injected;
    injected.action = "INJECTED";
    injected.previous_hash = trail.entries[0].entry_hash;
    injected.entry_hash = "fake_hash";
    trail.entries.insert(trail.entries.begin() + 1, injected);

    EXPECT_FALSE(trail.verify_chain())
        << "Injected out-of-order entry must break the chain";
}

// ============================================================================
// Report generation
// ============================================================================

TEST(Report, GeneratedFromResult) {
    auto pkg = make_test_result();
    auto report = generate_report(pkg, ReportFormat::JSON, "RPT-001");

    EXPECT_FALSE(report.content.empty());
    EXPECT_EQ(report.report_id, "RPT-001");
    EXPECT_FALSE(report.generated_timestamp.empty());

    // Report must contain the core's values.
    EXPECT_NE(report.content.find("tolerance"), std::string::npos)
        << "Report must contain tolerance field";
    EXPECT_NE(report.content.find("PASS"), std::string::npos)
        << "Report must contain the verdict";
    EXPECT_NE(report.content.find("CORROBORATING"), std::string::npos)
        << "Report must label heatmap as corroborating";
}

TEST(Report, Reproducibility) {
    auto pkg = make_test_result();
    auto r1 = format_report(pkg, ReportFormat::JSON);
    auto r2 = format_report(pkg, ReportFormat::JSON);
    EXPECT_EQ(r1, r2) << "Same input must produce identical report content";
}

TEST(Report, JSONFormat) {
    auto pkg = make_test_result();
    auto content = format_report(pkg, ReportFormat::JSON);
    EXPECT_NE(content.find("{"), std::string::npos);
    EXPECT_NE(content.find("verdict"), std::string::npos);
}

TEST(Report, CSVFormat) {
    auto pkg = make_test_result();
    auto content = format_report(pkg, ReportFormat::CSV);
    EXPECT_NE(content.find(","), std::string::npos);
}

TEST(Report, TextFormat) {
    auto pkg = make_test_result();
    auto content = format_report(pkg, ReportFormat::TEXT);
    EXPECT_NE(content.find("PASS"), std::string::npos);
}

// ============================================================================
// Signing and locking
// ============================================================================

TEST(Report, SignAndLock) {
    auto pkg = make_test_result();
    auto report = generate_report(pkg, ReportFormat::JSON);

    sign_report(report, "Sina");

    EXPECT_TRUE(report.is_signed);
    EXPECT_TRUE(report.is_locked);
    EXPECT_FALSE(report.signature_hash.empty());
    EXPECT_EQ(report.signed_by, "Sina");
}

TEST(Report, VerifySignature) {
    auto pkg = make_test_result();
    auto report = generate_report(pkg, ReportFormat::JSON);
    sign_report(report, "Sina");

    EXPECT_TRUE(verify_report(report));
}

TEST(Report, TamperAfterSigningDetected) {
    auto pkg = make_test_result();
    auto report = generate_report(pkg, ReportFormat::JSON);
    sign_report(report, "Sina");

    // Tamper with the report content.
    report.content += " EXTRA";

    EXPECT_FALSE(verify_report(report))
        << "Tampered signed report must fail verification";
}

TEST(Report, LockedReportAuditRecorded) {
    auto pkg = make_test_result();
    auto report = generate_report(pkg, ReportFormat::JSON);
    sign_report(report, "Sina");

    // The audit trail should record the signing.
    bool found_sign = false;
    for (auto& e : report.audit.entries)
        if (e.action.find("SIGN") != std::string::npos) found_sign = true;
    EXPECT_TRUE(found_sign);
}

// ============================================================================
// Adversarial
// ============================================================================

TEST(ReportAdversarial, EmptyResult) {
    ResultPackage empty;
    auto report = generate_report(empty, ReportFormat::JSON);
    EXPECT_FALSE(report.content.empty());
    // Should still produce a report (with INVALID verdict).
}
