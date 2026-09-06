'use strict';
const assert = require('assert');
const imp = require('./viewer_importers.js');

function asciiBuffer(text) {
  return new TextEncoder().encode(text).buffer;
}

function testAsciiPlyRgb() {
  const text = [
    'ply',
    'format ascii 1.0',
    'comment rgb fixture',
    'element vertex 3',
    'property float x',
    'property float y',
    'property float z',
    'property uchar red',
    'property uchar green',
    'property uchar blue',
    'end_header',
    '0 0 0 255 0 0',
    '1 0 0 0 255 0',
    '0 1 0 0 0 255',
    '',
  ].join('\n');
  const result = imp.parseAsset('rgb.ply', asciiBuffer(text));
  assert.equal(result.kind, 'ply_ascii');
  assert.equal(result.points.length, 3);
  assert(result.points[0].c[0] > 0.99);
  assert(result.points.every(p => p.p.every(Number.isFinite)));
  assert(result.points.every(p => p.s > 0));
}

function binaryPlyFixture() {
  const header = [
    'ply',
    'format binary_little_endian 1.0',
    'element vertex 2',
    'property float x',
    'property float y',
    'property float z',
    'property float f_dc_0',
    'property float f_dc_1',
    'property float f_dc_2',
    'property float opacity',
    'property float scale_0',
    'property float scale_1',
    'property float scale_2',
    'property uchar red',
    'property uchar green',
    'property uchar blue',
    'end_header',
    '',
  ].join('\n');
  const hb = new TextEncoder().encode(header);
  const stride = 10 * 4 + 3;
  const out = new Uint8Array(hb.length + stride * 2);
  out.set(hb, 0);
  const view = new DataView(out.buffer);
  let o = hb.length;
  const rows = [
    [0, 0, 0, 0.2, -0.1, 0.0, 3.0, -3.0, -3.1, -3.2, 255, 120, 20],
    [1, 2, 3, 0.0, 0.1, 0.2, 4.0, -2.8, -2.9, -3.0, 20, 140, 255],
  ];
  for (const row of rows) {
    for (let i = 0; i < 10; ++i) { view.setFloat32(o, row[i], true); o += 4; }
    for (let i = 10; i < 13; ++i) view.setUint8(o++, row[i]);
  }
  return out.buffer;
}

function testBinaryPly() {
  const result = imp.parseAsset('gaussians.ply', binaryPlyFixture());
  assert.equal(result.kind, 'ply_binary');
  assert.equal(result.format, 'binary_little_endian');
  assert.equal(result.points.length, 2);
  assert(result.points[0].a > 0.9);
  assert(result.points[0].s > 0);
  assert(result.points.every(p => p.p.every(Number.isFinite)));
}

function testObjSurfaceSampling() {
  const obj = [
    '# quad with vertex colors',
    'v -1 -1 0 1 0 0',
    'v  1 -1 0 0 1 0',
    'v  1  1 0 0 0 1',
    'v -1  1 0 1 1 1',
    'f 1 2 3 4',
    '',
  ].join('\n');
  const result = imp.parseAsset('quad.obj', asciiBuffer(obj), { maxPoints: 2000 });
  assert.equal(result.kind, 'obj_surface_splats');
  assert.equal(result.triangleCount, 2);
  assert(result.points.length > 4);
  assert(result.points.length <= 2000);
  assert(result.points.every(p => p.p.every(Number.isFinite)));
}

function testNegativeObjIndices() {
  const obj = [
    'v 0 0 0',
    'v 1 0 0',
    'v 0 1 0',
    'f -3 -2 -1',
  ].join('\n');
  const result = imp.parseAsset('negative.obj', asciiBuffer(obj), { maxPoints: 100 });
  assert.equal(result.triangleCount, 1);
  assert(result.points.length > 3);
}

function testDownsample() {
  const points = Array.from({ length: 1000 }, (_, i) => ({ id: i + 1, p: [i, 0, 0], c: [1, 1, 1], s: null, a: 1 }));
  const sampled = imp.deterministicDownsample(points, 100);
  assert.equal(sampled.points.length, 100);
  assert.equal(sampled.sourceCount, 1000);
  assert.equal(sampled.downsampled, true);
}

function testClearFailures() {
  assert.throws(() => imp.parseAsset('empty.ply', new ArrayBuffer(0)), /empty/i);
  assert.throws(() => imp.parseAsset('bad.ply', asciiBuffer('ply\nformat ascii 1.0\n')), /end_header/i);
  assert.throws(() => imp.parseAsset('bad.obj', asciiBuffer('f 1 2 3\n')), /no vertex|face index out of range/i);
  assert.throws(() => imp.parseAsset('thing.xyz', asciiBuffer('hello world')), /unsupported asset type/i);
}

testAsciiPlyRgb();
testBinaryPly();
testObjSurfaceSampling();
testNegativeObjIndices();
testDownsample();
testClearFailures();
console.log('viewer_importers_test: passed');
