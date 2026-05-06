'use strict';

const fs = require('fs');
const path = require('path');
const { createCanvas } = require('canvas');

const SIZE = 512;
const NUM_BOXES = 10;
const RAYS_PER_BOX = 10_000;
const RAY_ALPHA = 0.05;
const LINE_WIDTH = 1;

const BOX_MIN_DIM = 40;
const BOX_MAX_DIM = 140;
const BOX_PLACEMENT_MAX_RETRIES = 1000;

// Epsilon used to skip the ray's own origin point when finding the
// nearest forward intersection. Coordinates live in pixel space so
// 1e-6 is many orders of magnitude below visible precision.
const EPS = 1e-6;

function randInt(minInclusive, maxExclusive) {
  return Math.floor(minInclusive + Math.random() * (maxExclusive - minInclusive));
}

function randRange(min, max) {
  return min + Math.random() * (max - min);
}

function aabbsOverlap(a, b) {
  return !(a.maxX <= b.minX || b.maxX <= a.minX || a.maxY <= b.minY || b.maxY <= a.minY);
}

function generateBoxes(count) {
  const boxes = [];

  for (let i = 0; i < count; i++) {
    let placed = null;
    for (let attempt = 0; attempt < BOX_PLACEMENT_MAX_RETRIES; attempt++) {
      const w = randRange(BOX_MIN_DIM, BOX_MAX_DIM);
      const h = randRange(BOX_MIN_DIM, BOX_MAX_DIM);
      const minX = randRange(0, SIZE - w);
      const minY = randRange(0, SIZE - h);
      const candidate = { minX, minY, maxX: minX + w, maxY: minY + h };
      if (!boxes.some((b) => aabbsOverlap(candidate, b))) {
        placed = candidate;
        break;
      }
    }
    if (!placed) {
      throw new Error(`Failed to place box ${i} after ${BOX_PLACEMENT_MAX_RETRIES} attempts`);
    }

    const id = i;
    placed.id = id;

    boxes.push(placed);
  }

  return boxes;
}

// ID 0..255 -> HSL color. Hue ramps 0..300 deg so red doesn't wrap back to red.
function colorsForId(id) {
  const hue = (id / NUM_BOXES) * 360;
  return {
    solid: `hsl(${hue.toFixed(2)}, 100%, 55%)`,
    ray: `hsla(${hue.toFixed(2)}, 100%, 55%, ${RAY_ALPHA})`,
  };
}

// Sample a uniformly random point on a box's perimeter, weighted by side length.
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

// Smallest t > EPS at which the ray enters/exits an AABB. Uses the slab method
// and returns Infinity if there's no forward hit on the box's edges.
function intersectAabb(ox, oy, dx, dy, box) {
  const invDx = dx !== 0 ? 1 / dx : Infinity;
  const invDy = dy !== 0 ? 1 / dy : Infinity;

  let tx1 = (box.minX - ox) * invDx;
  let tx2 = (box.maxX - ox) * invDx;
  if (tx1 > tx2) { const tmp = tx1; tx1 = tx2; tx2 = tmp; }

  let ty1 = (box.minY - oy) * invDy;
  let ty2 = (box.maxY - oy) * invDy;
  if (ty1 > ty2) { const tmp = ty1; ty1 = ty2; ty2 = tmp; }

  const tEnter = Math.max(tx1, ty1);
  const tExit = Math.min(tx2, ty2);

  if (tExit < tEnter || tExit <= EPS) return Infinity;

  // First positive hit is tEnter if we're outside, otherwise tExit (we're inside / on edge).
  return tEnter > EPS ? tEnter : tExit;
}

// Smallest t > EPS at which the ray crosses the canvas boundary.
// One of the slabs always yields a finite hit because the origin is in [0,SIZE].
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
  return { x: ox + tMin * dx, y: oy + tMin * dy };
}

function render(boxes) {
  const canvas = createCanvas(SIZE, SIZE);
  const ctx = canvas.getContext('2d');

  ctx.fillStyle = '#000';
  ctx.fillRect(0, 0, SIZE, SIZE);

  ctx.lineWidth = LINE_WIDTH;
  ctx.lineCap = 'butt';

  for (const box of boxes) {
    const { ray: rayColor } = colorsForId(box.id);
    ctx.strokeStyle = rayColor;
    ctx.beginPath();

    for (let i = 0; i < RAYS_PER_BOX; i++) {
      const origin = sampleBoxPerimeter(box);
      const theta = Math.random() * Math.PI * 2;
      const dx = Math.cos(theta);
      const dy = Math.sin(theta);
      const end = castRay(origin.x, origin.y, dx, dy, boxes);
      ctx.moveTo(origin.x, origin.y);
      ctx.lineTo(end.x, end.y);
    }

    ctx.stroke();
  }

  for (const box of boxes) {
    const { solid } = colorsForId(box.id);
    ctx.strokeStyle = solid;
    ctx.strokeRect(box.minX, box.minY, box.maxX - box.minX, box.maxY - box.minY);
  }

  return canvas;
}

function main() {
  const start = Date.now();
  const boxes = generateBoxes(NUM_BOXES);
  const canvas = render(boxes);
  const outPath = path.join(__dirname, 'raycast.png');
  fs.writeFileSync(outPath, canvas.toBuffer('image/png'));
  const elapsed = Date.now() - start;
  console.log(
    `Rendered ${NUM_BOXES} boxes x ${RAYS_PER_BOX} rays = ${NUM_BOXES * RAYS_PER_BOX} rays ` +
      `in ${elapsed} ms -> ${outPath}`
  );
}

main();
