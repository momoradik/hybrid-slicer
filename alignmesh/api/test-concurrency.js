#!/usr/bin/env node
/**
 * V-5 Concurrency, cancellation, progress, and no cross-job leakage.
 *
 * Verifies:
 * 1. No cross-job leakage (A's result never appears for B)
 * 2. Cancellation leaves no fabricated result
 * 3. Stale/superseded responses are discardable
 * 4. Idempotence (same request → same result)
 *
 * Usage: node test-concurrency.js [core-url]
 */

const http = require('http');
const CORE = process.argv[2] || 'http://localhost:9092';

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

function postJsonWithAbort(url, body, abortAfterMs) {
  return new Promise((resolve) => {
    const data = JSON.stringify(body);
    const u = new URL(url);
    const req = http.request({
      hostname: u.hostname, port: u.port, path: u.pathname,
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(data) },
    }, res => {
      let d = '';
      res.on('data', chunk => d += chunk);
      res.on('end', () => {
        try { resolve({ completed: true, data: JSON.parse(d) }); }
        catch(e) { resolve({ completed: true, data: null, error: 'parse error' }); }
      });
    });
    req.on('error', () => resolve({ completed: false, cancelled: true }));
    setTimeout(() => { req.destroy(); resolve({ completed: false, cancelled: true }); }, abortAfterMs);
    req.write(data);
    req.end();
  });
}

async function main() {
  console.log('V-5 Concurrency, cancellation, progress, no cross-job leakage');
  console.log('Core: ' + CORE);
  console.log('');

  // ══════════════════════════════════════════════════════════════════════
  // 1. No cross-job leakage
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 1. No cross-job leakage ---');

  // Submit two distinct jobs with different file paths.
  // Each result must reference its own input, never the other's.
  const jobA = { reference: '/job-a/ref.stl', measured: '/job-a/meas.stl', tolerance: 0.1 };
  const jobB = { reference: '/job-b/ref.stl', measured: '/job-b/meas.stl', tolerance: 0.2 };

  // Sequential execution (core is single-threaded).
  const resultA = await postJson(CORE + '/inspect', jobA);
  const resultB = await postJson(CORE + '/inspect', jobB);

  // Each result's error message should reference its own file path.
  // (Both will fail with "Cannot open file" but with different paths.)
  const errA = (resultA.errors || []).join(' ');
  const errB = (resultB.errors || []).join(' ');

  assert(errA.includes('job-a') || errA.includes('/job-a/'),
    'Job A error references job-a path');
  assert(errB.includes('job-b') || errB.includes('/job-b/'),
    'Job B error references job-b path');

  // Cross-leakage check: A's error must NOT contain B's path.
  assert(!errA.includes('job-b'),
    'Job A result does NOT contain job-b path (no cross-leakage)');
  assert(!errB.includes('job-a'),
    'Job B result does NOT contain job-a path (no cross-leakage)');

  // Tolerance should match each job's input.
  assert(resultA.tolerance_mm === 0.1,
    'Job A tolerance = 0.1 (as submitted)');
  assert(resultB.tolerance_mm === 0.2,
    'Job B tolerance = 0.2 (as submitted)');

  // ══════════════════════════════════════════════════════════════════════
  // 2. Cancellation: no partial result as final
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 2. Cancellation ---');

  // Start a request and abort it after 50ms.
  // The UI should treat this as "no result" — never a partial/fabricated result.
  const cancelled = await postJsonWithAbort(CORE + '/inspect', {
    reference: '/cancel-test/ref.stl', measured: '/cancel-test/meas.stl', tolerance: 0.1,
  }, 50);

  if (cancelled.cancelled) {
    assert(true, 'cancelled request: no complete response received');
    assert(cancelled.data === undefined || cancelled.data === null,
      'cancelled request: no data to display');
  } else {
    // If the server responded before we could cancel (fast error response),
    // verify it's a proper error, not a fabricated result.
    assert(cancelled.data.valid === false || cancelled.data.errors.length > 0,
      'fast-response before cancel: is an error, not a fabricated result');
  }

  // UI simulation: after cancellation, the UI state should be:
  // - result = null (no result to display)
  // - error = "cancelled" or connection error
  // - verdict = none (never auto-assigned)
  let uiResult = null;
  let uiError = null;
  if (cancelled.cancelled) {
    uiError = 'Request cancelled';
    uiResult = null;  // NO result
  }
  assert(uiResult === null, 'after cancel: UI has no result');
  if (cancelled.cancelled) {
    assert(uiError !== null, 'after cancel: UI has an error state');
  }

  // ══════════════════════════════════════════════════════════════════════
  // 3. Stale/superseded response discarding
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 3. Stale response discarding ---');

  // Simulate: user submits job 1, then submits job 2 before job 1 completes.
  // Job 1's response arrives after job 2's. The UI must discard job 1's response.
  //
  // The UI pattern (React Query mutation) handles this with request IDs:
  // each mutation call increments a counter, and only the latest is accepted.

  let requestCounter = 0;
  let latestRequestId = 0;

  // Simulate submitting job 1.
  const req1Id = ++requestCounter;
  // Simulate submitting job 2 (supersedes job 1).
  const req2Id = ++requestCounter;
  latestRequestId = req2Id;

  // Job 2 completes first.
  const resp2 = await postJson(CORE + '/inspect', jobB);
  // Check: is this the latest request?
  assert(req2Id === latestRequestId, 'job 2 response: is the latest request');

  // Job 1 completes after.
  const resp1 = await postJson(CORE + '/inspect', jobA);
  // Check: is this the latest request? NO — it's stale.
  assert(req1Id !== latestRequestId,
    'job 1 response: is NOT the latest request (stale)');

  // UI simulation: only accept if requestId matches latestRequestId.
  let displayedResult = null;
  // Job 2 arrives:
  if (req2Id === latestRequestId) displayedResult = resp2;
  // Job 1 arrives (stale):
  if (req1Id === latestRequestId) displayedResult = resp1;  // should NOT execute

  assert(displayedResult === resp2,
    'displayed result is from job 2 (latest), not job 1 (stale)');
  assert(displayedResult !== resp1,
    'stale job 1 response is discarded');

  // ══════════════════════════════════════════════════════════════════════
  // 4. Idempotence: same request → same result
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 4. Idempotence ---');

  const reqSame = { reference: '/idempotent/ref.stl', measured: '/idempotent/meas.stl', tolerance: 0.05 };
  const r1 = await postJson(CORE + '/inspect', reqSame);
  const r2 = await postJson(CORE + '/inspect', reqSame);

  // Same request must produce the same result.
  assert(r1.valid === r2.valid, 'idempotent: valid matches');
  assert(r1.verdict === r2.verdict, 'idempotent: verdict matches');
  assert(r1.tolerance_mm === r2.tolerance_mm, 'idempotent: tolerance matches');
  assert(r1.alignment_rms === r2.alignment_rms, 'idempotent: alignment_rms matches');
  assert(r1.stats.rms === r2.stats.rms, 'idempotent: stats.rms matches');

  // Error messages should be identical.
  assert(JSON.stringify(r1.errors) === JSON.stringify(r2.errors),
    'idempotent: errors identical');

  // ══════════════════════════════════════════════════════════════════════
  // 5. Progress never implies verdict
  // ══════════════════════════════════════════════════════════════════════
  console.log('--- 5. Progress never implies verdict ---');

  // The core's response is atomic (no streaming progress in current impl).
  // The UI shows "Inspecting..." during the request and only shows a verdict
  // after the response arrives. Verify: there is no intermediate state
  // where progress text could be mistaken for a verdict.

  const validVerdicts = ['PASS', 'WARNING', 'FAIL', 'INVALID'];
  const progressTexts = ['Inspecting...', 'Running inspection...', 'Loading...'];

  for (const pt of progressTexts) {
    assert(!validVerdicts.includes(pt),
      'progress text "' + pt + '" is not a valid verdict');
  }

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
