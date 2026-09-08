/* Vulkax interactive-viewer asset importers.
 * Browser + Node compatible, dependency-free, and presentation-only.
 */
(function (global) {
  'use strict';

  const SH0 = 0.28209479177387814;
  const DEFAULT_MAX_POINTS = 250000;
  const MAX_FILE_BYTES = 512 * 1024 * 1024;

  const TYPE_INFO = {
    char: ['getInt8', 1], int8: ['getInt8', 1], int8_t: ['getInt8', 1],
    uchar: ['getUint8', 1], uint8: ['getUint8', 1], uint8_t: ['getUint8', 1],
    short: ['getInt16', 2], int16: ['getInt16', 2], int16_t: ['getInt16', 2],
    ushort: ['getUint16', 2], uint16: ['getUint16', 2], uint16_t: ['getUint16', 2],
    int: ['getInt32', 4], int32: ['getInt32', 4], int32_t: ['getInt32', 4],
    uint: ['getUint32', 4], uint32: ['getUint32', 4], uint32_t: ['getUint32', 4],
    float: ['getFloat32', 4], float32: ['getFloat32', 4],
    double: ['getFloat64', 8], float64: ['getFloat64', 8],
  };

  function clamp(x, lo = 0, hi = 1) {
    return Math.max(lo, Math.min(hi, x));
  }

  function finite(value, label) {
    const number = Number(value);
    if (!Number.isFinite(number)) throw new Error(label + ' is not finite');
    return number;
  }

  function sigmoid(x) {
    if (x >= 0) return 1 / (1 + Math.exp(-x));
    const e = Math.exp(x);
    return e / (1 + e);
  }

  function bytesOf(input) {
    if (input instanceof ArrayBuffer) return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    throw new Error('Importer expected an ArrayBuffer');
  }

  function decodeUtf8(bytes) {
    return new TextDecoder('utf-8', { fatal: false }).decode(bytes).replace(/^\uFEFF/, '');
  }

  function findHeaderEnd(bytes) {
    const needle = new TextEncoder().encode('end_header');
    const limit = Math.min(bytes.length, 1024 * 1024);
    outer: for (let i = 0; i <= limit - needle.length; ++i) {
      for (let j = 0; j < needle.length; ++j) if (bytes[i + j] !== needle[j]) continue outer;
      let p = i + needle.length;
      while (p < bytes.length && (bytes[p] === 32 || bytes[p] === 9 || bytes[p] === 13)) ++p;
      if (p < bytes.length && bytes[p] === 10) return p + 1;
      if (p === bytes.length) return p;
    }
    throw new Error('PLY header does not contain end_header within the first 1 MiB');
  }

  function parsePlyHeader(bytes) {
    const dataOffset = findHeaderEnd(bytes);
    const header = decodeUtf8(bytes.subarray(0, dataOffset));
    const lines = header.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    if (!lines.length || lines[0].toLowerCase() !== 'ply') throw new Error('Not a PLY file');

    let format = null;
    const elements = [];
    let current = null;
    for (const line of lines.slice(1)) {
      const parts = line.split(/\s+/);
      const tag = parts[0].toLowerCase();
      if (tag === 'format') {
        format = parts[1] && parts[1].toLowerCase();
      } else if (tag === 'element') {
        if (parts.length < 3) throw new Error('Malformed PLY element declaration');
        current = { name: parts[1], count: Number(parts[2]), properties: [] };
        if (!Number.isInteger(current.count) || current.count < 0) throw new Error('Invalid PLY element count');
        elements.push(current);
      } else if (tag === 'property' && current) {
        if (parts[1] === 'list') {
          current.properties.push({ list: true, countType: parts[2], itemType: parts[3], name: parts[4] });
        } else {
          current.properties.push({ list: false, type: parts[1], name: parts[2] });
        }
      }
    }
    if (!format) throw new Error('PLY format declaration missing');
    if (!['ascii', 'binary_little_endian', 'binary_big_endian'].includes(format)) {
      throw new Error('Unsupported PLY format: ' + format);
    }
    const vertexIndex = elements.findIndex(e => e.name === 'vertex');
    if (vertexIndex < 0) throw new Error('PLY has no vertex element');
    const vertex = elements[vertexIndex];
    if (!vertex.properties.length) throw new Error('PLY vertex element has no properties');
    if (vertex.properties.some(p => p.list)) throw new Error('PLY vertex list properties are not supported');
    for (const name of ['x', 'y', 'z']) if (!vertex.properties.some(p => p.name === name)) throw new Error('PLY vertex property ' + name + ' is required');
    return { format, elements, vertexIndex, vertex, dataOffset };
  }

  function readScalar(view, offset, type, littleEndian) {
    const info = TYPE_INFO[String(type).toLowerCase()];
    if (!info) throw new Error('Unsupported PLY scalar type: ' + type);
    const [method, size] = info;
    if (offset + size > view.byteLength) throw new Error('Truncated binary PLY payload');
    return [view[method](offset, littleEndian), offset + size];
  }

  function skipBinaryElement(view, offset, element, littleEndian) {
    for (let row = 0; row < element.count; ++row) {
      for (const prop of element.properties) {
        if (!prop.list) {
          [, offset] = readScalar(view, offset, prop.type, littleEndian);
        } else {
          let count;
          [count, offset] = readScalar(view, offset, prop.countType, littleEndian);
          if (!Number.isInteger(count) || count < 0) throw new Error('Invalid PLY list length');
          for (let i = 0; i < count; ++i) [, offset] = readScalar(view, offset, prop.itemType, littleEndian);
        }
      }
    }
    return offset;
  }

  function rgbFromRecord(record, propertyTypes) {
    const hasRgb = ['red', 'green', 'blue'].every(k => record[k] !== undefined);
    if (hasRgb) {
      const names = ['red', 'green', 'blue'];
      return names.map(name => {
        let value = finite(record[name], 'PLY ' + name);
        const type = String(propertyTypes[name] || '').toLowerCase();
        if (['uchar', 'uint8', 'uint8_t'].includes(type)) value /= 255;
        else if (['ushort', 'uint16', 'uint16_t'].includes(type)) value /= 65535;
        else if (value > 1) value /= value <= 255 ? 255 : 65535;
        return clamp(value);
      });
    }
    if (['f_dc_0', 'f_dc_1', 'f_dc_2'].every(k => record[k] !== undefined)) {
      return [0, 1, 2].map(i => clamp(0.5 + SH0 * finite(record['f_dc_' + i], 'PLY f_dc_' + i)));
    }
    return [0.38, 0.62, 0.98];
  }

  function scaleFromRecord(record) {
    if (['scale_0', 'scale_1', 'scale_2'].every(k => record[k] !== undefined)) {
      const mean = (finite(record.scale_0, 'PLY scale_0') + finite(record.scale_1, 'PLY scale_1') + finite(record.scale_2, 'PLY scale_2')) / 3;
      const value = Math.exp(mean);
      return Number.isFinite(value) && value > 0 ? value : null;
    }
    for (const key of ['radius', 'size', 'scale']) {
      if (record[key] !== undefined) {
        const value = Math.abs(finite(record[key], 'PLY ' + key));
        if (value > 0) return value;
      }
    }
    return null;
  }

  function opacityFromRecord(record) {
    if (record.opacity === undefined && record.alpha === undefined) return 0.9;
    let value = finite(record.opacity !== undefined ? record.opacity : record.alpha, 'PLY opacity');
    if (value < 0 || value > 1) value = sigmoid(value);
    return clamp(value);
  }

  function recordToPoint(record, propertyTypes, id) {
    return {
      id,
      p: [finite(record.x, 'PLY x'), finite(record.y, 'PLY y'), finite(record.z, 'PLY z')],
      c: rgbFromRecord(record, propertyTypes),
      s: scaleFromRecord(record),
      a: opacityFromRecord(record),
    };
  }

  function parseAsciiPly(bytes, header) {
    if (header.vertexIndex !== 0) throw new Error('ASCII PLY with elements before vertex is not supported');
    const body = decodeUtf8(bytes.subarray(header.dataOffset));
    const lines = body.split(/\r?\n/);
    const points = [];
    const propertyTypes = Object.fromEntries(header.vertex.properties.map(p => [p.name, p.type]));
    let lineIndex = 0;
    while (points.length < header.vertex.count && lineIndex < lines.length) {
      const line = lines[lineIndex++].trim();
      if (!line || line.startsWith('comment')) continue;
      const fields = line.split(/\s+/);
      if (fields.length < header.vertex.properties.length) throw new Error('Truncated ASCII PLY vertex row ' + (points.length + 1));
      const record = {};
      header.vertex.properties.forEach((p, i) => { record[p.name] = fields[i]; });
      points.push(recordToPoint(record, propertyTypes, points.length + 1));
    }
    if (points.length !== header.vertex.count) throw new Error('ASCII PLY contains fewer vertices than declared');
    return points;
  }

  function parseBinaryPly(bytes, header) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const little = header.format === 'binary_little_endian';
    let offset = header.dataOffset;
    for (let e = 0; e < header.vertexIndex; ++e) offset = skipBinaryElement(view, offset, header.elements[e], little);
    const propertyTypes = Object.fromEntries(header.vertex.properties.map(p => [p.name, p.type]));
    const points = new Array(header.vertex.count);
    for (let row = 0; row < header.vertex.count; ++row) {
      const record = {};
      for (const prop of header.vertex.properties) {
        let value;
        [value, offset] = readScalar(view, offset, prop.type, little);
        record[prop.name] = value;
      }
      points[row] = recordToPoint(record, propertyTypes, row + 1);
    }
    return points;
  }

  function deterministicDownsample(points, maxPoints) {
    const limit = Math.max(1, Math.floor(maxPoints || DEFAULT_MAX_POINTS));
    if (points.length <= limit) return { points, sourceCount: points.length, downsampled: false };
    const out = new Array(limit);
    const step = points.length / limit;
    for (let i = 0; i < limit; ++i) out[i] = points[Math.min(points.length - 1, Math.floor((i + 0.5) * step))];
    return { points: out, sourceCount: points.length, downsampled: true };
  }

  function normalizePoints(points) {
    if (!points.length) throw new Error('Imported asset contains no points');
    const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity];
    for (const item of points) {
      item.p.forEach((v, i) => { mn[i] = Math.min(mn[i], v); mx[i] = Math.max(mx[i], v); });
    }
    const center = mn.map((v, i) => (v + mx[i]) * 0.5);
    const span = Math.max(mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 1e-9);
    const fallbackScale = clamp(0.75 / Math.sqrt(points.length), 0.0015, 0.024);
    for (const item of points) {
      item.p = item.p.map((v, i) => (v - center[i]) / span);
      item.s = item.s === null || item.s === undefined ? fallbackScale : clamp(Math.abs(item.s) / span, 0.00025, 0.08);
      item.a = clamp(item.a === undefined ? 0.9 : item.a);
    }
    return points;
  }

  function parsePLY(input, options = {}) {
    const bytes = bytesOf(input);
    const header = parsePlyHeader(bytes);
    let points = header.format === 'ascii' ? parseAsciiPly(bytes, header) : parseBinaryPly(bytes, header);
    const sampled = deterministicDownsample(points, options.maxPoints || DEFAULT_MAX_POINTS);
    points = normalizePoints(sampled.points);
    return {
      points,
      kind: header.format === 'ascii' ? 'ply_ascii' : 'ply_binary',
      sourceCount: sampled.sourceCount,
      downsampled: sampled.downsampled,
      format: header.format,
    };
  }

  function parseObjVertex(parts) {
    if (parts.length < 4) throw new Error('Malformed OBJ vertex line');
    const p = [finite(parts[1], 'OBJ x'), finite(parts[2], 'OBJ y'), finite(parts[3], 'OBJ z')];
    let c = [0.42, 0.66, 1.0];
    if (parts.length >= 7) {
      c = [parts[4], parts[5], parts[6]].map((v, i) => {
        let n = finite(v, 'OBJ color ' + i);
        if (n > 1) n /= 255;
        return clamp(n);
      });
    }
    return { p, c };
  }

  function triangleArea(a, b, c) {
    const ab = b.map((v, i) => v - a[i]), ac = c.map((v, i) => v - a[i]);
    const cross = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]];
    return 0.5 * Math.hypot(cross[0], cross[1], cross[2]);
  }

  function objIndex(token, vertexCount) {
    const head = token.split('/')[0];
    const raw = Number(head);
    if (!Number.isInteger(raw) || raw === 0) throw new Error('Invalid OBJ face vertex index: ' + token);
    const index = raw > 0 ? raw - 1 : vertexCount + raw;
    if (index < 0 || index >= vertexCount) throw new Error('OBJ face index out of range: ' + raw);
    return index;
  }

  function mix3(a, b, c, wa, wb, wc) {
    return [0, 1, 2].map(i => a[i] * wa + b[i] * wb + c[i] * wc);
  }

  function surfaceSampleObj(vertices, triangles, maxPoints) {
    if (!triangles.length) {
      return vertices.map((v, i) => ({ id: i + 1, p: v.p.slice(), c: v.c.slice(), s: null, a: 0.9 }));
    }
    const areas = triangles.map(t => triangleArea(vertices[t[0]].p, vertices[t[1]].p, vertices[t[2]].p));
    const totalArea = areas.reduce((a, b) => a + b, 0);
    if (!(totalArea > 0)) return vertices.map((v, i) => ({ id: i + 1, p: v.p.slice(), c: v.c.slice(), s: null, a: 0.9 }));

    const target = Math.min(maxPoints, Math.max(vertices.length, Math.min(60000, Math.max(12000, vertices.length * 3))));
    const out = [];
    let serial = 0;
    for (let ti = 0; ti < triangles.length && out.length < target; ++ti) {
      const t = triangles[ti], a = vertices[t[0]], b = vertices[t[1]], c = vertices[t[2]];
      const count = Math.max(1, Math.round(target * areas[ti] / totalArea));
      for (let j = 0; j < count && out.length < target; ++j) {
        // Deterministic low-discrepancy barycentric sequence.
        const u = (j + 0.5) / count;
        const v = ((serial * 0.6180339887498949) % 1);
        const su = Math.sqrt(u);
        const wa = 1 - su, wb = su * (1 - v), wc = su * v;
        out.push({ id: out.length + 1, p: mix3(a.p, b.p, c.p, wa, wb, wc), c: mix3(a.c, b.c, c.c, wa, wb, wc), s: null, a: 0.9 });
        ++serial;
      }
    }
    return out.length ? out : vertices.map((v, i) => ({ id: i + 1, p: v.p.slice(), c: v.c.slice(), s: null, a: 0.9 }));
  }

  function parseOBJ(input, options = {}) {
    const bytes = bytesOf(input);
    const text = decodeUtf8(bytes);
    const vertices = [], triangles = [];
    let lineNo = 0;
    for (const raw of text.split(/\r?\n/)) {
      ++lineNo;
      const line = raw.trim();
      if (!line || line.startsWith('#')) continue;
      const parts = line.split(/\s+/);
      if (parts[0] === 'v') {
        try { vertices.push(parseObjVertex(parts)); }
        catch (error) { throw new Error('OBJ line ' + lineNo + ': ' + error.message); }
      } else if (parts[0] === 'f') {
        if (parts.length < 4) continue;
        const face = parts.slice(1).map(token => objIndex(token, vertices.length));
        for (let i = 1; i + 1 < face.length; ++i) triangles.push([face[0], face[i], face[i + 1]]);
      }
    }
    if (!vertices.length) throw new Error('OBJ has no vertex records');
    const maxPoints = Math.max(1, Math.floor(options.maxPoints || DEFAULT_MAX_POINTS));
    let points = surfaceSampleObj(vertices, triangles, maxPoints);
    const sampled = deterministicDownsample(points, maxPoints);
    points = normalizePoints(sampled.points);
    return {
      points,
      kind: triangles.length ? 'obj_surface_splats' : 'obj_vertices',
      sourceCount: vertices.length,
      triangleCount: triangles.length,
      downsampled: sampled.downsampled,
      format: 'obj',
    };
  }

  function sniffKind(name, bytes) {
    const lower = String(name || '').toLowerCase();
    if (lower.endsWith('.ply')) return 'ply';
    if (lower.endsWith('.obj')) return 'obj';
    const prefix = decodeUtf8(bytes.subarray(0, Math.min(bytes.length, 4096))).trimStart();
    if (/^ply(?:\r?\n|\s)/i.test(prefix)) return 'ply';
    if (/^(?:#.*\n)*\s*(?:v|o|g|mtllib)\s/im.test(prefix)) return 'obj';
    throw new Error('Unsupported asset type. Use .ply or .obj');
  }

  function parseAsset(name, input, options = {}) {
    const bytes = bytesOf(input);
    if (bytes.byteLength === 0) throw new Error('Selected file is empty');
    if (bytes.byteLength > (options.maxFileBytes || MAX_FILE_BYTES)) {
      throw new Error('File is too large for the browser importer (' + (bytes.byteLength / (1024 * 1024)).toFixed(1) + ' MiB)');
    }
    const kind = sniffKind(name, bytes);
    return kind === 'ply' ? parsePLY(bytes, options) : parseOBJ(bytes, options);
  }

  global.VulkaxImport = {
    parseAsset,
    parsePLY,
    parseOBJ,
    parsePlyHeader,
    normalizePoints,
    deterministicDownsample,
    DEFAULT_MAX_POINTS,
    MAX_FILE_BYTES,
  };
  if (typeof module !== 'undefined' && module.exports) module.exports = global.VulkaxImport;
})(typeof globalThis !== 'undefined' ? globalThis : this);
