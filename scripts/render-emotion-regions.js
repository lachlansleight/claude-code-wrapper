#!/usr/bin/env node
/**
 * ASCII plot of FaceConfig::kEmotionPoints in FACE_CONFIG.h (nearest-anchor /
 * Voronoi-style regions). Resolution matches firmware: nearest point in (v,a);
 * ties break by kPickOrder (first listed wins).
 *
 * Grid: same number of characters per 0.05 on v and on a (square in (v,a) space).
 *   v ∈ [-1, 1] → 40 steps × 4 = 160 columns
 *   a ∈ [0, 1] → 20 steps × 4 = 80 rows (top row = high arousal)
 *
 * Usage:
 *   node scripts/render-emotion-regions.js [path/to/FACE_CONFIG.h]
 * Options:
 *   --compact    80×40 grid (2 chars per 0.05 on each axis)
 *   --rows N     override row count (columns stay at 160 unless --compact)
 *   --square     alias for --compact (legacy)
 */

const fs = require("fs");
const path = require("path");

const STEP = 0.05;
const CHARS_PER_005_V = 4;
const CHARS_PER_005_A = 4;
const COMPACT_CHARS_PER_005 = 2;
const V_LO = -1;
const V_HI = 1;
const A_LO = 0;
const A_HI = 1;
const DIST_SQ_TIE_EPS = 1e-7;

function usage() {
  console.error(
    `Usage: node ${path.basename(process.argv[1])} [FACE_CONFIG.h] [--compact] [--rows N]`,
  );
  process.exit(1);
}

function parseArgs(argv) {
  let file = null;
  let rowsOverride = null;
  let compact = false;
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--help" || a === "-h") usage();
    if (a === "--square" || a === "--compact") {
      compact = true;
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
      path.join(__dirname, "..", "robot_v3", "src", "face", "FACE_CONFIG.h"),
    cols,
    rows,
    charsPer005V: cv,
    charsPer005A: ca,
  };
}

function parseEmotionPoints(src) {
  const i = src.indexOf("EmotionPoint kEmotionPoints");
  if (i < 0) throw new Error("EmotionPoint kEmotionPoints not found");
  const slice = src.slice(i);
  const o = slice.indexOf("{");
  const c = slice.indexOf("};");
  if (o < 0 || c < 0) throw new Error("kEmotionPoints array braces not found");
  const body = slice.slice(o + 1, c);
  const points = [];
  for (const line of body.split("\n")) {
    const m = line.match(/\{\s*([^}]*)\}\s*,\s*\/\/\s*(\w+)/);
    if (!m) continue;
    const numStr = m[1].replace(/f\b/gi, " ");
    const nums = [];
    for (const x of numStr.matchAll(/-?\d*\.?\d+(?:[eE][+-]?\d+)?/g)) {
      nums.push(parseFloat(x[0]));
    }
    if (nums.length < 2) continue;
    let v;
    let a;
    if (nums.length >= 4) {
      const [v0, a0, v1, a1] = nums;
      const minV = Math.min(v0, v1);
      const maxV = Math.max(v0, v1);
      const minA = Math.min(a0, a1);
      const maxA = Math.max(a0, a1);
      v = 0.5 * (minV + maxV);
      a = 0.5 * (minA + maxA);
    } else {
      v = nums[0];
      a = nums[1];
    }
    points.push({ name: m[2], v, a });
  }
  return points;
}

function parsePickOrder(src) {
  const needle =
    src.indexOf("static constexpr EmotionSystem::NamedEmotion kPickOrder") >= 0
      ? "static constexpr EmotionSystem::NamedEmotion kPickOrder"
      : "static constexpr NamedEmotion kPickOrder";
  const i = src.indexOf(needle);
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

function distSq(v, a, p) {
  const dv = v - p.v;
  const da = a - p.a;
  return dv * dv + da * da;
}

function winnerAt(v, a, pointsByName, pointsList, pickOrder) {
  let bestD = Infinity;
  for (const p of pointsList) {
    const d = distSq(v, a, p);
    if (d < bestD) bestD = d;
  }
  for (const name of pickOrder) {
    const p = pointsByName.get(name);
    if (!p) continue;
    const d = distSq(v, a, p);
    if (d <= bestD + DIST_SQ_TIE_EPS) return name;
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
  const pointsList = parseEmotionPoints(src);
  const pickOrder = parsePickOrder(src);
  const pointsByName = new Map(pointsList.map((p) => [p.name, p]));
  const inPick = new Set(pickOrder);
  for (const p of pointsList) {
    if (!inPick.has(p.name)) {
      console.error(
        `Warning: ${p.name} is in kEmotionPoints but not kPickOrder — tie-breaks may not match firmware.`,
      );
    }
  }

  const { cols, rows } = opts;
  const lines = [];
  const vAxis =
    `v=${V_LO}`.padEnd(8) + "←" + "─".repeat(Math.max(0, cols - 18)) + "→" + `v=${V_HI}`;
  lines.push(vAxis);
  lines.push(`a=${A_HI} (top)`.padEnd(cols));
  for (let r = 0; r < rows; r++) {
    const a = A_HI - ((r + 0.5) / rows) * (A_HI - A_LO);
    let row = "";
    for (let c = 0; c < cols; c++) {
      const v = V_LO + ((c + 0.5) / cols) * (V_HI - V_LO);
      const w = winnerAt(v, a, pointsByName, pointsList, pickOrder);
      row += w ? tileChar(w, c, r, cols) : " ";
    }
    lines.push(row);
  }
  lines.push(`a=${A_LO} (bottom)`.padEnd(cols));
  lines.push("");
  lines.push(`Source: ${path.relative(process.cwd(), opts.file) || opts.file}`);
  lines.push(
    `Grid: ${cols}×${rows} chars (${STEP} v → ${opts.charsPer005V} chars, ${STEP} a → ${opts.charsPer005A} chars); nearest anchor, ties → pick order`,
  );
  lines.push(`Pick order (tie-break): ${pickOrder.join(" → ")}`);
  console.log(lines.join("\n"));
}

main();
