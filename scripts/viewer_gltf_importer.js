/* Vulkax interactive-viewer glTF / GLB importer.
 * Browser + Node compatible, dependency-free, and presentation-only.
 *
 * Supported:
 *   - glTF 2.0 JSON with embedded data-URI buffers;
 *   - glTF 2.0 JSON with external .bin buffers supplied through options.resources;
 *   - GLB 2.0 embedded JSON/BIN;
 *   - POSITION, COLOR_0, indexed/non-indexed primitives;
 *   - TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN and POINTS;
 *   - node matrix/TRS transforms and active-scene traversal;
 *   - material baseColorFactor (textures are intentionally not sampled yet).
 */
(function (global) {
  'use strict';

  const DEFAULT_MAX_POINTS = 250000;
  const MAX_FILE_BYTES = 512 * 1024 * 1024;

  const COMPONENTS = {
    5120: { method: 'getInt8', size: 1, signed: true, max: 127, min: -128 },
    5121: { method: 'getUint8', size: 1, signed: false, max: 255, min: 0 },
    5122: { method: 'getInt16', size: 2, signed: true, max: 32767, min: -32768 },
    5123: { method: 'getUint16', size: 2, signed: false, max: 65535, min: 0 },
    5125: { method: 'getUint32', size: 4, signed: false, max: 4294967295, min: 0 },
    5126: { method: 'getFloat32', size: 4, float: true },
  };
  const TYPE_WIDTH = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT2: 4, MAT3: 9, MAT4: 16 };

  function clamp(x, lo = 0, hi = 1) { return Math.max(lo, Math.min(hi, x)); }
  function bytesOf(input) {
    if (input instanceof ArrayBuffer) return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    throw new Error('glTF importer expected an ArrayBuffer');
  }
  function decodeUtf8(bytes) { return new TextDecoder('utf-8', { fatal: false }).decode(bytes).replace(/^\uFEFF/, ''); }
  function basename(path) {
    const clean = decodeURIComponent(String(path || '').split('?')[0].split('#')[0]).replace(/\\/g, '/');
    return clean.slice(clean.lastIndexOf('/') + 1);
  }
  function resourceLookup(resources, uri) {
    if (!resources) return null;
    const keys = [uri, decodeURIComponent(uri), basename(uri)];
    for (const key of keys) {
      if (resources instanceof Map && resources.has(key)) return resources.get(key);
      if (!(resources instanceof Map) && Object.prototype.hasOwnProperty.call(resources, key)) return resources[key];
    }
    return null;
  }
  function base64Bytes(text) {
    if (typeof Buffer !== 'undefined' && typeof Buffer.from === 'function') {
      const b = Buffer.from(text, 'base64');
      return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
    }
    if (typeof atob !== 'function') throw new Error('This runtime cannot decode base64 glTF buffers');
    const binary = atob(text), out = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; ++i) out[i] = binary.charCodeAt(i);
    return out;
  }
  function dataUriBytes(uri) {
    const comma = uri.indexOf(',');
    if (comma < 0) throw new Error('Malformed glTF data URI');
    const meta = uri.slice(5, comma), body = uri.slice(comma + 1);
    if (/;base64/i.test(meta)) return base64Bytes(body);
    return new TextEncoder().encode(decodeURIComponent(body));
  }

  function identity() { return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]; }
  function mul4(a, b) {
    const o = new Array(16).fill(0);
    for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) {
      for (let k = 0; k < 4; ++k) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    }
    return o;
  }
  function trsMatrix(node) {
    if (node.matrix) {
      if (!Array.isArray(node.matrix) || node.matrix.length !== 16) throw new Error('glTF node matrix must contain 16 values');
      return node.matrix.map(Number);
    }
    const t = node.translation || [0, 0, 0], q = node.rotation || [0, 0, 0, 1], s = node.scale || [1, 1, 1];
    const x = Number(q[0]), y = Number(q[1]), z = Number(q[2]), w = Number(q[3]);
    const xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
    return [
      (1 - 2 * (yy + zz)) * s[0], (2 * (xy + wz)) * s[0], (2 * (xz - wy)) * s[0], 0,
      (2 * (xy - wz)) * s[1], (1 - 2 * (xx + zz)) * s[1], (2 * (yz + wx)) * s[1], 0,
      (2 * (xz + wy)) * s[2], (2 * (yz - wx)) * s[2], (1 - 2 * (xx + yy)) * s[2], 0,
      Number(t[0]), Number(t[1]), Number(t[2]), 1,
    ];
  }
  function transformPoint(m, p) {
    const x = p[0], y = p[1], z = p[2];
    return [m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]];
  }

  function normalizedComponent(value, componentType) {
    const info = COMPONENTS[componentType];
    if (!info || info.float) return value;
    if (info.signed) return Math.max(-1, value / info.max);
    return value / info.max;
  }
  function readAccessor(doc, buffers, index) {
    const accessor = doc.accessors && doc.accessors[index];
    if (!accessor) throw new Error('glTF accessor ' + index + ' is missing');
    if (accessor.sparse) throw new Error('Sparse glTF accessors are not supported yet');
    const component = COMPONENTS[accessor.componentType];
    if (!component) throw new Error('Unsupported glTF accessor componentType ' + accessor.componentType);
    const width = TYPE_WIDTH[accessor.type];
    if (!width) throw new Error('Unsupported glTF accessor type ' + accessor.type);
    const count = Number(accessor.count || 0);
    if (!Number.isInteger(count) || count < 0) throw new Error('Invalid glTF accessor count');

    if (accessor.bufferView === undefined) return Array.from({ length: count }, () => new Array(width).fill(0));
    const viewDef = doc.bufferViews && doc.bufferViews[accessor.bufferView];
    if (!viewDef) throw new Error('glTF bufferView ' + accessor.bufferView + ' is missing');
    const bytes = buffers[viewDef.buffer];
    if (!bytes) throw new Error('glTF buffer ' + viewDef.buffer + ' is unavailable');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const packed = component.size * width;
    const stride = Number(viewDef.byteStride || packed);
    if (stride < packed) throw new Error('glTF bufferView byteStride is smaller than accessor element size');
    const start = Number(viewDef.byteOffset || 0) + Number(accessor.byteOffset || 0);
    const out = new Array(count);
    for (let i = 0; i < count; ++i) {
      const row = new Array(width), base = start + i * stride;
      for (let c = 0; c < width; ++c) {
        const offset = base + c * component.size;
        if (offset + component.size > view.byteLength) throw new Error('Truncated glTF accessor ' + index);
        let value = view[component.method](offset, true);
        if (accessor.normalized) value = normalizedComponent(value, accessor.componentType);
        row[c] = value;
      }
      out[i] = row;
    }
    return out;
  }

  function resolveBuffers(doc, glbBin, resources) {
    const defs = doc.buffers || [];
    return defs.map((def, i) => {
      let bytes = null;
      if (def.uri) {
        if (String(def.uri).startsWith('data:')) bytes = dataUriBytes(String(def.uri));
        else {
          const resource = resourceLookup(resources, String(def.uri));
          if (!resource) throw new Error('glTF buffer sidecar not supplied: ' + def.uri + '. Select/drop the .gltf and its .bin file together.');
          bytes = bytesOf(resource);
        }
      } else if (i === 0 && glbBin) bytes = glbBin;
      else throw new Error('glTF buffer ' + i + ' has no URI and no GLB BIN chunk');
      if (def.byteLength !== undefined && bytes.byteLength < Number(def.byteLength)) throw new Error('glTF buffer ' + i + ' is shorter than declared byteLength');
      return bytes;
    });
  }

  function triangleArea(a, b, c) {
    const ab = b.map((v, i) => v - a[i]), ac = c.map((v, i) => v - a[i]);
    const cr = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]];
    return 0.5 * Math.hypot(cr[0], cr[1], cr[2]);
  }
  function mix3(a, b, c, wa, wb, wc) { return [0, 1, 2].map(i => a[i] * wa + b[i] * wb + c[i] * wc); }
  function normalizePoints(points) {
    if (!points.length) throw new Error('glTF contains no displayable points');
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const item of points) item.p.forEach((v, i) => { mn[i] = Math.min(mn[i], v); mx[i] = Math.max(mx[i], v); });
    const center = mn.map((v, i) => (v + mx[i]) * 0.5), span = Math.max(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 1e-9);
    const fallbackScale = clamp(0.78 / Math.sqrt(points.length), 0.0013, 0.022);
    for (const item of points) {
      item.p = item.p.map((v, i) => (v - center[i]) / span);
      item.s = fallbackScale;
      item.a = clamp(item.a === undefined ? 0.92 : item.a);
    }
    return points;
  }
  function surfaceSample(vertices, triangles, maxPoints) {
    if (!triangles.length) return vertices.map((v, i) => ({ id: i + 1, p: v.p.slice(), c: v.c.slice(), s: null, a: v.a }));
    const areas = triangles.map(t => triangleArea(vertices[t[0]].p, vertices[t[1]].p, vertices[t[2]].p));
    const totalArea = areas.reduce((a, b) => a + b, 0);
    if (!(totalArea > 0)) return vertices.map((v, i) => ({ id: i + 1, p: v.p.slice(), c: v.c.slice(), s: null, a: v.a }));
    const target = Math.min(maxPoints, Math.max(vertices.length, Math.min(90000, Math.max(16000, vertices.length * 4))));
    const out = [];
    let serial = 0;
    for (let ti = 0; ti < triangles.length && out.length < target; ++ti) {
      const t = triangles[ti], a = vertices[t[0]], b = vertices[t[1]], c = vertices[t[2]];
      const count = Math.max(1, Math.round(target * areas[ti] / totalArea));
      for (let j = 0; j < count && out.length < target; ++j) {
        const u = (j + 0.5) / count, v = (serial * 0.6180339887498949) % 1, su = Math.sqrt(u);
        const wa = 1 - su, wb = su * (1 - v), wc = su * v;
        out.push({ id: out.length + 1, p: mix3(a.p, b.p, c.p, wa, wb, wc), c: mix3(a.c, b.c, c.c, wa, wb, wc), s: null, a: a.a * wa + b.a * wb + c.a * wc });
        ++serial;
      }
    }
    return out;
  }
  function deterministicDownsample(points, maxPoints) {
    if (points.length <= maxPoints) return { points, downsampled: false };
    const out = new Array(maxPoints), step = points.length / maxPoints;
    for (let i = 0; i < maxPoints; ++i) out[i] = points[Math.min(points.length - 1, Math.floor((i + 0.5) * step))];
    return { points: out, downsampled: true };
  }

  function primitiveTriangles(indices, mode) {
    const out = [];
    if (mode === 0) return out; // POINTS
    if (mode === 4 || mode === undefined) {
      for (let i = 0; i + 2 < indices.length; i += 3) out.push([indices[i], indices[i + 1], indices[i + 2]]);
    } else if (mode === 5) {
      for (let i = 0; i + 2 < indices.length; ++i) {
        const tri = i % 2 === 0 ? [indices[i], indices[i + 1], indices[i + 2]] : [indices[i + 1], indices[i], indices[i + 2]];
        if (tri[0] !== tri[1] && tri[1] !== tri[2] && tri[0] !== tri[2]) out.push(tri);
      }
    } else if (mode === 6) {
      for (let i = 1; i + 1 < indices.length; ++i) out.push([indices[0], indices[i], indices[i + 1]]);
    } else {
      throw new Error('Unsupported glTF primitive mode ' + mode + ' (supported: POINTS, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN)');
    }
    return out;
  }
  function materialColor(doc, materialIndex) {
    const material = materialIndex === undefined ? null : doc.materials && doc.materials[materialIndex];
    const factor = material && material.pbrMetallicRoughness && material.pbrMetallicRoughness.baseColorFactor;
    const f = Array.isArray(factor) && factor.length >= 4 ? factor.map(Number) : [0.42, 0.66, 1.0, 1.0];
    return { c: [clamp(f[0]), clamp(f[1]), clamp(f[2])], a: clamp(f[3]) };
  }

  function buildMesh(doc, buffers, maxPoints) {
    const vertices = [], triangles = [];
    let primitiveCount = 0, meshCount = 0;
    const nodes = doc.nodes || [], meshes = doc.meshes || [];
    const childSet = new Set();
    nodes.forEach(n => (n.children || []).forEach(c => childSet.add(c)));
    let roots = [];
    if (doc.scenes && doc.scenes.length) {
      const sceneIndex = doc.scene === undefined ? 0 : Number(doc.scene);
      const active = doc.scenes[sceneIndex];
      if (!active) throw new Error('glTF active scene ' + sceneIndex + ' is missing');
      roots = (active.nodes || []).slice();
    } else roots = nodes.map((_, i) => i).filter(i => !childSet.has(i));
    if (!roots.length && nodes.length) roots = [0];

    const visitedMeshNodes = new Set();
    function visit(nodeIndex, parentMatrix) {
      const node = nodes[nodeIndex];
      if (!node) throw new Error('glTF node ' + nodeIndex + ' is missing');
      const world = mul4(parentMatrix, trsMatrix(node));
      if (node.mesh !== undefined) {
        const mesh = meshes[node.mesh];
        if (!mesh) throw new Error('glTF mesh ' + node.mesh + ' is missing');
        ++meshCount;
        (mesh.primitives || []).forEach((primitive, primitiveIndex) => {
          if (primitive.extensions && (primitive.extensions.KHR_draco_mesh_compression || primitive.extensions.EXT_meshopt_compression)) {
            throw new Error('Compressed glTF primitive requires Draco/meshopt decoding, which the dependency-free viewer does not support yet');
          }
          if (!primitive.attributes || primitive.attributes.POSITION === undefined) return;
          ++primitiveCount;
          const positions = readAccessor(doc, buffers, primitive.attributes.POSITION);
          if (!positions.length || positions[0].length < 3) throw new Error('glTF POSITION accessor must be VEC3');
          const colors = primitive.attributes.COLOR_0 === undefined ? null : readAccessor(doc, buffers, primitive.attributes.COLOR_0);
          const material = materialColor(doc, primitive.material);
          const base = vertices.length;
          for (let i = 0; i < positions.length; ++i) {
            const raw = positions[i], transformed = transformPoint(world, raw);
            let c = material.c.slice(), a = material.a;
            if (colors && colors[i]) {
              c = [0, 1, 2].map(k => clamp((colors[i][k] === undefined ? 1 : colors[i][k]) * material.c[k]));
              if (colors[i][3] !== undefined) a = clamp(a * colors[i][3]);
            }
            vertices.push({ p: transformed, c, a });
          }
          let localIndices;
          if (primitive.indices !== undefined) {
            const indexRows = readAccessor(doc, buffers, primitive.indices);
            localIndices = indexRows.map(row => {
              const value = Number(row[0]);
              if (!Number.isInteger(value) || value < 0 || value >= positions.length) throw new Error('glTF primitive index out of range');
              return value;
            });
          } else localIndices = Array.from({ length: positions.length }, (_, i) => i);
          for (const tri of primitiveTriangles(localIndices, primitive.mode)) triangles.push(tri.map(i => base + i));
        });
        visitedMeshNodes.add(nodeIndex);
      }
      for (const child of node.children || []) visit(child, world);
    }
    for (const root of roots) visit(root, identity());

    // Some malformed/exporter-minimal files contain meshes without nodes. Keep them
    // usable by rendering each unreferenced mesh once at identity.
    if (!nodes.length && meshes.length) {
      const synthetic = { nodes: meshes.map((_, i) => ({ mesh: i })) };
      const originalNodes = doc.nodes;
      doc.nodes = synthetic.nodes;
      try {
        for (let i = 0; i < synthetic.nodes.length; ++i) visit(i, identity());
      } finally { doc.nodes = originalNodes; }
    }

    if (!vertices.length) throw new Error('glTF contains no POSITION mesh data in the active scene');
    let points = surfaceSample(vertices, triangles, maxPoints);
    const sampled = deterministicDownsample(points, maxPoints);
    points = normalizePoints(sampled.points);
    return { points, sourceVertexCount: vertices.length, triangleCount: triangles.length, primitiveCount, meshCount, downsampled: sampled.downsampled };
  }

  function parseDocument(doc, glbBin, options) {
    if (!doc || !doc.asset || String(doc.asset.version || '').split('.')[0] !== '2') throw new Error('Only glTF 2.x is supported');
    const buffers = resolveBuffers(doc, glbBin, options.resources);
    return buildMesh(doc, buffers, Math.max(1, Math.floor(options.maxPoints || DEFAULT_MAX_POINTS)));
  }
  function parseGLTF(input, options = {}) {
    const bytes = bytesOf(input);
    const text = decodeUtf8(bytes).trim();
    let doc;
    try { doc = JSON.parse(text); } catch (error) { throw new Error('Invalid glTF JSON: ' + error.message); }
    const result = parseDocument(doc, null, options);
    return { ...result, kind: 'gltf_surface_splats', sourceCount: result.sourceVertexCount, format: 'gltf2' };
  }
  function parseGLB(input, options = {}) {
    const bytes = bytesOf(input), view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (bytes.byteLength < 20 || view.getUint32(0, true) !== 0x46546c67) throw new Error('Not a GLB file');
    const version = view.getUint32(4, true), declaredLength = view.getUint32(8, true);
    if (version !== 2) throw new Error('Only GLB version 2 is supported');
    if (declaredLength > bytes.byteLength) throw new Error('Truncated GLB container');
    let offset = 12, jsonChunk = null, binChunk = null;
    while (offset + 8 <= declaredLength) {
      const length = view.getUint32(offset, true), type = view.getUint32(offset + 4, true); offset += 8;
      if (offset + length > declaredLength) throw new Error('Truncated GLB chunk');
      const chunk = bytes.subarray(offset, offset + length);
      if (type === 0x4e4f534a && !jsonChunk) jsonChunk = chunk;
      else if (type === 0x004e4942 && !binChunk) binChunk = chunk;
      offset += length;
    }
    if (!jsonChunk) throw new Error('GLB JSON chunk missing');
    let doc;
    try { doc = JSON.parse(decodeUtf8(jsonChunk).replace(/\u0000+$/g, '').trim()); } catch (error) { throw new Error('Invalid GLB JSON chunk: ' + error.message); }
    const result = parseDocument(doc, binChunk, options);
    return { ...result, kind: 'glb_surface_splats', sourceCount: result.sourceVertexCount, format: 'glb2' };
  }
  function parseAsset(name, input, options = {}) {
    const bytes = bytesOf(input);
    if (!bytes.byteLength) throw new Error('Selected glTF/GLB file is empty');
    if (bytes.byteLength > (options.maxFileBytes || MAX_FILE_BYTES)) throw new Error('File is too large for browser glTF import');
    const lower = String(name || '').toLowerCase();
    if (lower.endsWith('.glb') || (bytes.byteLength >= 4 && new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(0, true) === 0x46546c67)) return parseGLB(bytes, options);
    if (lower.endsWith('.gltf') || decodeUtf8(bytes.subarray(0, Math.min(bytes.length, 128))).trimStart().startsWith('{')) return parseGLTF(bytes, options);
    throw new Error('Unsupported glTF asset type. Use .glb or .gltf');
  }

  global.VulkaxGltfImport = { parseAsset, parseGLTF, parseGLB, readAccessor, DEFAULT_MAX_POINTS, MAX_FILE_BYTES };
  if (typeof module !== 'undefined' && module.exports) module.exports = global.VulkaxGltfImport;
})(typeof globalThis !== 'undefined' ? globalThis : this);
