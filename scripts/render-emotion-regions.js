#!/usr/bin/env node
/**
 * ASCII plot of EmotionSystem.cpp kBoxes, with overlap resolution matching
 * emotionForPoint: first matching region in kPickOrder wins.
 *
 * Grid: same number of characters per 0.05 on v and on a (square in (v,a) space).
 *   v ∈ [-1, 1] → 40 steps × 4 = 160 columns
 *   a ∈ [0, 1] → 20 steps × 4 = 80 rows (top row = high arousal)
 *
 * Usage:
 *   node scripts/render-emotion-regions.js [path/to/EmotionSystem.cpp]
 * Options:
 *   --compact    80×40 grid (2 chars per 0.05 on each axis)
 *   --rows N     override row count (columns stay at 160 unless --compact)
 *   --square     alias for --compact (legacy)
 *   --raw        do not swap min/max when a box has inverted bounds
 */

const fs = require("fs");
const path = require("path");

const STEP = 0.05;
/** Chars per 0.05 step on valence (full grid width = (v range / STEP) × this). */
const CHARS_PER_005_V = 4;
/** Chars per 0.05 step on arousal. */
const CHARS_PER_005_A = 4;
const COMPACT_CHARS_PER_005 = 2;
const V_LO = -1;
const V_HI = 1;
const A_LO = 0;
const A_HI = 1;

function usage() {
  console.error(
    `Usage: node ${path.basename(process.argv[1])} [EmotionSystem.cpp] [--compact] [--rows N] [--raw]`,
  );
  process.exit(1);
}

function parseArgs(argv) {
  let file = null;
  let rowsOverride = null;
  let compact = false;
  let raw = false;
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--help" || a === "-h") usage();
    if (a === "--square" || a === "--compact") {
      compact = true;
      continue;
    }
    if (a === "--raw") {
      raw = true;
      continue;
    }
    if (a === "--rows") {
      const n = parseInt(argv[++i], 10);
      if (!Number.isFinite(n) || n < 1) usage();
      rowsOverride = n;
      continue;
    }
    if (a.startsWith("-")) usage();
    if (file == null) file = a;
    else usage();
  }
  const cv = compact ? COMPACT_CHARS_PER_005 : CHARS_PER_005_V;
  const ca = compact ? COMPACT_CHARS_PER_005 : CHARS_PER_005_A;
  let cols = Math.round((V_HI - V_LO) / STEP) * cv;
  let rows = Math.round((A_HI - A_LO) / STEP) * ca;
  if (rowsOverride != null) rows = rowsOverride;
  return {
    file:
      file ||
      path.join(__dirname, "..", "robot_v3", "src", "behaviour", "EmotionSystem.cpp"),
    cols,
    rows,
    raw,
    charsPer005V: cv,
    charsPer005A: ca,
  };
}

function parseBoxes(src) {
  const i = src.indexOf("constexpr Box kBoxes");
  if (i < 0) throw new Error("constexpr Box kBoxes not found");
  const slice = src.slice(i);
  const o = slice.indexOf("{");
  const c = slice.indexOf("};");
  if (o < 0 || c < 0) throw new Error("kBoxes array braces not found");
  const body = slice.slice(o + 1, c);
  const boxes = [];
  for (const line of body.split("\n")) {
    const m = line.match(/\{\s*([^}]*)\}\s*,\s*\/\/\s*(\w+)/);
    if (!m) continue;
    const numStr = m[1].replace(/f\b/gi, " ");
    const nums = [];
    for (const x of numStr.matchAll(/-?\d*\.?\d+(?:[eE][+-]?\d+)?/g)) {
      nums.push(parseFloat(x[0]));
    }
    if (nums.length < 4) continue;
    boxes.push({
      name: m[2],
      minV: nums[0],
      minA: nums[1],
      maxV: nums[2],
      maxA: nums[3],
    });
  }
  return boxes;
}

function parsePickOrder(src) {
  const i = src.indexOf("static constexpr NamedEmotion kPickOrder");
  if (i < 0) throw new Error("kPickOrder not found");
  const slice = src.slice(i);
  const o = slice.indexOf("{");
  const c = slice.indexOf("};");
  if (o < 0 || c < 0) throw new Error("kPickOrder braces not found");
  const body = slice.slice(o + 1, c);
  const order = [];
  for (const m of body.matchAll(/NamedEmotion::(\w+)/g)) {
    order.push(m[1]);
  }
  return order;
}

function normalizeBox(b, raw) {
  if (raw) return { ...b };
  let { minV, maxV, minA, maxA, name } = b;
  if (minV > maxV) [minV, maxV] = [maxV, minV];
  if (minA > maxA) [minA, maxA] = [maxA, minA];
  return { name, minV, maxV, minA, maxA };
}

function inBox(v, a, b) {
  return v >= b.minV && v <= b.maxV && a >= b.minA && a <= b.maxA;
}

function winnerAt(v, a, boxesByName, pickOrder) {
  for (const name of pickOrder) {
    const box = boxesByName.get(name);
    if (box && inBox(v, a, box)) return name;
  }
  return null;
}

function tileChar(name, col, row, cols) {
  const label = name.toUpperCase().replace(/[^A-Z0-9]/g, "") || "?";
  const k = (row * cols + col) % label.length;
  return label[k];
}

function main() {
  const opts = parseArgs(process.argv);
  const src = fs.readFileSync(opts.file, "utf8");
  const rawBoxes = parseBoxes(src);
  const pickOrder = parsePickOrder(src);
  const boxes = rawBoxes.map((b) => normalizeBox(b, opts.raw));
  const boxesByName = new Map(boxes.map((b) => [b.name, b]));
  const inPick = new Set(pickOrder);
  for (const b of boxes) {
    if (!inPick.has(b.name)) {
      console.error(
        `Warning: ${b.name} is in kBoxes but not kPickOrder — it never wins a cell (matches firmware).`,
      );
    }
  }

  const { cols, rows } = opts;
  const lines = [];
  const vAxis = `v=${V_LO}`.padEnd(8) + "←" + "─".repeat(Math.max(0, cols - 18)) + "→" + `v=${V_HI}`;
  lines.push(vAxis);
  lines.push(`a=${A_HI} (top)`.padEnd(cols));
  for (let r = 0; r < rows; r++) {
    const a = A_HI - ((r + 0.5) / rows) * (A_HI - A_LO);
    let row = "";
    for (let c = 0; c < cols; c++) {
      const v = V_LO + ((c + 0.5) / cols) * (V_HI - V_LO);
      const w = winnerAt(v, a, boxesByName, pickOrder);
      row += w ? tileChar(w, c, r, cols) : " ";
    }
    lines.push(row);
  }
  lines.push(`a=${A_LO} (bottom)`.padEnd(cols));
  lines.push("");
  lines.push(`Source: ${path.relative(process.cwd(), opts.file) || opts.file}`);
  lines.push(
    `Grid: ${cols}×${rows} chars (${STEP} v → ${opts.charsPer005V} chars, ${STEP} a → ${opts.charsPer005A} chars)`,
  );
  lines.push(`Pick order: ${pickOrder.join(" → ")}`);
  console.log(lines.join("\n"));
}

main();
