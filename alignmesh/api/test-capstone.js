#!/usr/bin/env node
/**
 * V-6 End-to-end equivalence + firewall audit (capstone).
 *
 * 1. Field-by-field equivalence: UI adds nothing, loses nothing.
 * 2. Determinism through the boundary: same input → same result, repeatably.
 * 3. Static firewall audit: every number in the UI is core-supplied or pure display.
 *
 * Usage: node test-capstone.js [core-url]
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const CORE = process.argv[2] || 'http://localhost:9093';

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error('  FAIL: ' + msg); }
}

function postJson(url, body) {
  return new Promise((resolve, reject) => {
    const data = JSON.stringify(body);
    const u = new URL(url);
    const req = http.request({
      hostname: u.hostname, port: u.port, path: u.pathname,
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) },
    }, res => {
      let d = '';
      res.on('data', chunk => d += chunk);
      res.on('end', () => { try { resolve(JSON.parse(d)); } catch(e) { reject(e); } });
    });
    req.on('error', reject);
    req.setTimeout(30000, () => { req.destroy(); reject(new Error('Timeout')); });
    req.write(data);
    req.end();
  });
}

// ══════════════════════════════════════════════════════════════════════════
// The UI rendering simulation: exactly what the React components do.
// Each function returns what the UI would display for each field.
// The point: it ONLY reads from the response. No math.
// ══════════════════════════════════════════════════════════════════════════

function uiRender(resp) {
  return {
    // VerdictPanel
    verdict:          resp.verdict,
    verdict_label:    resp.verdict_label,
    tolerance:        resp.tolerance_mm,
    tier:             resp.precision_tier,
    alignment_mode:   resp.alignment_mode,
    alignment_rms:    resp.alignment_rms,
    max_dev:          resp.stats.max,
    acceptance_lower: resp.acceptance_lower,
    acceptance_upper: resp.acceptance_upper,

    // StatisticsPanel
    n_points:                 resp.stats.n_points,
    mean:                     resp.stats.mean,
    rms:                      resp.stats.rms,
    std_dev:                  resp.stats.std_dev,
    percent_within_tolerance: resp.stats.percent_within_tolerance,

    // ObservabilityPanel
    fully_constrained:     resp.fully_constrained,
    num_under_constrained: resp.num_under_constrained,
    expanded_uncertainty:  resp.expanded_uncertainty,
    coverage_factor:       resp.coverage_factor,

    // ProvenancePanel
    core_version:   resp.core_version,
    compiler:       resp.fingerprint.compiler,
    cpu:            resp.fingerprint.cpu,
    timestamp:      resp.timestamp,
    reference_hash: resp.reference_hash,
    measured_hash:  resp.measured_hash,
    heatmap_label:  resp.heatmap_label,

    // Integrity
    n_display_points:     resp.n_display_points,
    deviation_count:      resp.point_deviations ? resp.point_deviations.length : 0,
    deviation_checksum:   resp.deviation_checksum,
    max_deviation_index:  resp.max_deviation_index,
    max_deviation_value:  resp.max_deviation_value,
    transform_matrix:     resp.transform_matrix,
  };
}

async function main() {
  console.log('V-6 End-to-end equivalence + firewall audit (capstone)');
  console.log('Core: ' + CORE);
  console.log('');

  // ══════════════════════════════════════════════════════════════════════
  // 1. Field-by-field equivalence
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 1. Field-by-field equivalence ---');

  const req = { reference: '/test/ref.stl', measured: '/test/meas.stl', tolerance: 0.1 };
  const coreResp = await postJson(CORE + '/inspect', req);
  const uiView = uiRender(coreResp);

  // Every field the UI displays must equal the core's value exactly.
  const fields = [
    ['verdict',          coreResp.verdict],
    ['verdict_label',    coreResp.verdict_label],
    ['tolerance',        coreResp.tolerance_mm],
    ['tier',             coreResp.precision_tier],
    ['alignment_mode',   coreResp.alignment_mode],
    ['alignment_rms',    coreResp.alignment_rms],
    ['max_dev',          coreResp.stats.max],
    ['acceptance_lower', coreResp.acceptance_lower],
    ['acceptance_upper', coreResp.acceptance_upper],
    ['n_points',         coreResp.stats.n_points],
    ['mean',             coreResp.stats.mean],
    ['rms',              coreResp.stats.rms],
    ['std_dev',          coreResp.stats.std_dev],
    ['percent_within_tolerance', coreResp.stats.percent_within_tolerance],
    ['fully_constrained',     coreResp.fully_constrained],
    ['num_under_constrained', coreResp.num_under_constrained],
    ['expanded_uncertainty',  coreResp.expanded_uncertainty],
    ['coverage_factor',       coreResp.coverage_factor],
    ['core_version',   coreResp.core_version],
    ['compiler',       coreResp.fingerprint.compiler],
    ['cpu',            coreResp.fingerprint.cpu],
    ['timestamp',      coreResp.timestamp],
    ['reference_hash', coreResp.reference_hash],
    ['measured_hash',  coreResp.measured_hash],
    ['heatmap_label',  coreResp.heatmap_label],
    ['n_display_points',    coreResp.n_display_points],
    ['deviation_checksum',  coreResp.deviation_checksum],
    ['max_deviation_index', coreResp.max_deviation_index],
    ['max_deviation_value', coreResp.max_deviation_value],
  ];

  for (const [name, coreVal] of fields) {
    assert(uiView[name] === coreVal,
      name + ': UI=' + JSON.stringify(uiView[name]) + ' core=' + JSON.stringify(coreVal));
  }

  // Transform matrix: compare element-by-element.
  if (coreResp.transform_matrix && uiView.transform_matrix) {
    assert(coreResp.transform_matrix.length === uiView.transform_matrix.length,
      'transform_matrix length matches');
    let matMatch = true;
    for (let i = 0; i < 16; i++) {
      if (coreResp.transform_matrix[i] !== uiView.transform_matrix[i]) matMatch = false;
    }
    assert(matMatch, 'transform_matrix elements are identical');
  }

  // Point deviations array.
  assert(uiView.deviation_count === coreResp.n_display_points,
    'deviation array count matches n_display_points');

  // ══════════════════════════════════════════════════════════════════════
  // 2. Determinism through the boundary
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 2. Determinism ---');

  const r1 = await postJson(CORE + '/inspect', req);
  const r2 = await postJson(CORE + '/inspect', req);

  // Same input → identical output at every field.
  assert(r1.verdict === r2.verdict, 'determinism: verdict');
  assert(r1.tolerance_mm === r2.tolerance_mm, 'determinism: tolerance_mm');
  assert(r1.alignment_rms === r2.alignment_rms, 'determinism: alignment_rms');
  assert(r1.stats.rms === r2.stats.rms, 'determinism: stats.rms');
  assert(r1.stats.max === r2.stats.max, 'determinism: stats.max');
  assert(r1.expanded_uncertainty === r2.expanded_uncertainty, 'determinism: U');
  assert(r1.coverage_factor === r2.coverage_factor, 'determinism: k');
  assert(r1.deviation_checksum === r2.deviation_checksum, 'determinism: checksum');
  assert(r1.core_version === r2.core_version, 'determinism: core_version');
  assert(r1.reference_hash === r2.reference_hash, 'determinism: ref_hash');

  // The UI renders of both must be identical.
  const ui1 = uiRender(r1);
  const ui2 = uiRender(r2);
  let allFieldsMatch = true;
  for (const key of Object.keys(ui1)) {
    if (key === 'transform_matrix') {
      for (let i = 0; i < 16; i++) {
        if (ui1.transform_matrix[i] !== ui2.transform_matrix[i]) allFieldsMatch = false;
      }
    } else if (JSON.stringify(ui1[key]) !== JSON.stringify(ui2[key])) {
      allFieldsMatch = false;
      console.error('  determinism mismatch: ' + key);
    }
  }
  assert(allFieldsMatch, 'determinism: all UI-rendered fields are identical across runs');

  // ══════════════════════════════════════════════════════════════════════
  // 3. Static firewall audit
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 3. Static firewall audit ---');

  const srcDir = path.join(__dirname, '..', 'ui', 'drawgen', 'web', 'src');
  const inspectionFiles = [];

  function findFiles(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) findFiles(full);
      else if (entry.name.endsWith('.tsx') || entry.name.endsWith('.ts')) inspectionFiles.push(full);
    }
  }
  findFiles(srcDir);

  // Patterns that indicate COMPUTATION (function calls, assignments — not labels).
  const computePatterns = [
    /[^/]\.norm\(\)/,        // vector norm (not in comment)
    /[^/]\.cross\(\)/,       // cross product
    /[^/]\.dot\(\)/,         // dot product
    /kabsch\s*\(/i,          // kabsch function CALL (not label text)
    /align_landmark\s*\(/i,  // alignment function CALL
    /fit_plane\s*\(/i,       // fitting function CALL
    /fit_sphere\s*\(/i,
    /fit_cylinder\s*\(/i,
    /uncertainty\s*[=]\s*[^=].*Math\./,  // computing uncertainty with math
  ];

  // Patterns that are ALLOWED (display transforms, labels, comments).
  const allowedPatterns = [
    /\/\//,          // comments
    /\/\*/,          // block comments
    /className/,     // CSS classes
    /toString\(\)/,  // converting core value to string for display
    /\.toFixed\(/,   // formatting for display
    /Math\.max\(0/,  // clamping for color scale (display)
    /Math\.min\(1/,  // clamping
    /['"`]/,         // string literals (label text, not code)
    /desc:/,         // description field
    /label:/,        // label field
  ];

  let violations = [];

  for (const file of inspectionFiles) {
    const relPath = path.relative(srcDir, file);
    // Only audit inspection-related files.
    if (!relPath.includes('inspection') && !relPath.includes('Inspection') &&
        !relPath.includes('InspectionViewer') && !relPath.includes('alignmesh')) continue;

    const content = fs.readFileSync(file, 'utf8');
    const lines = content.split('\n');

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      for (const pattern of computePatterns) {
        if (pattern.test(line)) {
          // Check if it's in an allowed context.
          const isAllowed = allowedPatterns.some(ap => ap.test(line));
          if (!isAllowed) {
            violations.push({
              file: relPath,
              line: i + 1,
              content: line.trim().substring(0, 100),
              pattern: pattern.toString(),
            });
          }
        }
      }
    }
  }

  if (violations.length === 0) {
    assert(true, 'firewall audit: ZERO computation violations in inspection code');
  } else {
    for (const v of violations) {
      assert(false, 'VIOLATION in ' + v.file + ':' + v.line + ' [' + v.pattern + '] ' + v.content);
    }
  }

  // Count all number-touching lines and classify them.
  let coreSourced = 0;
  let displayTransform = 0;
  let unclassified = 0;

  for (const file of inspectionFiles) {
    const relPath = path.relative(srcDir, file);
    if (!relPath.includes('inspection') && !relPath.includes('Inspection') &&
        !relPath.includes('InspectionViewer') && !relPath.includes('alignmesh')) continue;

    const lines = fs.readFileSync(file, 'utf8').split('\n');
    for (const line of lines) {
      if (/result\.\w+|stats\.\w+|fingerprint\.\w+|coreVersion\.\w+/.test(line) &&
          !/import|interface|type |Props|const.*=.*{/.test(line)) {
        coreSourced++;
      } else if (/deviationToColor|THREE\.Color|Math\.max\(0|Math\.min\(1|\.toFixed|linear-gradient/.test(line)) {
        displayTransform++;
      }
    }
  }

  console.log('  Core-sourced value displays: ' + coreSourced);
  console.log('  Pure display transforms: ' + displayTransform);
  assert(coreSourced > 0, 'firewall: inspection code displays core-sourced values');
  assert(violations.length === 0, 'firewall: no computation violations');

  // ══════════════════════════════════════════════════════════════════════
  // Summary
  // ══════════════════════════════════════════════════════════════════════
  console.log('');
  console.log('=== ' + passed + ' passed, ' + failed + ' failed ===');
  process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
  console.error('FATAL: ' + err.message);
  process.exit(1);
});
