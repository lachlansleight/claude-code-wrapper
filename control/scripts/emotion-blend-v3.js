// JS port of robot_v3/src/behaviour/EmotionBlend.cpp.
//
// Given a (valence, activation) point in the domain
// V in [-1, +1] and A in [0, 1], find the triangle in
// window.EmotionTriangulation that contains it and barycentric-blend
// the FaceParams of the three anchor presets.
//
// Anchor preset = the per-emotion preset in
// FrameControllerV3.baseTargetForExpression(emotionName). Emotion
// names from the triangulation ("Neutral", "Happy", "Excited",
// "Joyful", "Sad") map directly to expression names in BASE_TARGETS.
//
// Public API:
//   EmotionBlendV3.findTriangle(v, a) -> { indices: [i0,i1,i2], weights: [l0,l1,l2] } | null
//   EmotionBlendV3.blendedFaceParams(v, a) -> FaceParams
//   EmotionBlendV3.blendedEmotionArmMotion(v, a) -> { min_offset_deg, max_offset_deg, waggle_period_s, waggle_interval_s }
//   EmotionBlendV3.ready() -> bool
//
// This module mirrors the firmware's algorithm exactly, including the
// nearest-anchor fallback when the point falls outside every
// triangle (shouldn't happen given the domain-corner hull check, but
// keeps both paths in lockstep).

(function () {
  const BARY_EPS = 1e-5;

  function clampf(x, lo, hi) {
    return x < lo ? lo : x > hi ? hi : x;
  }

  function table() {
    return window.EmotionTriangulation;
  }

  function ready() {
    return !!(table() && Array.isArray(table().anchors) && Array.isArray(table().triangles));
  }

  // Barycentric weights of (v, a) inside triangle (i0, i1, i2).
  // Returns null on degenerate (zero-area) triangles.
  function barycentric(v, a, i0, i1, i2) {
    const t = table();
    const A = t.anchors[i0];
    const B = t.anchors[i1];
    const C = t.anchors[i2];
    const denom = (B.a - C.a) * (A.v - C.v) + (C.v - B.v) * (A.a - C.a);
    if (Math.abs(denom) < 1e-12) return null;
    const l0 = ((B.a - C.a) * (v - C.v) + (C.v - B.v) * (a - C.a)) / denom;
    const l1 = ((C.a - A.a) * (v - C.v) + (A.v - C.v) * (a - C.a)) / denom;
    const l2 = 1.0 - l0 - l1;
    return [l0, l1, l2];
  }

  function findTriangle(v, a) {
    if (!ready()) return null;
    const tris = table().triangles;
    for (let t = 0; t < tris.length; ++t) {
      const [i0, i1, i2] = tris[t];
      const w = barycentric(v, a, i0, i1, i2);
      if (!w) continue;
      let [l0, l1, l2] = w;
      if (l0 >= -BARY_EPS && l1 >= -BARY_EPS && l2 >= -BARY_EPS) {
        if (l0 < 0) l0 = 0;
        if (l1 < 0) l1 = 0;
        if (l2 < 0) l2 = 0;
        const s = l0 + l1 + l2;
        if (s > 1e-12) {
          const inv = 1 / s;
          l0 *= inv; l1 *= inv; l2 *= inv;
        }
        return { indices: [i0, i1, i2], weights: [l0, l1, l2] };
      }
    }
    return null;
  }

  function presetForEmotion(name) {
    const FC = window.FrameControllerV3;
    if (!FC) return null;
    return FC.baseTargetForExpression(name);
  }

  // Mirrors robot_v3 EmotionBlend.cpp::armPresetFor (keep in sync).
  const ARM_PRESETS = {
    Neutral: { min: -25, max: -15, period_s: 5.0, interval_s: 2.0 },
    Happy: { min: -23, max: -7, period_s: 2.0, interval_s: 1.0 },
    Excited: { min: -15, max: -5, period_s: 1.0, interval_s: 0.0 },
    Joyful: { min: -15, max: 15, period_s: 0.9, interval_s: 0.2 },
    Sad: { min: -20, max: -20, period_s: 1.0, interval_s: 0.0 },
    Sleepy: { min: -22, max: -14, period_s: 5.0, interval_s: 3.0 },
    Distressed: { min: -6, max: 6, period_s: 0.9, interval_s: 0.15 },
    Blissed: { min: -16, max: -4, period_s: 3.0, interval_s: 1.5 },
    Depressed: { min: 0, max: 0, period_s: 1.0, interval_s: 0.0 },
    Shocked: { min: 0, max: 0, period_s: 1.0, interval_s: 0.0 },
    Disappointed: { min: -15, max: -15, period_s: 1.0, interval_s: 0.0 },
  };

  function armPresetForEmotion(name) {
    return ARM_PRESETS[name] || { min: -20, max: -15, period_s: 4.0, interval_s: 2.0 };
  }

  function blendArmField(a, b, c, la, lb, lc) {
    return Math.round(a * la + b * lb + c * lc);
  }

  function blendArmFloat(a, b, c, la, lb, lc) {
    return a * la + b * lb + c * lc;
  }

  function blendArmThree(Pa, Pb, Pc, la, lb, lc) {
    return {
      min_offset_deg: blendArmField(Pa.min, Pb.min, Pc.min, la, lb, lc),
      max_offset_deg: blendArmField(Pa.max, Pb.max, Pc.max, la, lb, lc),
      waggle_period_s: blendArmFloat(Pa.period_s, Pb.period_s, Pc.period_s, la, lb, lc),
      waggle_interval_s: blendArmFloat(Pa.interval_s, Pb.interval_s, Pc.interval_s, la, lb, lc),
    };
  }

  function normalizeArmMotion(r) {
    let { min_offset_deg: lo, max_offset_deg: hi, waggle_period_s: ps, waggle_interval_s: is } = r;
    if (lo > hi) {
      const t = lo;
      lo = hi;
      hi = t;
    }
    if (ps < 0.05) ps = 0.05;
    if (is < 0) is = 0;
    return { min_offset_deg: lo, max_offset_deg: hi, waggle_period_s: ps, waggle_interval_s: is };
  }

  function blendedEmotionArmMotion(v, a) {
    if (!ready()) return null;
    v = clampf(v, -1, 1);
    a = clampf(a, 0, 1);
    const tri = findTriangle(v, a);
    if (!tri) {
      const idx = nearestAnchor(v, a);
      const e = table().anchors[idx].emotion;
      return normalizeArmMotion(armPresetForEmotion(e));
    }
    const [i0, i1, i2] = tri.indices;
    const [l0, l1, l2] = tri.weights;
    const t = table();
    const Pa = armPresetForEmotion(t.anchors[i0].emotion);
    const Pb = armPresetForEmotion(t.anchors[i1].emotion);
    const Pc = armPresetForEmotion(t.anchors[i2].emotion);
    return normalizeArmMotion(blendArmThree(Pa, Pb, Pc, l0, l1, l2));
  }

  function blendField(a, b, c, la, lb, lc) {
    return Math.round(a * la + b * lb + c * lc);
  }

  function blendThree(A, B, C, la, lb, lc) {
    const out = {};
    const fields = window.FrameControllerV3.paramFields();
    for (const k of fields) {
      out[k] = blendField(A[k] | 0, B[k] | 0, C[k] | 0, la, lb, lc);
    }
    return out;
  }

  function nearestAnchor(v, a) {
    const t = table();
    let bestD = Infinity;
    let best = 0;
    for (let i = 0; i < t.anchors.length; ++i) {
      const an = t.anchors[i];
      const dv = v - an.v;
      const da = a - an.a;
      const d = dv * dv + da * da;
      if (d < bestD) { bestD = d; best = i; }
    }
    return best;
  }

  function blendedFaceParams(v, a) {
    if (!ready()) return null;
    v = clampf(v, -1, 1);
    a = clampf(a, 0, 1);
    const tri = findTriangle(v, a);
    if (!tri) {
      const idx = nearestAnchor(v, a);
      const e = table().anchors[idx].emotion;
      const p = presetForEmotion(e);
      return p ? { ...p } : null;
    }
    const [i0, i1, i2] = tri.indices;
    const [l0, l1, l2] = tri.weights;
    const t = table();
    const A = presetForEmotion(t.anchors[i0].emotion);
    const B = presetForEmotion(t.anchors[i1].emotion);
    const C = presetForEmotion(t.anchors[i2].emotion);
    if (!A || !B || !C) return null;
    return blendThree(A, B, C, l0, l1, l2);
  }

  window.EmotionBlendV3 = {
    ready,
    findTriangle,
    blendedFaceParams,
    blendedEmotionArmMotion,
    table,
  };
})();
