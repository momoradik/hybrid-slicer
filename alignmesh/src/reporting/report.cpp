#define _CRT_SECURE_NO_WARNINGS
#include "alignmesh/reporting/report.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace alignmesh::reporting {

// ============================================================================
// Timestamp helper
// ============================================================================

static std::string now_iso8601() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// ============================================================================
// Audit trail
// ============================================================================

static std::string compute_entry_hash(const AuditEntry& e) {
    std::string content = e.timestamp + "|" + e.action + "|" +
                          e.user + "|" + e.detail + "|" + e.previous_hash;
    return io::sha256_hex(
        reinterpret_cast<const uint8_t*>(content.data()), content.size());
}

std::string AuditTrail::append(const std::string& action,
                                const std::string& user,
                                const std::string& detail) {
    AuditEntry entry;
    entry.timestamp = now_iso8601();
    entry.action = action;
    entry.user = user;
    entry.detail = detail;
    entry.previous_hash = entries.empty() ? "GENESIS" : entries.back().entry_hash;
    entry.entry_hash = compute_entry_hash(entry);
    entries.push_back(entry);
    return entry.entry_hash;
}

bool AuditTrail::verify_chain() const {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        // Verify previous_hash linkage.
        if (i == 0) {
            if (entries[i].previous_hash != "GENESIS") return false;
        } else {
            if (entries[i].previous_hash != entries[i - 1].entry_hash) return false;
        }
        // Verify entry hash.
        if (entries[i].entry_hash != compute_entry_hash(entries[i])) return false;
    }
    return true;
}

std::string AuditTrail::head_hash() const {
    return entries.empty() ? "GENESIS" : entries.back().entry_hash;
}

// ============================================================================
// Report formatting
// ============================================================================

static std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string format_report(const service::ResultPackage& r, ReportFormat fmt) {
    std::ostringstream o;

    auto verdict_str = [](alignmesh::analysis::Verdict v) -> const char* {
        switch (v) {
        case alignmesh::analysis::Verdict::PASS:    return "PASS";
        case alignmesh::analysis::Verdict::WARNING: return "WARNING";
        case alignmesh::analysis::Verdict::FAIL:    return "FAIL";
        case alignmesh::analysis::Verdict::INVALID: return "INVALID";
        }
        return "UNKNOWN";
    };

    switch (fmt) {
    case ReportFormat::JSON:
        o << "{\n";
        o << "  \"core_version\": \"" << escape_json(r.core_version) << "\",\n";
        o << "  \"timestamp\": \"" << escape_json(r.timestamp) << "\",\n";
        o << "  \"reference_hash\": \"" << r.reference_hash << "\",\n";
        o << "  \"measured_hash\": \"" << r.measured_hash << "\",\n";
        o << "  \"tolerance_mm\": " << r.tolerance << ",\n";
        o << "  \"precision_tier\": \"" << escape_json(r.precision_tier) << "\",\n";
        o << "  \"alignment_mode\": \"" << escape_json(r.alignment_mode) << "\",\n";
        o << "  \"alignment_rms\": " << r.alignment_rms << ",\n";
        o << "  \"verdict\": \"" << verdict_str(r.verdict) << "\",\n";
        o << "  \"granular_verdict\": \"" << verdict_str(r.granular_verdict) << "\",\n";
        o << "  \"acceptance_zone\": [" << r.acceptance_lower << ", " << r.acceptance_upper << "],\n";
        o << "  \"deviation_stats\": {\n";
        o << "    \"mean\": " << r.unsigned_stats.mean << ",\n";
        o << "    \"rms\": " << r.unsigned_stats.rms << ",\n";
        o << "    \"max\": " << r.unsigned_stats.max << ",\n";
        o << "    \"std_dev\": " << r.unsigned_stats.std_dev << ",\n";
        o << "    \"p95\": " << r.unsigned_stats.p95 << ",\n";
        o << "    \"p99\": " << r.unsigned_stats.p99 << ",\n";
        o << "    \"n_points\": " << r.unsigned_stats.n_points << ",\n";
        o << "    \"percent_within_tolerance\": " << r.unsigned_stats.percent_within_tolerance << "\n";
        o << "  },\n";
        o << "  \"expanded_uncertainty\": " << r.expanded_uncertainty << ",\n";
        o << "  \"coverage_factor\": " << r.coverage_factor << ",\n";
        o << "  \"fully_constrained\": " << (r.fully_constrained ? "true" : "false") << ",\n";
        o << "  \"heatmap_label\": \"" << escape_json(r.heatmap_label) << "\",\n";
        o << "  \"fingerprint\": {\n";
        o << "    \"compiler\": \"" << r.fingerprint.compiler_id << " " << r.fingerprint.compiler_version << "\",\n";
        o << "    \"cpu\": \"" << escape_json(r.fingerprint.cpu_brand) << "\"\n";
        o << "  }\n";
        o << "}\n";
        break;

    case ReportFormat::CSV:
        o << "field,value\n";
        o << "core_version," << r.core_version << "\n";
        o << "tolerance_mm," << r.tolerance << "\n";
        o << "verdict," << verdict_str(r.verdict) << "\n";
        o << "alignment_rms," << r.alignment_rms << "\n";
        o << "deviation_mean," << r.unsigned_stats.mean << "\n";
        o << "deviation_rms," << r.unsigned_stats.rms << "\n";
        o << "deviation_max," << r.unsigned_stats.max << "\n";
        o << "n_points," << r.unsigned_stats.n_points << "\n";
        o << "percent_within_tolerance," << r.unsigned_stats.percent_within_tolerance << "\n";
        o << "heatmap_label," << r.heatmap_label << "\n";
        break;

    case ReportFormat::QIF:
        // Simplified QIF (ISO 23952) — full QIF requires XSD schema.
        o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        o << "<QIFDocument xmlns=\"http://qifstandards.org/xsd/qif3\">\n";
        o << "  <MeasurementResults>\n";
        o << "    <Tolerance>" << r.tolerance << "</Tolerance>\n";
        o << "    <Verdict>" << verdict_str(r.verdict) << "</Verdict>\n";
        o << "    <DeviationMean>" << r.unsigned_stats.mean << "</DeviationMean>\n";
        o << "    <DeviationRMS>" << r.unsigned_stats.rms << "</DeviationRMS>\n";
        o << "    <DeviationMax>" << r.unsigned_stats.max << "</DeviationMax>\n";
        o << "  </MeasurementResults>\n";
        o << "</QIFDocument>\n";
        break;

    case ReportFormat::TEXT:
        o << "=== INSPECTION REPORT ===\n";
        o << "Core: " << r.core_version << "\n";
        o << "Time: " << r.timestamp << "\n";
        o << "Ref hash: " << r.reference_hash << "\n";
        o << "Meas hash: " << r.measured_hash << "\n";
        o << "Tolerance: " << r.tolerance << " mm\n";
        o << "Tier: " << r.precision_tier << "\n";
        o << "Alignment: " << r.alignment_mode << " (RMS=" << r.alignment_rms << ")\n";
        o << "--- DEVIATION ---\n";
        o << "Mean: " << r.unsigned_stats.mean << " mm\n";
        o << "RMS:  " << r.unsigned_stats.rms << " mm\n";
        o << "Max:  " << r.unsigned_stats.max << " mm\n";
        o << "Within tolerance: " << r.unsigned_stats.percent_within_tolerance << "%\n";
        o << "--- DECISION ---\n";
        o << "Verdict: " << verdict_str(r.verdict) << "\n";
        o << "Heatmap: " << r.heatmap_label << "\n";
        break;
    }

    return o.str();
}

// ============================================================================
// Report generation
// ============================================================================

InspectionReport generate_report(
        const service::ResultPackage& result,
        ReportFormat format,
        const std::string& report_id) {
    InspectionReport report;
    report.result = result;
    report.format = format;
    report.report_id = report_id.empty() ?
        ("RPT-" + result.timestamp) : report_id;
    report.generated_timestamp = now_iso8601();
    report.content = format_report(result, format);

    report.audit.append("REPORT_GENERATED", "system",
        "format=" + std::string(format == ReportFormat::JSON ? "JSON" :
                                format == ReportFormat::CSV ? "CSV" :
                                format == ReportFormat::QIF ? "QIF" : "TEXT"));

    return report;
}

// ============================================================================
// Signing
// ============================================================================

void sign_report(InspectionReport& report, const std::string& user) {
    // Compute signature hash over the report content + audit chain.
    std::string to_sign = report.content + "|" + report.audit.head_hash() +
                          "|" + user;
    report.signature_hash = io::sha256_hex(
        reinterpret_cast<const uint8_t*>(to_sign.data()), to_sign.size());
    report.signed_by = user;
    report.signed_timestamp = now_iso8601();
    report.is_signed = true;
    report.is_locked = true;

    report.audit.append("SIGNED_AND_LOCKED", user,
        "signature=" + report.signature_hash);
}

bool verify_report(const InspectionReport& report) {
    if (!report.is_signed) return false;

    // Verify audit chain.
    if (!report.audit.verify_chain()) return false;

    // Find the signing entry and recompute the signature.
    // The signature was computed from: content + audit_head_before_signing + user.
    // After signing, one more entry was added to the audit trail.
    // So we need to use the hash BEFORE the signing entry.
    if (report.audit.entries.size() < 2) return false;

    // The signing entry is the last one; the hash before it is the previous entry's hash.
    auto& sign_entry = report.audit.entries.back();
    std::string to_sign = report.content + "|" + sign_entry.previous_hash +
                          "|" + report.signed_by;
    std::string expected = io::sha256_hex(
        reinterpret_cast<const uint8_t*>(to_sign.data()), to_sign.size());

    return report.signature_hash == expected;
}

} // namespace alignmesh::reporting
