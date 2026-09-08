'use strict';
const assert = require('assert');
const exp = require('./viewer_splat_export.js');

function testBasicExport() {
  const text = exp.toPly([
    { p: [0, 0, 0], c: [1, 0.5, 0], s: 0.02, a: 0.9 },
    { p: [1, -2, 3], c: [0.1, 0.2, 0.3], s: 0.01, a: 0.4 },
  ]);
  assert(text.startsWith('ply\nformat ascii 1.0\n'));
  assert(text.includes('element vertex 2'));
  assert(text.includes('property float f_dc_0'));
  assert(text.includes('property float opacity'));
  assert(text.includes('property float scale_0'));
  assert(text.includes('property float rot_0'));
  assert(text.includes('property uint vulkax_id_namespace'));
  assert(text.includes('property uint vulkax_id_local'));
  const body = text.split('end_header\n')[1].trim().split('\n');
  assert.equal(body.length, 2);
  assert.equal(body[0].trim().split(/\s+/).length, 16);
  assert.equal(body[1].trim().split(/\s+/).length, 16);
  assert(body.every(line => line.split(/\s+/).every(v => Number.isFinite(Number(v)))));
}

function testStableIdsAndScale() {
  const text = exp.toPly([{ p: [0.25, 0.5, 0.75], c: [0.2, 0.3, 0.4], s: 0.005, a: 0.75 }], { namespaceId: 7 });
  const row = text.split('end_header\n')[1].trim().split(/\s+/).map(Number);
  assert.equal(row[14], 7);
  assert.equal(row[15], 1);
  assert(Math.abs(Math.exp(row[7]) - 0.005) < 1e-10);
  const alpha = 1 / (1 + Math.exp(-row[6]));
  assert(Math.abs(alpha - 0.75) < 1e-10);
}

function testFailures() {
  assert.throws(() => exp.toPly([]), /no splats/i);
  assert.throws(() => exp.toPly([{ p: [NaN, 0, 0], c: [1, 1, 1], s: 1, a: 1 }]), /invalid position/i);
}

testBasicExport();
testStableIdsAndScale();
testFailures();
console.log('viewer_splat_export_test: passed');
