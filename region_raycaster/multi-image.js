'use strict';

const fs = require('fs');
const path = require('path');
const { createCanvas, createImageData } = require('canvas');

const SIZE = 512;
const NUM_BOXES = 10;
const RAYS_PER_BOX = 1_000_000;

const BOX_MIN_DIM = 40;
const BOX_MAX_DIM = 140;
const BOX_PLACEMENT_MAX_RETRIES = 1000;

// Skip immediately re-hitting the start point on the perimeter.
const EPS = 1e-6;

// Value is 1 at d=0 and d=1, then halves every 50 px after that:
// value(d) = 2^(-(d - 1) / 50), for d > 1.
const HALF_DISTANCE_PX = 50;

function randRange(min, max) {
  return min + Math.random() * (max - min);
}

function aabbsOverlap(a, b) {
  return !(a.maxX <= b.minX || b.maxX <= a.minX || a.maxY <= b.minY || b.maxY <= a.minY);
}

function generateBoxes(count) {

  // return [
  //   { id: 0, minX: 320, minY: 448, maxX: 512, maxY: 512 }, //joyful
  //   { id: 1, minX: 320, minY: 300, maxX: 512, maxY: 350 }, //excited
  //   { id: 2, minX: 320, minY: 200, maxX: 512, maxY: 300 }, //happy
  //   { id: 3, minX: 200, minY: 0, maxX: 300, maxY: 512 }, //neutral
  //   { id: 4, minX: 0, minY: 0, maxX: 100, maxY: 512 }, //sad
  // ];

  const boxes = [];
  for (let i = 0; i < count; i++) {
    let placed = null;

    for (let attempt = 0; attempt < BOX_PLACEMENT_MAX_RETRIES; attempt++) {
      const w = randRange(BOX_MIN_DIM, BOX_MAX_DIM);
      const h = randRange(BOX_MIN_DIM, BOX_MAX_DIM);
      const minX = randRange(0, SIZE - w);
      const minY = randRange(0, SIZE - h);
      const candidate = { id: i, minX, minY, maxX: minX + w, maxY: minY + h };
      if (!boxes.some((b) => aabbsOverlap(candidate, b))) {
        placed = candidate;
        break;
      }
    }

    if (!placed) {
      throw new Error(`Failed to place box ${i} after ${BOX_PLACEMENT_MAX_RETRIES} attempts`);
    }

    boxes.push(placed);
  }

  return boxes;
}

function sampleBoxPerimeter(box) {
  const w = box.maxX - box.minX;
  const h = box.maxY - box.minY;
  const perimeter = 2 * (w + h);
  let t = Math.random() * perimeter;

  if (t < w) return { x: box.minX + t, y: box.minY };
  t -= w;
  if (t < h) return { x: box.maxX, y: box.minY + t };
  t -= h;
  if (t < w) return { x: box.maxX - t, y: box.maxY };
  t -= w;
  return { x: box.minX, y: box.maxY - t };
}

function intersectAabb(ox, oy, dx, dy, box) {
  const invDx = dx !== 0 ? 1 / dx : Infinity;
  const invDy = dy !== 0 ? 1 / dy : Infinity;

  let tx1 = (box.minX - ox) * invDx;
  let tx2 = (box.maxX - ox) * invDx;
  if (tx1 > tx2) {
    const tmp = tx1;
    tx1 = tx2;
    tx2 = tmp;
  }

  let ty1 = (box.minY - oy) * invDy;
  let ty2 = (box.maxY - oy) * invDy;
  if (ty1 > ty2) {
    const tmp = ty1;
    ty1 = ty2;
    ty2 = tmp;
  }

  const tEnter = Math.max(tx1, ty1);
  const tExit = Math.min(tx2, ty2);

  if (tExit < tEnter || tExit <= EPS) return Infinity;
  return tEnter > EPS ? tEnter : tExit;
}

function intersectCanvasBounds(ox, oy, dx, dy) {
  let best = Infinity;

  if (dx > 0) {
    const t = (SIZE - ox) / dx;
    if (t > EPS && t < best) best = t;
  } else if (dx < 0) {
    const t = (0 - ox) / dx;
    if (t > EPS && t < best) best = t;
  }

  if (dy > 0) {
    const t = (SIZE - oy) / dy;
    if (t > EPS && t < best) best = t;
  } else if (dy < 0) {
    const t = (0 - oy) / dy;
    if (t > EPS && t < best) best = t;
  }

  return best;
}

function castRay(ox, oy, dx, dy, boxes) {
  let tMin = intersectCanvasBounds(ox, oy, dx, dy);
  for (let i = 0; i < boxes.length; i++) {
    const t = intersectAabb(ox, oy, dx, dy, boxes[i]);
    if (t < tMin) tMin = t;
  }
  return tMin;
}

function falloffValue(distancePx) {
  if (distancePx <= 1) return 1;
  return Math.pow(2, -((distancePx - 1) / HALF_DISTANCE_PX));
}

function writeRayToLayer(buffer, layerOffset, ox, oy, dx, dy, tEnd) {
  // Step one pixel at a time along ray distance.
  const steps = Math.max(1, Math.ceil(tEnd));
  for (let s = 0; s <= steps; s++) {
    const t = (s / steps) * tEnd;
    const x = Math.round(ox + dx * t);
    const y = Math.round(oy + dy * t);
    if (x < 0 || x >= SIZE || y < 0 || y >= SIZE) continue;

    const idx = layerOffset + y * SIZE + x;
    const value = falloffValue(t);

    // Keep the brightest contribution per pixel for this origin box.
    if (value > buffer[idx]) {
      buffer[idx] = value;
    }
  }
}

function saveLayerAsPng(buffer, layerOffset, outPath) {
  const canvas = createCanvas(SIZE, SIZE);
  const ctx = canvas.getContext('2d');
  const pixels = new Uint8ClampedArray(SIZE * SIZE * 4);

  for (let y = 0; y < SIZE; y++) {
    for (let x = 0; x < SIZE; x++) {
      const srcIdx = layerOffset + y * SIZE + x;
      const dstIdx = (y * SIZE + x) * 4;
      const v = Math.max(0, Math.min(1, buffer[srcIdx]));
      const gray = Math.round(v * 255);
      pixels[dstIdx] = gray;
      pixels[dstIdx + 1] = gray;
      pixels[dstIdx + 2] = gray;
      pixels[dstIdx + 3] = 255;
    }
  }

  const imageData = createImageData(pixels, SIZE, SIZE);
  ctx.putImageData(imageData, 0, 0);
  fs.writeFileSync(outPath, canvas.toBuffer('image/png'));
}

function main() {
  const start = Date.now();
  const boxes = generateBoxes(NUM_BOXES);

  const layerSize = SIZE * SIZE;
  const buffer = new Float32Array(layerSize * NUM_BOXES);
  const outputPaths = [];

  for (let layer = 0; layer < boxes.length; layer++) {
    console.log(`Rendering layer ${layer} of ${boxes.length}`);
    const box = boxes[layer];
    const layerOffset = layer * layerSize;

    for (let i = 0; i < RAYS_PER_BOX; i++) {
      const origin = sampleBoxPerimeter(box);
      const theta = Math.random() * Math.PI * 2;
      const dx = Math.cos(theta);
      const dy = Math.sin(theta);
      const tEnd = castRay(origin.x, origin.y, dx, dy, boxes);
      writeRayToLayer(buffer, layerOffset, origin.x, origin.y, dx, dy, tEnd);
    }

    const id = boxes[layer].id;
    const outPath = path.join(__dirname, `raycast-${id}.png`);
    saveLayerAsPng(buffer, layerOffset, outPath);
    outputPaths.push(outPath);
  }

  const elapsed = Date.now() - start;
  console.log(
    `Rendered ${NUM_BOXES} layers (${NUM_BOXES * RAYS_PER_BOX} rays total) in ${elapsed} ms`
  );
  for (const outPath of outputPaths) {
    console.log(`Saved ${outPath}`);
  }
}

main();
