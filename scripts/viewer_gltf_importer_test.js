'use strict';
const assert = require('assert');
const imp = require('./viewer_gltf_importer.js');

function makeGeometryBuffer() {
  // 3 positions (float32 VEC3) + 3 uint16 indices.
  const bytes = new Uint8Array(42);
  const view = new DataView(bytes.buffer);
  const positions = [
    -1, -1, 0,
     1, -1, 0,
     0,  1, 0,
  ];
  let o = 0;
  for (const value of positions) { view.setFloat32(o, value, true); o += 4; }
  view.setUint16(36, 0, true);
  view.setUint16(38, 1, true);
  view.setUint16(40, 2, true);
  return bytes;
}

function makeDocument(uri) {
  return {
    asset: { version: '2.0', generator: 'Vulkax importer regression' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0, translation: [2, 3, 4], scale: [2, 1, 1] }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0 }, indices: 1, material: 0 }] }],
    materials: [{ pbrMetallicRoughness: { baseColorFactor: [0.2, 0.7, 1.0, 0.8] } }],
    buffers: [{ byteLength: 42, ...(uri === undefined ? {} : { uri }) }],
    bufferViews: [
      { buffer: 0, byteOffset: 0, byteLength: 36 },
      { buffer: 0, byteOffset: 36, byteLength: 6 },
    ],
    accessors: [
      { bufferView: 0, componentType: 5126, count: 3, type: 'VEC3' },
      { bufferView: 1, componentType: 5123, count: 3, type: 'SCALAR' },
    ],
  };
}

function toArrayBuffer(bytes) {
  return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
}

function dataUri(bytes) {
  return 'data:application/octet-stream;base64,' + Buffer.from(bytes).toString('base64');
}

function makeGLB(doc, binBytes) {
  let json = Buffer.from(JSON.stringify(doc), 'utf8');
  const jsonPad = (4 - (json.length % 4)) % 4;
  if (jsonPad) json = Buffer.concat([json, Buffer.alloc(jsonPad, 0x20)]);
  let bin = Buffer.from(binBytes);
  const binPad = (4 - (bin.length % 4)) % 4;
  if (binPad) bin = Buffer.concat([bin, Buffer.alloc(binPad)]);
  const total = 12 + 8 + json.length + 8 + bin.length;
  const out = Buffer.alloc(total);
  out.writeUInt32LE(0x46546c67, 0);
  out.writeUInt32LE(2, 4);
  out.writeUInt32LE(total, 8);
  let o = 12;
  out.writeUInt32LE(json.length, o); out.writeUInt32LE(0x4e4f534a, o + 4); o += 8;
  json.copy(out, o); o += json.length;
  out.writeUInt32LE(bin.length, o); out.writeUInt32LE(0x004e4942, o + 4); o += 8;
  bin.copy(out, o);
  return out.buffer.slice(out.byteOffset, out.byteOffset + out.byteLength);
}

function testEmbeddedGLTF() {
  const geometry = makeGeometryBuffer();
  const doc = makeDocument(dataUri(geometry));
  const input = new TextEncoder().encode(JSON.stringify(doc)).buffer;
  const result = imp.parseAsset('triangle.gltf', input, { maxPoints: 2000 });
  assert.equal(result.kind, 'gltf_surface_splats');
  assert.equal(result.format, 'gltf2');
  assert.equal(result.triangleCount, 1);
  assert.equal(result.primitiveCount, 1);
  assert.equal(result.meshCount, 1);
  assert.equal(result.sourceVertexCount, 3);
  assert(result.points.length > 3);
  assert(result.points.length <= 2000);
  assert(result.points.every(p => p.p.every(Number.isFinite)));
  assert(result.points.every(p => p.s > 0));
}

function testGLB() {
  const geometry = makeGeometryBuffer();
  const doc = makeDocument(undefined);
  const glb = makeGLB(doc, geometry);
  const result = imp.parseAsset('triangle.glb', glb, { maxPoints: 1500 });
  assert.equal(result.kind, 'glb_surface_splats');
  assert.equal(result.format, 'glb2');
  assert.equal(result.triangleCount, 1);
  assert(result.points.length > 3);
}

function testExternalSidecar() {
  const geometry = makeGeometryBuffer();
  const doc = makeDocument('mesh.bin');
  const input = new TextEncoder().encode(JSON.stringify(doc)).buffer;
  const result = imp.parseAsset('model.gltf', input, {
    maxPoints: 500,
    resources: { 'mesh.bin': toArrayBuffer(geometry) },
  });
  assert.equal(result.triangleCount, 1);
  assert(result.points.length > 3);
  assert.throws(
    () => imp.parseAsset('model.gltf', input, { maxPoints: 500 }),
    /sidecar not supplied/i,
  );
}

function testTriangleStrip() {
  // Four vertices + four uint16 indices.
  const bytes = new Uint8Array(56);
  const view = new DataView(bytes.buffer);
  const positions = [
    -1, -1, 0,
     1, -1, 0,
    -1,  1, 0,
     1,  1, 0,
  ];
  let o = 0;
  for (const value of positions) { view.setFloat32(o, value, true); o += 4; }
  [0, 1, 2, 3].forEach((v, i) => view.setUint16(48 + i * 2, v, true));
  const doc = {
    asset: { version: '2.0' }, scene: 0, scenes: [{ nodes: [0] }], nodes: [{ mesh: 0 }],
    meshes: [{ primitives: [{ attributes: { POSITION: 0 }, indices: 1, mode: 5 }] }],
    buffers: [{ byteLength: 56, uri: dataUri(bytes) }],
    bufferViews: [{ buffer: 0, byteOffset: 0, byteLength: 48 }, { buffer: 0, byteOffset: 48, byteLength: 8 }],
    accessors: [{ bufferView: 0, componentType: 5126, count: 4, type: 'VEC3' }, { bufferView: 1, componentType: 5123, count: 4, type: 'SCALAR' }],
  };
  const input = new TextEncoder().encode(JSON.stringify(doc)).buffer;
  const result = imp.parseAsset('strip.gltf', input, { maxPoints: 1000 });
  assert.equal(result.triangleCount, 2);
  assert(result.points.length > 4);
}

function testClearFailures() {
  assert.throws(() => imp.parseAsset('empty.glb', new ArrayBuffer(0)), /empty/i);
  assert.throws(() => imp.parseAsset('bad.gltf', new TextEncoder().encode('{ nope').buffer), /invalid gltf json/i);
  const unsupported = { asset: { version: '1.0' } };
  assert.throws(() => imp.parseAsset('old.gltf', new TextEncoder().encode(JSON.stringify(unsupported)).buffer), /only gltf 2/i);
  const compressed = makeDocument(dataUri(makeGeometryBuffer()));
  compressed.meshes[0].primitives[0].extensions = { KHR_draco_mesh_compression: { bufferView: 0, attributes: {} } };
  assert.throws(() => imp.parseAsset('draco.gltf', new TextEncoder().encode(JSON.stringify(compressed)).buffer), /draco\/meshopt/i);
}

testEmbeddedGLTF();
testGLB();
testExternalSidecar();
testTriangleStrip();
testClearFailures();
console.log('viewer_gltf_importer_test: passed');
