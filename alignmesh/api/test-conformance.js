#!/usr/bin/env node
/**
 * Contract conformance test.
 *
 * Takes a live core response and validates it against api/contract.json.
 * Fails loudly if the core emits a field/type the contract doesn't declare.
 *
 * Usage:
 *   node test-conformance.js [core-url]
 *   Default core-url: http://localhost:9090
 *
 * Exit 0 = all pass. Exit 1 = conformance failure.
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const CORE = process.argv[2] || 'http://localhost:9090';
const contract = JSON.parse(
  fs.readFileSync(path.join(__dirname, 'contract.json'), 'utf8')
);
const defs = contract.$defs;

let passed = 0;
let failed = 0;

function assert(condition, msg) {
  if (condition) {
    passed++;
  } else {
    failed++;
    console.error(`  FAIL: ${msg}`);
  }
}

function resolveRef(ref) {
  return defs[ref.split('/').pop()];
}

function validateObject(obj, schema, path) {
  if (schema.$ref) {
    schema = resolveRef(schema.$ref);
  }

  if (schema.type === 'object') {
    assert(typeof obj === 'object' && obj !== null, `${path} should be object, got ${typeof obj}`);
    if (typeof obj !== 'object' || obj === null) return;

    // Check required fields exist.
    for (const field of (schema.required || [])) {
      assert(field in obj, `${path}.${field} is required but missing`);
    }

    // Check each declared field's type.
    for (const [field, prop] of Object.entries(schema.properties || {})) {
      if (!(field in obj)) continue;
      validateField(obj[field], prop, `${path}.${field}`);
    }

    // Check for undeclared fields.
    const declared = new Set(Object.keys(schema.properties || {}));
    for (const key of Object.keys(obj)) {
      assert(declared.has(key), `${path}.${key} is not declared in the contract (undeclared field)`);
    }
  }
}

function validateField(value, prop, path) {
  if (prop.$ref) {
    const resolved = resolveRef(prop.$ref);
    if (resolved.type === 'string' && resolved.enum) {
      assert(typeof value === 'string', `${path} should be string, got ${typeof value}`);
      assert(resolved.enum.includes(value), `${path} = "${value}" not in enum [${resolved.enum}]`);
    } else {
      validateObject(value, resolved, path);
    }
    return;
  }

  if (prop.type === 'string') {
    assert(typeof value === 'string', `${path} should be string, got ${typeof value}`);
    if (prop.enum) {
      assert(prop.enum.includes(value), `${path} = "${value}" not in enum [${prop.enum}]`);
    }
  } else if (prop.type === 'number' || prop.type === 'integer') {
    assert(typeof value === 'number', `${path} should be number, got ${typeof value}`);
  } else if (prop.type === 'boolean') {
    assert(typeof value === 'boolean', `${path} should be boolean, got ${typeof value}`);
  } else if (prop.type === 'array') {
    assert(Array.isArray(value), `${path} should be array, got ${typeof value}`);
  }
}

function fetch(url) {
  return new Promise((resolve, reject) => {
    const req = http.get(url, res => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { reject(new Error('Invalid JSON from ' + url)); }
      });
    });
    req.on('error', reject);
    req.setTimeout(5000, () => { req.destroy(); reject(new Error('Timeout')); });
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
      res.on('end', () => {
        try { resolve(JSON.parse(d)); }
        catch (e) { reject(new Error('Invalid JSON')); }
      });
    });
    req.on('error', reject);
    req.setTimeout(10000, () => { req.destroy(); reject(new Error('Timeout')); });
    req.write(data);
    req.end();
  });
}

async function main() {
  console.log(`Contract conformance test against ${CORE}`);
  console.log('');

  // 1. GET /health
  console.log('--- GET /health ---');
  const health = await fetch(CORE + '/health');
  validateObject(health, defs.HealthResponse, 'health');

  // 2. GET /version
  console.log('--- GET /version ---');
  const version = await fetch(CORE + '/version');
  validateObject(version, defs.VersionResponse, 'version');

  // 3. POST /inspect (error case — nonexistent files)
  console.log('--- POST /inspect (error case) ---');
  const errResult = await postJson(CORE + '/inspect', {
    reference: '/nonexistent/a.stl',
    measured: '/nonexistent/b.stl',
    tolerance: 0.1,
  });
  validateObject(errResult, defs.InspectResponse, 'inspect_error');

  // 4. Mutation test: alter a field and verify detection.
  console.log('--- Mutation test: renamed field ---');
  const mutated = { ...errResult };
  mutated.verdict_RENAMED = mutated.verdict_label;
  delete mutated.verdict_label;
  const beforeFail = failed;
  validateObject(mutated, defs.InspectResponse, 'mutated');
  const mutationCaught = failed > beforeFail;
  if (mutationCaught) {
    console.log('  (Mutation correctly detected — this is expected)');
  } else {
    console.error('  CRITICAL: mutation was NOT detected!');
    failed++;
  }

  // Summary
  console.log('');
  console.log(`=== ${passed} passed, ${failed - (mutationCaught ? 2 : 0)} real failures ===`);
  // The mutation test adds 2 "expected" failures (missing field + undeclared field).
  const realFails = failed - (mutationCaught ? 2 : 0);
  process.exit(realFails > 0 ? 1 : 0);
}

main().catch(err => {
  console.error('FATAL:', err.message);
  process.exit(1);
});
