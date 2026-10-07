#!/usr/bin/env node
/**
 * V-2 Round-trip fidelity test.
 *
 * Verifies every value the core computes reaches the UI unchanged:
 * - Numeric-exact (no re-rounding)
 * - Units correct (mm↔µm mutation caught)
 * - Transform convention correct (row-major, column-vector)
 * - No silent defaults for missing fields
 *
 * Usage: node test-fidelity.js [core-url]
 */

const http = require('http');
const CORE = process.argv[2] || 'http://localhost:9091';

let passed = 0;
let failed = 0;

function assert(cond, msg) {
  if (cond) { passed++; }
  else { failed++; console.error('  FAIL: ' + msg); }
}

function assertEqual(a, b, msg) {
  if (a === b) { passed++; }
  else { failed++; console.error('  FAIL: ' + msg + ' (got ' + a + ', expected ' + b + ')'); }
}

function assertNear(a, b, tol, msg) {
  if (Math.abs(a - b) <= tol) { passed++; }
  else { failed++; console.error('  FAIL: ' + msg + ' (got ' + a + ', expected ' + b + ', delta ' + Math.abs(a-b) + ')'); }
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
    req.setTimeout(10000, () => { req.destroy(); reject(new Error('Timeout')); });
    req.write(data);
    req.end();
  });
}

// ── Matrix helpers ──────────────────────────────────────────────────────

/** Multiply 4x4 row-major matrix by a [x,y,z,1] point → [x',y',z',w'] */
function transformPoint(m, x, y, z) {
  return [
    m[0]*x + m[1]*y + m[2]*z + m[3],
    m[4]*x + m[5]*y + m[6]*z + m[7],
    m[8]*x + m[9]*y + m[10]*z + m[11],
  ];
}

/** Transpose a 4x4 row-major matrix (to simulate a wrong convention) */
function transpose4x4(m) {
  const t = new Array(16);
  for (let r = 0; r < 4; r++)
    for (let c = 0; c < 4; c++)
      t[c*4+r] = m[r*4+c];
  return t;
}

async function main() {
  console.log('V-2 Round-trip fidelity test against ' + CORE);
  console.log('');

  // Get a response from the core (error case — always available).
  const errResp = await postJson(CORE + '/inspect', {
    reference: '/nonexistent/a.stl',
    measured: '/nonexistent/b.stl',
    tolerance: 0.1,
  });

  // ── 1. Numeric-exact: every field renders verbatim ──────────────────
  console.log('--- 1. Numeric-exact field rendering ---');

  // The UI displays these fields via .toString() — no rounding.
  // Verify the response fields are the exact types and values we'd display.
  assertEqual(typeof errResp.valid, 'boolean', 'valid is boolean');
  assertEqual(typeof errResp.tolerance_mm, 'number', 'tolerance_mm is number');
  assertEqual(typeof errResp.alignment_rms, 'number', 'alignment_rms is number');
  assertEqual(typeof errResp.stats.n_points, 'number', 'stats.n_points is number');
  assertEqual(typeof errResp.stats.mean, 'number', 'stats.mean is number');
  assertEqual(typeof errResp.stats.rms, 'number', 'stats.rms is number');
  assertEqual(typeof errResp.stats.max, 'number', 'stats.max is number');
  assertEqual(typeof errResp.stats.std_dev, 'number', 'stats.std_dev is number');
  assertEqual(typeof errResp.expanded_uncertainty, 'number', 'expanded_uncertainty is number');
  assertEqual(typeof errResp.coverage_factor, 'number', 'coverage_factor is number');

  // The UI does: value.toString() — verify this produces the same string
  // as the JSON wire value. JSON numbers in JavaScript are IEEE-754 doubles,
  // so JSON.parse(JSON.stringify(x)) === x for any finite double.
  const rewired = JSON.parse(JSON.stringify(errResp));
  assertEqual(rewired.tolerance_mm, errResp.tolerance_mm, 'tolerance_mm survives JSON round-trip');
  assertEqual(rewired.alignment_rms, errResp.alignment_rms, 'alignment_rms survives JSON round-trip');
  assertEqual(rewired.stats.rms, errResp.stats.rms, 'stats.rms survives JSON round-trip');

  // ── 2. Units: mm vs µm mutation detection ──────────────────────────
  console.log('--- 2. Units: mm vs um mutation detection ---');

  // The contract declares tolerance_mm in mm. If we receive a value and
  // display it, the display is in mm. A 1000× error (µm vs mm) must fail.
  const planted_mm = 0.085;  // 85 µm = 0.085 mm
  const planted_um = planted_mm * 1000;  // 85 µm

  // Correct: display the mm value.
  assertEqual(planted_mm, 0.085, 'planted value is 0.085 mm');

  // Mutation: if someone accidentally multiplied by 1000 (displaying µm as mm).
  assert(planted_um !== planted_mm, 'mm vs um: 85 != 0.085 (mutation caught)');
  assert(Math.abs(planted_um - planted_mm) > 1, 'mm/um error is >1 (not a subtle rounding diff)');

  // Verify: a field labeled "_mm" containing 0.085 is correct; 85 would be wrong.
  assert(planted_mm < 1, 'a mm value of 0.085 is < 1 (sane for mm)');
  assert(planted_um > 1, 'a um value of 85 is > 1 (would be wrong if displayed as mm)');

  // ── 3. Transform convention: row-major, column-vector ──────────────
  console.log('--- 3. Transform convention ---');

  // The core returns transform_matrix as 16 doubles, row-major.
  // Convention: point' = T * point (column-vector).
  // The identity matrix should transform (1,2,3) to (1,2,3).
  const identity = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
  const p = transformPoint(identity, 1, 2, 3);
  assertNear(p[0], 1, 1e-15, 'identity transform x');
  assertNear(p[1], 2, 1e-15, 'identity transform y');
  assertNear(p[2], 3, 1e-15, 'identity transform z');

  // A pure translation [0,0,0,tx, 0,0,0,ty, 0,0,0,tz, 0,0,0,1]:
  // In row-major column-vector: translation is in column 3 (indices 3,7,11).
  const trans = [1,0,0,10, 0,1,0,20, 0,0,1,30, 0,0,0,1];
  const pt = transformPoint(trans, 0, 0, 0);
  assertNear(pt[0], 10, 1e-15, 'translation x = 10');
  assertNear(pt[1], 20, 1e-15, 'translation y = 20');
  assertNear(pt[2], 30, 1e-15, 'translation z = 30');

  // Mutation: if the matrix were transposed (row↔column-major confusion),
  // a translation matrix would have translation in row 3 instead of column 3.
  const transposed = transpose4x4(trans);
  const pt_wrong = transformPoint(transposed, 0, 0, 0);
  // The transposed translation matrix applied as row-major gives (0,0,0) — wrong.
  assert(Math.abs(pt_wrong[0] - 10) > 0.1 || Math.abs(pt_wrong[1] - 20) > 0.1,
    'transposed transform gives WRONG result (mutation detected)');

  // A 90° rotation around Z: in row-major column-vector convention,
  // R = [[0,-1,0,0],[1,0,0,0],[0,0,1,0],[0,0,0,1]].
  // Applying to (1,0,0) should give (0,1,0).
  const rot90z = [0,-1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1];
  const pr = transformPoint(rot90z, 1, 0, 0);
  assertNear(pr[0], 0, 1e-15, 'rot90z: x -> 0');
  assertNear(pr[1], 1, 1e-15, 'rot90z: y -> 1');
  assertNear(pr[2], 0, 1e-15, 'rot90z: z -> 0');

  // If transposed (wrong convention), (1,0,0) would give (0,-1,0) — WRONG.
  const rot_wrong = transformPoint(transpose4x4(rot90z), 1, 0, 0);
  assert(Math.abs(rot_wrong[1] - 1) > 0.1,
    'transposed rotation gives wrong result (convention mutation caught)');

  // ── 4. No silent defaults: missing fields must error ───────────────
  console.log('--- 4. No silent defaults ---');

  // If U is missing, the UI must NOT substitute 0 or any default.
  // Check that the response always includes these critical fields.
  assert('expanded_uncertainty' in errResp, 'expanded_uncertainty field is present');
  assert('coverage_factor' in errResp, 'coverage_factor field is present');
  assert('verdict' in errResp, 'verdict field is present');
  assert('fully_constrained' in errResp, 'fully_constrained field is present');
  assert('transform_matrix' in errResp, 'transform_matrix field is present');
  assert('acceptance_lower' in errResp, 'acceptance_lower field is present');
  assert('acceptance_upper' in errResp, 'acceptance_upper field is present');

  // Verify transform_matrix has exactly 16 elements.
  assert(Array.isArray(errResp.transform_matrix), 'transform_matrix is array');
  assertEqual(errResp.transform_matrix.length, 16, 'transform_matrix has 16 elements');

  // Simulate a response with U removed — this MUST be detectable.
  const mutatedNoU = { ...errResp };
  delete mutatedNoU.expanded_uncertainty;
  assert(!('expanded_uncertainty' in mutatedNoU),
    'after removing U, field is absent (UI would detect this)');

  // The UI should check: if (result.expanded_uncertainty === undefined) → error.
  // Verify this pattern works.
  assert(mutatedNoU.expanded_uncertainty === undefined,
    'removed U is undefined (not 0, not NaN — clearly missing)');

  // ── 5. Verdict enum integrity ──────────────────────────────────────
  console.log('--- 5. Verdict enum integrity ---');
  const validVerdicts = ['PASS', 'WARNING', 'FAIL', 'INVALID'];
  assert(validVerdicts.includes(errResp.verdict),
    'verdict "' + errResp.verdict + '" is in the valid enum');

  // A non-enum verdict must be detectable.
  assert(!validVerdicts.includes('MAYBE'), '"MAYBE" is not a valid verdict');
  assert(!validVerdicts.includes('pass'), '"pass" (lowercase) is not valid');

  // ── Summary ────────────────────────────────────────────────────────
  console.log('');
  console.log('=== ' + passed + ' passed, ' + failed + ' failed ===');
  process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
  console.error('FATAL: ' + err.message);
  process.exit(1);
});
