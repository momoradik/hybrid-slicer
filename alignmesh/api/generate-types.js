#!/usr/bin/env node
/**
 * Generate TypeScript types from the alignmesh API contract (contract.json).
 *
 * This is the ONLY source of frontend types for the API.
 * Hand-written types are forbidden — run this script to regenerate.
 *
 * Usage: node generate-types.js > ../ui/drawgen/web/src/api/alignmesh-types.generated.ts
 */

const fs = require('fs');
const path = require('path');

const contract = JSON.parse(
  fs.readFileSync(path.join(__dirname, 'contract.json'), 'utf8')
);

const defs = contract.$defs;
const lines = [];

lines.push('// AUTO-GENERATED from api/contract.json — DO NOT EDIT');
lines.push('// Regenerate: node api/generate-types.js > ui/drawgen/web/src/api/alignmesh-types.generated.ts');
lines.push('//');
lines.push('// Units for physical fields are documented inline from the contract.');
lines.push('');

// Emit all string enum types
for (const [name, schema] of Object.entries(defs)) {
  if (schema.type === 'string' && schema.enum) {
    lines.push(`/** ${schema.description || name} */`);
    lines.push(`export type ${name} = ${schema.enum.map(v => `'${v}'`).join(' | ')};`);
    lines.push('');
  }
}

// Helper: convert JSON Schema type to TS type
function tsType(prop) {
  if (prop.$ref) {
    const name = prop.$ref.split('/').pop();
    return name;
  }
  if (prop.type === 'string') return 'string';
  if (prop.type === 'number') return 'number';
  if (prop.type === 'integer') return 'number';
  if (prop.type === 'boolean') return 'boolean';
  if (prop.type === 'array' && prop.items) return `${tsType(prop.items)}[]`;
  return 'unknown';
}

// Emit interfaces for each $def (except Verdict which is a type alias)
for (const [name, schema] of Object.entries(defs)) {
  if (schema.type === 'string' && schema.enum) continue; // already emitted as type alias
  if (schema.type !== 'object') continue;

  lines.push(`/** ${schema.description || name} */`);
  lines.push(`export interface ${name} {`);

  const required = new Set(schema.required || []);
  for (const [field, prop] of Object.entries(schema.properties || {})) {
    const opt = required.has(field) ? '' : '?';
    const unit = prop.unit ? ` (${prop.unit})` : '';
    const desc = prop.description ? `${prop.description}${unit}` : unit;
    if (desc) lines.push(`  /** ${desc} */`);
    lines.push(`  ${field}${opt}: ${tsType(prop)};`);
  }

  lines.push('}');
  lines.push('');
}

process.stdout.write(lines.join('\n') + '\n');
