#!/usr/bin/env node
/**
 * CI gate: runs all V-tests against a live core instance.
 *
 * Usage: node ci-gate.js [core-url]
 *
 * This script:
 * 1. Verifies the generated types match the contract (no hand-edits)
 * 2. Runs V-1 through V-6 tests
 * 3. Exits 0 on success, 1 on any failure
 *
 * Add to CI as a required gate — any break blocks the build.
 */

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const CORE = process.argv[2] || 'http://localhost:9000';
const API_DIR = __dirname;
const ROOT = path.join(API_DIR, '..');
const GEN_FILE = path.join(ROOT, 'ui', 'drawgen', 'web', 'src', 'api', 'alignmesh-types.generated.ts');

let allPassed = true;

function run(label, cmd) {
  console.log('');
  console.log('══════════════════════════════════════════');
  console.log(label);
  console.log('══════════════════════════════════════════');
  try {
    const out = execSync(cmd, { encoding: 'utf8', timeout: 120000 });
    const lastLine = out.trim().split('\n').pop();
    console.log(lastLine);
    if (lastLine.includes('failed') && !lastLine.includes('0 failed') && !lastLine.includes('0 real fail')) {
      allPassed = false;
      console.log('>>> GATE FAILED <<<');
    }
  } catch (e) {
    allPassed = false;
    console.log('>>> GATE FAILED <<<');
    if (e.stdout) console.log(e.stdout.toString().trim().split('\n').pop());
    if (e.stderr) console.error(e.stderr.toString().trim().split('\n').slice(-3).join('\n'));
  }
}

// ── 0. Generated types match contract ──────────────────────────────────

console.log('');
console.log('══════════════════════════════════════════');
console.log('Gate 0: Generated types match contract');
console.log('══════════════════════════════════════════');

// Regenerate and compare to the committed file.
const freshTypes = execSync('node ' + path.join(API_DIR, 'generate-types.js'), { encoding: 'utf8' });
const committedTypes = fs.readFileSync(GEN_FILE, 'utf8');

if (freshTypes === committedTypes) {
  console.log('PASS: generated types match contract (no hand-edits)');
} else {
  console.log('FAIL: generated types differ from contract — someone hand-edited them');
  console.log('  Run: node api/generate-types.js > ui/drawgen/web/src/api/alignmesh-types.generated.ts');
  allPassed = false;
}

// ── 1–6. V-tests ───────────────────────────────────────────────────────

run('Gate 1 (V-1): Contract conformance',
  'node ' + path.join(API_DIR, 'test-conformance.js') + ' ' + CORE);

run('Gate 2 (V-2): Round-trip fidelity',
  'node ' + path.join(API_DIR, 'test-fidelity.js') + ' ' + CORE);

run('Gate 3 (V-3): Verdict integrity',
  'node ' + path.join(API_DIR, 'test-verdict-integrity.js') + ' ' + CORE);

run('Gate 4 (V-4): Large-payload completeness',
  'node ' + path.join(API_DIR, 'test-large-payload.js') + ' ' + CORE);

run('Gate 5 (V-5): Concurrency/leakage',
  'node ' + path.join(API_DIR, 'test-concurrency.js') + ' ' + CORE);

run('Gate 6 (V-6): End-to-end equivalence',
  'node ' + path.join(API_DIR, 'test-capstone.js') + ' ' + CORE);

// ── Summary ────────────────────────────────────────────────────────────

console.log('');
console.log('══════════════════════════════════════════');
if (allPassed) {
  console.log('ALL CI GATES PASSED');
} else {
  console.log('CI GATES FAILED — build blocked');
}
console.log('══════════════════════════════════════════');

process.exit(allPassed ? 0 : 1);
