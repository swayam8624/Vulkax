'use strict';
const assert = require('assert');
const imp = require('./viewer_image_importer.js');

function makeRgba(width, height) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; ++y) for (let x = 0; x < width; ++x) {
    const i = (y * width + x) * 4;
    rgba[i] = Math.round(255 * x / Math.max(1, width - 1));
    rgba[i + 1] = Math.round(255 * y / Math.max(1, height - 1));
    rgba[i + 2] = 128;
    rgba[i + 3] = (x === 0 && y === 0) ? 0 : 255;
  }
  return rgba;
}

function testBasicCard() {
  const result = imp.pixelsToSplats(makeRgba(4, 2), 4, 2, { maxPoints: 100 });
  assert.equal(result.kind, 'image_splat_card');
  assert.equal(result.format, 'image_2.5d');
  assert.equal(result.pixelWidth, 4);
  assert.equal(result.pixelHeight, 2);
  assert(result.points.length > 0);
  assert(result.points.every(p => p.p[2] === 0));
  assert(result.points.every(p => p.c.every(v => v >= 0 && v <= 1)));
  assert(result.points.every(p => p.s > 0));
  assert(result.presentationOnly);
}

function testBudgetDownsample() {
  const result = imp.pixelsToSplats(makeRgba(400, 300), 400, 300, { maxPoints: 5000 });
  assert(result.points.length <= 5000);
  assert(result.stride > 1);
  assert(result.downsampled);
}

function testAspectRatio() {
  const wide = imp.pixelsToSplats(makeRgba(8, 2), 8, 2, { maxPoints: 1000 });
  const xs = wide.points.map(p => p.p[0]), ys = wide.points.map(p => p.p[1]);
  const xSpan = Math.max(...xs) - Math.min(...xs);
  const ySpan = Math.max(...ys) - Math.min(...ys);
  assert(xSpan > ySpan * 2);
}

function testAlpha() {
  const rgba = new Uint8ClampedArray([
    255, 0, 0, 0,
    0, 255, 0, 255,
  ]);
  const result = imp.pixelsToSplats(rgba, 2, 1, { maxPoints: 10, alphaThreshold: 0.1 });
  assert.equal(result.points.length, 1);
  assert(result.points[0].c[1] > 0.99);
}

function testFailures() {
  assert.throws(() => imp.pixelsToSplats(new Uint8Array(1), 0, 1), /dimensions/i);
  assert.throws(() => imp.pixelsToSplats(new Uint8Array(3), 1, 1), /truncated/i);
  assert.throws(() => imp.pixelsToSplats(new Uint8Array([0, 0, 0, 0]), 1, 1), /no visible pixels/i);
}

testBasicCard();
testBudgetDownsample();
testAspectRatio();
testAlpha();
testFailures();
console.log('viewer_image_importer_test: passed');
