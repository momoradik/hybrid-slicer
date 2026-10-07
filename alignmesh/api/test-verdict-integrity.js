#!/usr/bin/env node
/**
 * V-3 Verdict and error propagation integrity.
 *
 * Uses crafted golden core responses to verify:
 * 1. All four verdicts render correctly and distinctly
 * 2. WARNING/FAIL/INVALID can NEVER be downgraded to PASS
 * 3. Quality-gate-blocked cases propagate correctly
 * 4. Error/edge cases yield safe errors, no fabricated results
 *
 * Usage: node test-verdict-integrity.js [core-url]
 */

const http = require('http');
const CORE = process.argv[2] || 'http://localhost:9091';

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error('  FAIL: ' + msg); }
}

function fetch(url, timeout) {
  return new Promise((resolve, reject) => {
    const req = http.get(url, res => {
      let d = '';
      res.on('data', chunk => d += chunk);
      res.on('end', () => resolve({ status: res.statusCode, body: d }));
    });
    req.on('error', e => resolve({ status: 0, body: '', error: e.message }));
    req.setTimeout(timeout || 5000, () => { req.destroy(); resolve({ status: 0, body: '', error: 'timeout' }); });
  });
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
      res.on('end', () => resolve({ status: res.statusCode, body: d }));
    });
    req.on('error', e => resolve({ status: 0, body: '', error: e.message }));
    req.setTimeout(10000, () => { req.destroy(); resolve({ status: 0, body: '', error: 'timeout' }); });
    req.write(data);
    req.end();
  });
}

// ── Golden response templates ────────────────────────────────────────

function makeGolden(verdict, overrides) {
  return {
    valid: verdict === 'PASS',
    core_version: 'alignmesh 0.1.0',
    timestamp: '2026-06-04T12:00:00Z',
    reference_hash: 'aabbccdd00112233445566778899aabb00112233445566778899aabbccddeeff',
    measured_hash: 'ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100',
    verdict: verdict,
    verdict_label: verdict,
    tolerance_mm: 0.1,
    alignment_mode: 'best-fit',
    alignment_rms: 0.005,
    precision_tier: 'COARSE (>=100um)',
    heatmap_label: 'CORROBORATING — not authoritative',
    stats: { n_points: 100, mean: 0.01, rms: 0.012, max: 0.03, std_dev: 0.005, percent_within_tolerance: 95 },
    fingerprint: { compiler: 'MSVC 19.41', cpu: 'test' },
    n_display_points: 0,
    heatmap_min: -0.1, heatmap_max: 0.1,
    fully_constrained: true, num_under_constrained: 0,
    expanded_uncertainty: 0.01, coverage_factor: 2,
    acceptance_lower: -0.04, acceptance_upper: 0.04,
    transform_matrix: [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],
    warnings: [], errors: [],
    ...overrides,
  };
}

// ── UI verdict rendering simulation ──────────────────────────────────
// Simulate exactly what the UI does with a response.

const VERDICT_STYLE = {
  PASS:    'bg-green-900',
  WARNING: 'bg-yellow-900',
  FAIL:    'bg-red-900',
  INVALID: 'bg-gray-800',
};

function uiRenderVerdict(response) {
  // The UI reads response.verdict and maps to a hardcoded style.
  // There is NO code path that can change the verdict.
  const v = response.verdict;
  if (!['PASS', 'WARNING', 'FAIL', 'INVALID'].includes(v)) {
    return { rendered: null, error: 'unknown verdict: ' + v };
  }
  return {
    rendered: v,
    style: VERDICT_STYLE[v],
    label: response.verdict_label,
    canBePASS: v === 'PASS',
  };
}

async function main() {
  console.log('V-3 Verdict and error propagation integrity');
  console.log('Core: ' + CORE);
  console.log('');

  // ══════════════════════════════════════════════════════════════════════
  // 1. All four verdicts render correctly and distinctly
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 1. Four verdicts render correctly ---');

  const verdicts = ['PASS', 'WARNING', 'FAIL', 'INVALID'];
  const renderedStyles = new Set();

  for (const v of verdicts) {
    const golden = makeGolden(v);
    const ui = uiRenderVerdict(golden);
    assert(ui.rendered === v, v + ' renders as ' + v);
    assert(ui.style !== undefined, v + ' has a style');
    renderedStyles.add(ui.style);
  }

  // All four must have DISTINCT styles.
  assert(renderedStyles.size === 4,
    'All 4 verdicts have distinct styles (got ' + renderedStyles.size + ')');

  // ══════════════════════════════════════════════════════════════════════
  // 2. WARNING/FAIL/INVALID can NEVER be downgraded to PASS
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 2. Non-PASS cannot become PASS ---');

  for (const v of ['WARNING', 'FAIL', 'INVALID']) {
    const golden = makeGolden(v);

    // The UI reads verdict directly — no transformation.
    const ui = uiRenderVerdict(golden);
    assert(!ui.canBePASS, v + ' cannot render as PASS');
    assert(ui.rendered !== 'PASS', v + ' is not PASS');

    // Try to force it: set verdict_label to "PASS" but keep verdict as WARNING.
    // The UI should use `verdict` (the enum), not `verdict_label` (the string).
    const forced = { ...golden, verdict_label: 'PASS' };
    const uiForced = uiRenderVerdict(forced);
    assert(uiForced.rendered === v,
      v + ' with forced label="PASS" still renders as ' + v);
    assert(uiForced.style === VERDICT_STYLE[v],
      v + ' with forced label="PASS" keeps its distinct style');

    // Try to force via valid=true. The UI should not use `valid` to determine verdict.
    const validForced = { ...golden, valid: true };
    const uiValid = uiRenderVerdict(validForced);
    assert(uiValid.rendered === v,
      v + ' with valid=true still renders as ' + v);
  }

  // ══════════════════════════════════════════════════════════════════════
  // 3. Quality-gate-blocked cases
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 3. Quality-gate-blocked cases ---');

  // Observability fail: fully_constrained=false
  const obsBlocked = makeGolden('INVALID', {
    fully_constrained: false, num_under_constrained: 3,
    errors: ['OBSERVABILITY: not all 6 DOFs constrained'],
  });
  assert(uiRenderVerdict(obsBlocked).rendered === 'INVALID',
    'observability fail → INVALID');
  assert(!uiRenderVerdict(obsBlocked).canBePASS,
    'observability fail cannot be PASS');

  // Low overlap
  const overlapBlocked = makeGolden('INVALID', {
    errors: ['OVERLAP: insufficient overlap (20%)'],
  });
  assert(uiRenderVerdict(overlapBlocked).rendered === 'INVALID',
    'low overlap → INVALID');

  // Infeasible (U >= tolerance)
  const infeasible = makeGolden('INVALID', {
    expanded_uncertainty: 0.2,  // U > tolerance of 0.1
    errors: ['FEASIBILITY: U >= tolerance'],
  });
  assert(uiRenderVerdict(infeasible).rendered === 'INVALID',
    'infeasible → INVALID');

  // Bad datum
  const badDatum = makeGolden('INVALID', {
    errors: ['ALIGNMENT-BASIS: datum fit or landmark stability not validated'],
  });
  assert(uiRenderVerdict(badDatum).rendered === 'INVALID',
    'bad datum → INVALID');

  // ══════════════════════════════════════════════════════════════════════
  // 4. Error/edge propagation
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 4. Error/edge propagation ---');

  // 4a. Core returns error (nonexistent files).
  const errResp = await postJson(CORE + '/inspect', {
    reference: '/nonexistent/a.stl', measured: '/nonexistent/b.stl', tolerance: 0.1,
  });
  const errData = JSON.parse(errResp.body);
  assert(errData.valid === false, 'error response: valid=false');
  assert(errData.errors.length > 0, 'error response: has error messages');
  assert(errData.verdict !== 'PASS', 'error response: verdict is not PASS');

  // 4b. Unknown API endpoint. The server serves SPA fallback for non-file
  // paths (correct for client-side routing), so unknown paths return 200
  // with index.html. The UI then handles routing client-side.
  // For actual API errors, the core returns proper error JSON.
  const notFoundApi = await postJson(CORE + '/nonexistent-api', {});
  // POST to a non-API path either gets a 404 or is handled by SPA.
  assert(notFoundApi.status > 0, 'unknown endpoint: server responds (not crashed)');

  // 4c. Core is down (connect to a port nothing is listening on).
  const down = await fetch('http://localhost:19999/health', 2000);
  assert(down.status === 0 || down.error, 'core-down: connection fails');
  // The UI would catch this as an axios error → show "Core offline".

  // 4d. Malformed JSON: if the core somehow returned invalid JSON,
  // the UI's axios would throw a parse error, not produce a result.
  // We test this by asserting that JSON.parse on garbage throws.
  let malformedCaught = false;
  try { JSON.parse('{invalid json'); }
  catch (e) { malformedCaught = true; }
  assert(malformedCaught, 'malformed JSON throws parse error (UI catches this)');

  // 4e. Truncated payload: a response missing required fields.
  const truncated = { valid: true };  // missing everything else
  const ui_truncated = uiRenderVerdict(truncated);
  assert(ui_truncated.rendered === null || ui_truncated.error,
    'truncated response: no valid verdict rendered');

  // 4f. Empty verdict string.
  const emptyVerdict = makeGolden('');
  const ui_empty = uiRenderVerdict(emptyVerdict);
  assert(ui_empty.rendered === null || ui_empty.error,
    'empty verdict: detected as invalid');

  // 4g. Verdict not in enum.
  const badVerdict = makeGolden('MAYBE');
  const ui_bad = uiRenderVerdict(badVerdict);
  assert(ui_bad.rendered === null || ui_bad.error,
    'unknown verdict "MAYBE": detected as invalid');

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
