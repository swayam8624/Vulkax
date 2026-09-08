/* Vulkax interactive-viewer splat exporter.
 * Converts viewer splats into an ASCII 3DGS/Vulkax-compatible PLY.
 * Exported coordinates are the viewer/import-normalized coordinates, so this is
 * presentation/authoring output rather than captured scientific evidence.
 */
(function (global) {
  'use strict';

  const SH0 = 0.28209479177387814;

  function clamp(x, lo, hi) { return Math.max(lo, Math.min(hi, x)); }
  function finite(x, fallback) { const v = Number(x); return Number.isFinite(v) ? v : fallback; }
  function logit(alpha) {
    const a = clamp(finite(alpha, 0.9), 1e-6, 1 - 1e-6);
    return Math.log(a / (1 - a));
  }
  function shDc(color) { return (clamp(finite(color, 0.5), 0, 1) - 0.5) / SH0; }

  function toPly(points, options = {}) {
    if (!Array.isArray(points) || !points.length) throw new Error('No splats available to export');
    const namespaceId = Math.max(1, Math.floor(finite(options.namespaceId, 1)));
    const lines = [
      'ply',
      'format ascii 1.0',
      'comment Vulkax interactive viewer presentation splat export',
      'comment coordinates may be normalized viewer coordinates; not captured scientific evidence',
      'element vertex ' + points.length,
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
      'property float rot_0',
      'property float rot_1',
      'property float rot_2',
      'property float rot_3',
      'property uint vulkax_id_namespace',
      'property uint vulkax_id_local',
      'end_header',
    ];
    for (let i = 0; i < points.length; ++i) {
      const p = points[i] || {}, pos = p.p || [0, 0, 0], color = p.c || [0.5, 0.5, 0.5];
      if (pos.length < 3 || !pos.slice(0, 3).every(Number.isFinite)) throw new Error('Splat ' + (i + 1) + ' has invalid position');
      const scale = clamp(Math.abs(finite(p.s, 0.01)), 1e-8, 1e8), ls = Math.log(scale);
      const localId = i + 1;
      lines.push([
        pos[0], pos[1], pos[2],
        shDc(color[0]), shDc(color[1]), shDc(color[2]),
        logit(p.a),
        ls, ls, ls,
        1, 0, 0, 0,
        namespaceId, localId,
      ].map(v => Number(v).toPrecision(17)).join(' '));
    }
    return lines.join('\n') + '\n';
  }

  function download(points, filename, options = {}) {
    if (typeof document === 'undefined' || typeof Blob === 'undefined' || typeof URL === 'undefined') throw new Error('Browser download API is unavailable');
    const text = toPly(points, options);
    const blob = new Blob([text], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    try {
      const a = document.createElement('a');
      a.href = url;
      a.download = filename || 'vulkax_imported_splats.ply';
      document.body.appendChild(a);
      a.click();
      a.remove();
    } finally { setTimeout(() => URL.revokeObjectURL(url), 1000); }
  }

  global.VulkaxSplatExport = { toPly, download };
  if (typeof module !== 'undefined' && module.exports) module.exports = global.VulkaxSplatExport;
})(typeof globalThis !== 'undefined' ? globalThis : this);
