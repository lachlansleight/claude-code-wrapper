'use strict';

const fs = require('fs');
const path = require('path');
const { createCanvas, loadImage } = require('canvas');

const SIZE = 512;

const RAYCAST_NAME_RE = /^raycast-(\d+)\.png$/i;

/**
 * All `raycast-<id>.png` files in `dir`, sorted by numeric id ascending.
 * @param {string} dir
 * @returns {{ id: number, filePath: string }[]}
 */
function discoverRaycastPngs(dir) {
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  const out = [];
  for (const ent of entries) {
    if (!ent.isFile()) continue;
    const m = ent.name.match(RAYCAST_NAME_RE);
    if (!m) continue;
    out.push({
      id: Number.parseInt(m[1], 10),
      filePath: path.join(dir, ent.name),
    });
  }
  out.sort((a, b) => a.id - b.id);
  return out;
}

function clamp01(v) {
  if (v < 0) return 0;
  if (v > 1) return 1;
  return v;
}

// Same assignment logic as index.js: hue = (index / boxCount) * 360, s=100%, l=55%.
// `index` is 0..boxCount-1 in sorted raycast file order (matches contiguous ids 0..n-1).
// Converted to RGB in [0, 255].
function indexToRgb(index, boxCount) {
  const h = (index / boxCount) * 360;
  const s = 1;
  const l = 0.55;

  const c = (1 - Math.abs(2 * l - 1)) * s;
  const hPrime = h / 60;
  const x = c * (1 - Math.abs((hPrime % 2) - 1));

  let r1 = 0;
  let g1 = 0;
  let b1 = 0;

  if (hPrime >= 0 && hPrime < 1) {
    r1 = c;
    g1 = x;
  } else if (hPrime >= 1 && hPrime < 2) {
    r1 = x;
    g1 = c;
  } else if (hPrime >= 2 && hPrime < 3) {
    g1 = c;
    b1 = x;
  } else if (hPrime >= 3 && hPrime < 4) {
    g1 = x;
    b1 = c;
  } else if (hPrime >= 4 && hPrime < 5) {
    r1 = x;
    b1 = c;
  } else {
    r1 = c;
    b1 = x;
  }

  const m = l - c / 2;
  return {
    r: Math.round((r1 + m) * 255),
    g: Math.round((g1 + m) * 255),
    b: Math.round((b1 + m) * 255),
  };
}

async function readGrayscaleData(filePath) {
  const image = await loadImage(filePath);
  const canvas = createCanvas(SIZE, SIZE);
  const ctx = canvas.getContext('2d');
  ctx.drawImage(image, 0, 0, SIZE, SIZE);
  return ctx.getImageData(0, 0, SIZE, SIZE).data;
}

async function main() {
  const start = Date.now();
  const layers = discoverRaycastPngs(__dirname);
  if (layers.length === 0) {
    throw new Error(
      `No raycast-*.png files found in ${__dirname}. Run multi-image.js first.`
    );
  }

  const pixelCount = SIZE * SIZE;
  const accum = new Float32Array(pixelCount * 3); // [r,g,b] per pixel
  const boxCount = layers.length;

  const perBoxGray = [];
  for (const { filePath } of layers) {
    perBoxGray.push(await readGrayscaleData(filePath));
  }

  for (let i = 0; i < boxCount; i++) {
    const { r, g, b } = indexToRgb(i, boxCount);
    const gray = perBoxGray[i];

    for (let p = 0; p < pixelCount; p++) {
      const srcIdx = p * 4;
      const brightness = clamp01(gray[srcIdx] / 255);
      if (brightness <= 0) continue;

      const dstIdx = p * 3;
      accum[dstIdx] += r * brightness;
      accum[dstIdx + 1] += g * brightness;
      accum[dstIdx + 2] += b * brightness;
    }
  }

  // Auto exposure: scale all channels so the global brightest channel hits 255.
  let globalPeak = 0;
  for (let p = 0; p < pixelCount; p++) {
    const srcIdx = p * 3;
    globalPeak = Math.max(globalPeak, accum[srcIdx], accum[srcIdx + 1], accum[srcIdx + 2]);
  }
  const exposureScale = globalPeak > 0 ? 255 / globalPeak : 1;

  const outCanvas = createCanvas(SIZE, SIZE);
  const outCtx = outCanvas.getContext('2d');
  const outImage = outCtx.createImageData(SIZE, SIZE);
  const out = outImage.data;

  for (let p = 0; p < pixelCount; p++) {
    const srcIdx = p * 3;
    let r = accum[srcIdx] * exposureScale;
    let g = accum[srcIdx + 1] * exposureScale;
    let b = accum[srcIdx + 2] * exposureScale;

    const peak = Math.max(r, g, b);
    if (peak > 255) {
      const scale = 255 / peak;
      r *= scale;
      g *= scale;
      b *= scale;
    }

    const dstIdx = p * 4;
    out[dstIdx] = Math.round(Math.max(0, Math.min(255, r)));
    out[dstIdx + 1] = Math.round(Math.max(0, Math.min(255, g)));
    out[dstIdx + 2] = Math.round(Math.max(0, Math.min(255, b)));
    out[dstIdx + 3] = 255;
  }

  outCtx.putImageData(outImage, 0, 0);

  const outPath = path.join(__dirname, 'combined.png');
  fs.writeFileSync(outPath, outCanvas.toBuffer('image/png'));

  const elapsed = Date.now() - start;
  console.log(
    `Combined ${boxCount} grayscale layers (${layers.map((l) => path.basename(l.filePath)).join(', ')}) ` +
      `in ${elapsed} ms -> ${outPath}`
  );
}

main().catch((err) => {
  console.error(err);
  process.exitCode = 1;
});
