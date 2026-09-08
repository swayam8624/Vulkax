/* Vulkax interactive-viewer image -> splat-card importer.
 *
 * A single RGB image does not contain true 3D geometry. This importer therefore
 * creates an explicitly presentation-only 2.5D splat card instead of inventing
 * depth. Browser decoding uses createImageBitmap with an <img> fallback; the pure
 * pixelsToSplats path is Node-testable.
 */
(function (global) {
  'use strict';

  const DEFAULT_MAX_POINTS = 80000;
  const MAX_FILE_BYTES = 128 * 1024 * 1024;

  function clamp(x, lo = 0, hi = 1) { return Math.max(lo, Math.min(hi, x)); }

  function pixelsToSplats(rgba, width, height, options = {}) {
    if (!Number.isInteger(width) || width <= 0 || !Number.isInteger(height) || height <= 0) throw new Error('Image dimensions must be positive integers');
    if (!rgba || rgba.length < width * height * 4) throw new Error('Image RGBA buffer is truncated');
    const maxPoints = Math.max(1, Math.floor(options.maxPoints || DEFAULT_MAX_POINTS));
    const alphaThreshold = clamp(options.alphaThreshold === undefined ? 0.02 : Number(options.alphaThreshold), 0, 1);
    const totalPixels = width * height;
    const stride = Math.max(1, Math.ceil(Math.sqrt(totalPixels / maxPoints)));
    const maxDim = Math.max(width, height);
    const points = [];
    let eligible = 0;

    // Count eligible source pixels at the chosen sampling lattice so the fallback
    // scale remains stable across images with large transparent regions.
    for (let y = 0; y < height; y += stride) for (let x = 0; x < width; x += stride) {
      const i = (y * width + x) * 4;
      if (rgba[i + 3] / 255 >= alphaThreshold) ++eligible;
    }
    const scale = clamp((stride / maxDim) * 0.72, 0.0008, 0.035);

    for (let y = 0; y < height; y += stride) for (let x = 0; x < width; x += stride) {
      const i = (y * width + x) * 4;
      const alpha = rgba[i + 3] / 255;
      if (alpha < alphaThreshold) continue;
      // Preserve image aspect ratio with the longest image dimension spanning 1.0.
      const px = ((x + 0.5) - width * 0.5) / maxDim;
      const py = (height * 0.5 - (y + 0.5)) / maxDim;
      points.push({
        id: points.length + 1,
        p: [px, py, 0],
        c: [rgba[i] / 255, rgba[i + 1] / 255, rgba[i + 2] / 255],
        s: scale,
        a: alpha,
      });
    }
    if (!points.length) throw new Error('Image contains no visible pixels above the alpha threshold');
    return {
      points,
      kind: 'image_splat_card',
      format: 'image_2.5d',
      sourceCount: totalPixels,
      sampledPixelCount: eligible,
      pixelWidth: width,
      pixelHeight: height,
      stride,
      downsampled: stride > 1,
      presentationOnly: true,
    };
  }

  function canvasPixels(source, width, height) {
    const canvas = document.createElement('canvas');
    canvas.width = width; canvas.height = height;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    if (!ctx) throw new Error('Browser 2D canvas is unavailable for image decoding');
    ctx.clearRect(0, 0, width, height);
    ctx.drawImage(source, 0, 0, width, height);
    return ctx.getImageData(0, 0, width, height).data;
  }

  async function decodeWithImageElement(file) {
    if (typeof Image === 'undefined' || typeof URL === 'undefined') throw new Error('Browser image decoder is unavailable');
    const url = URL.createObjectURL(file);
    try {
      const image = await new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = () => reject(new Error('Browser could not decode this image format'));
        img.src = url;
      });
      return { source: image, width: image.naturalWidth || image.width, height: image.naturalHeight || image.height };
    } finally { URL.revokeObjectURL(url); }
  }

  async function parseFile(file, options = {}) {
    if (!file) throw new Error('No image file selected');
    if (file.size === 0) throw new Error('Selected image is empty');
    if (file.size > (options.maxFileBytes || MAX_FILE_BYTES)) throw new Error('Image is too large for browser splat-card import');
    let decoded = null;
    if (typeof createImageBitmap === 'function') {
      try {
        const bitmap = await createImageBitmap(file);
        decoded = { source: bitmap, width: bitmap.width, height: bitmap.height, close: () => bitmap.close && bitmap.close() };
      } catch (_) { decoded = null; }
    }
    if (!decoded) decoded = await decodeWithImageElement(file);
    try {
      if (!decoded.width || !decoded.height) throw new Error('Decoded image has invalid dimensions');
      const rgba = canvasPixels(decoded.source, decoded.width, decoded.height);
      const result = pixelsToSplats(rgba, decoded.width, decoded.height, options);
      result.sourceName = file.name || 'image';
      result.mimeType = file.type || '';
      return result;
    } finally {
      if (decoded.close) decoded.close();
    }
  }

  global.VulkaxImageImport = { parseFile, pixelsToSplats, DEFAULT_MAX_POINTS, MAX_FILE_BYTES };
  if (typeof module !== 'undefined' && module.exports) module.exports = global.VulkaxImageImport;
})(typeof globalThis !== 'undefined' ? globalThis : this);
