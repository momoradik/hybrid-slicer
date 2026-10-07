# API Contract — Single Source of Truth

## Rule

**`api/contract.json` is the single source of truth for the core ↔ frontend API.**

Both sides must change through the contract:

1. **To add/change a field:** edit `contract.json` first, then update the core's JSON serializer, then regenerate types.
2. **To regenerate frontend types:** `node api/generate-types.js > ui/drawgen/web/src/api/alignmesh-types.generated.ts`
3. **Never hand-edit** `alignmesh-types.generated.ts` — CI will catch it.

## Files

| File | Purpose |
|------|---------|
| `contract.json` | JSON Schema — the authoritative contract |
| `generate-types.js` | Generates TypeScript types from the contract |
| `test-conformance.js` | V-1: validates live responses against the schema |
| `test-fidelity.js` | V-2: numeric exactness, units, transform convention |
| `test-verdict-integrity.js` | V-3: verdict propagation, no PASS downgrade |
| `test-large-payload.js` | V-4: no dropped points, checksum integrity |
| `test-concurrency.js` | V-5: no cross-job leakage, cancellation safety |
| `test-capstone.js` | V-6: end-to-end equivalence + firewall audit |
| `ci-gate.js` | Runs all gates — used by CI |

## CI

```bash
bash ci/api_gate.sh [port]
```

This starts the core, runs all V-tests, stops the core. Any failure blocks the build.

## Units

Every physical field in `contract.json` has a `"unit"` annotation. The convention is:
- All distances/deviations: **mm**
- Percentages: **%**
- Counts: **count** or **index**
- Transform: 4×4 row-major, column-vector (`point' = T * point`), right-handed
