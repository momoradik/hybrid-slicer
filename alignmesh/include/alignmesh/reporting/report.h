#pragma once

#include "alignmesh/service/result_package.h"
#include "alignmesh/io/sha256.h"

#include <cstdint>
#include <string>
#include <vector>

namespace alignmesh::reporting {

// ============================================================================
// Reporting, QIF, audit trail, signing
// ============================================================================
//
// Report content (spec §16):
//   - Identifiers, file hashes, software versions, numerical fingerprint, seeds
//   - Trusted timestamp, scanner/temperature data
//   - Tolerance, tier, feasibility
//   - All transforms + composition chain
//   - Covariance, observability
//   - Deviation stats with CIs + correlation caveat
//   - Percent within guard-banded zone
//   - PASS/WARNING/FAIL/INVALID decision with the rule applied
//
// Audit trail:
//   - Hash-chained, append-only
//   - Cryptographic signing + lock after approval
//   - ALCOA+ (Attributable, Legible, Contemporaneous, Original, Accurate,
//     Complete, Consistent, Enduring, Available)
//
// Reports clearly separate:
//   - Official deterministic results
//   - AI/heuristic suggestions
//   - User overrides
//   - Blocked claims
// ============================================================================

/// A single audit trail entry.
struct AuditEntry {
    std::string timestamp;
    std::string action;        // e.g., "INSPECTION_RUN", "REPORT_GENERATED", "APPROVED"
    std::string user;
    std::string detail;
    std::string previous_hash; // hash of the previous entry (chain)
    std::string entry_hash;    // SHA-256 of this entry's content + previous_hash
};

/// The audit trail (hash-chained, append-only).
struct AuditTrail {
    std::vector<AuditEntry> entries;

    /// Append a new entry. Returns the entry's hash.
    std::string append(const std::string& action,
                       const std::string& user,
                       const std::string& detail);

    /// Verify the hash chain integrity. Returns true if intact.
    bool verify_chain() const;

    /// Get the latest hash (head of chain).
    std::string head_hash() const;
};

/// Report format.
enum class ReportFormat {
    JSON,   // machine-readable
    CSV,    // tabular data export
    QIF,    // ISO 23952 (simplified — full QIF requires XSD schema)
    TEXT,   // human-readable plain text
};

/// A signed, lockable inspection report.
struct InspectionReport {
    /// The result package from the core.
    service::ResultPackage result;

    /// Report metadata.
    std::string report_id;
    std::string generated_timestamp;

    /// Audit trail.
    AuditTrail audit;

    /// Signing.
    bool is_signed = false;
    bool is_locked = false;
    std::string signature_hash;  // SHA-256 of the full report content
    std::string signed_by;
    std::string signed_timestamp;

    /// The report content in the requested format.
    std::string content;
    ReportFormat format = ReportFormat::JSON;
};

/// Generate a report from a result package.
InspectionReport generate_report(
    const service::ResultPackage& result,
    ReportFormat format = ReportFormat::JSON,
    const std::string& report_id = "");

/// Generate report content in the specified format.
std::string format_report(
    const service::ResultPackage& result,
    ReportFormat format);

/// Sign and lock a report. After locking, the report cannot be altered.
void sign_report(InspectionReport& report,
                 const std::string& user);

/// Verify a signed report's integrity.
bool verify_report(const InspectionReport& report);

} // namespace alignmesh::reporting
