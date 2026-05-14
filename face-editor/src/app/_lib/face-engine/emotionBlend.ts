/**
 * Port of robot_v3/src/behaviour/EmotionBlend.cpp
 * Ported from control/scripts/emotion-blend-v3.js — no window globals.
 */

import type { FaceParams } from "./faceParams";
import type { ParamField } from "./faceParams";
import type {
  BlendTriangle,
  EmotionArmMotion,
  EmotionTriangulationTable,
} from "./types";

const BARY_EPS = 1e-5;

function clampf(x: number, lo: number, hi: number): number {
  return x < lo ? lo : x > hi ? hi : x;
}

const ARM_PRESETS: Record<string, EmotionArmMotion> = {
  Neutral: {
    min_offset_deg: -25,
    max_offset_deg: -15,
    waggle_period_s: 5.0,
    waggle_interval_s: 2.0,
  },
  Happy: {
    min_offset_deg: -23,
    max_offset_deg: -7,
    waggle_period_s: 2.0,
    waggle_interval_s: 1.0,
  },
  Excited: {
    min_offset_deg: -15,
    max_offset_deg: -5,
    waggle_period_s: 1.0,
    waggle_interval_s: 0.0,
  },
  Joyful: {
    min_offset_deg: -15,
    max_offset_deg: 15,
    waggle_period_s: 0.9,
    waggle_interval_s: 0.2,
  },
  Sad: {
    min_offset_deg: -20,
    max_offset_deg: -20,
    waggle_period_s: 1.0,
    waggle_interval_s: 0.0,
  },
  Sleepy: {
    min_offset_deg: -22,
    max_offset_deg: -14,
    waggle_period_s: 5.0,
    waggle_interval_s: 3.0,
  },
  Distressed: {
    min_offset_deg: -6,
    max_offset_deg: 6,
    waggle_period_s: 0.9,
    waggle_interval_s: 0.15,
  },
  Blissed: {
    min_offset_deg: -16,
    max_offset_deg: -4,
    waggle_period_s: 3.0,
    waggle_interval_s: 1.5,
  },
  Depressed: {
    min_offset_deg: 0,
    max_offset_deg: 0,
    waggle_period_s: 1.0,
    waggle_interval_s: 0.0,
  },
  Shocked: {
    min_offset_deg: 0,
    max_offset_deg: 0,
    waggle_period_s: 1.0,
    waggle_interval_s: 0.0,
  },
  Disappointed: {
    min_offset_deg: -15,
    max_offset_deg: -15,
    waggle_period_s: 1.0,
    waggle_interval_s: 0.0,
  },
};

export interface EmotionBlendDeps {
  triangulation: EmotionTriangulationTable;
  paramFields: readonly ParamField[];
  baseTargetForExpression: (name: string) => FaceParams;
}

export interface EmotionBlendApi {
  ready(): boolean;
  findTriangle(v: number, a: number): BlendTriangle | null;
  blendedFaceParams(v: number, a: number): FaceParams | null;
  blendedEmotionArmMotion(v: number, a: number): EmotionArmMotion | null;
}

export function createEmotionBlend(deps: EmotionBlendDeps): EmotionBlendApi {
  const { triangulation: t, paramFields, baseTargetForExpression } = deps;

  function ready(): boolean {
    return !!(t && Array.isArray(t.anchors) && Array.isArray(t.triangles));
  }

  function barycentric(
    v: number,
    a: number,
    i0: number,
    i1: number,
    i2: number,
  ): [number, number, number] | null {
    const A = t.anchors[i0]!;
    const B = t.anchors[i1]!;
    const C = t.anchors[i2]!;
    const denom = (B.a - C.a) * (A.v - C.v) + (C.v - B.v) * (A.a - C.a);
    if (Math.abs(denom) < 1e-12) return null;
    const l0 = ((B.a - C.a) * (v - C.v) + (C.v - B.v) * (a - C.a)) / denom;
    const l1 = ((C.a - A.a) * (v - C.v) + (A.v - C.v) * (a - C.a)) / denom;
    const l2 = 1.0 - l0 - l1;
    return [l0, l1, l2];
  }

  function findTriangle(v: number, a: number): BlendTriangle | null {
    if (!ready()) return null;
    const tris = t.triangles;
    for (let ti = 0; ti < tris.length; ++ti) {
      const tri = tris[ti]!;
      const [i0, i1, i2] = tri;
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
          l0 *= inv;
          l1 *= inv;
          l2 *= inv;
        }
        return { indices: [i0, i1, i2], weights: [l0, l1, l2] };
      }
    }
    return null;
  }

  function presetForEmotion(name: string): FaceParams | null {
    return baseTargetForExpression(name);
  }

  function armPresetForEmotion(name: string): EmotionArmMotion {
    return (
      ARM_PRESETS[name] ?? {
        min_offset_deg: -20,
        max_offset_deg: -15,
        waggle_period_s: 4.0,
        waggle_interval_s: 2.0,
      }
    );
  }

  function blendArmField(
    a: number,
    b: number,
    c: number,
    la: number,
    lb: number,
    lc: number,
  ): number {
    return Math.round(a * la + b * lb + c * lc);
  }

  function blendArmFloat(
    a: number,
    b: number,
    c: number,
    la: number,
    lb: number,
    lc: number,
  ): number {
    return a * la + b * lb + c * lc;
  }

  function blendArmThree(
    Pa: EmotionArmMotion,
    Pb: EmotionArmMotion,
    Pc: EmotionArmMotion,
    la: number,
    lb: number,
    lc: number,
  ): EmotionArmMotion {
    return {
      min_offset_deg: blendArmField(
        Pa.min_offset_deg,
        Pb.min_offset_deg,
        Pc.min_offset_deg,
        la,
        lb,
        lc,
      ),
      max_offset_deg: blendArmField(
        Pa.max_offset_deg,
        Pb.max_offset_deg,
        Pc.max_offset_deg,
        la,
        lb,
        lc,
      ),
      waggle_period_s: blendArmFloat(
        Pa.waggle_period_s,
        Pb.waggle_period_s,
        Pc.waggle_period_s,
        la,
        lb,
        lc,
      ),
      waggle_interval_s: blendArmFloat(
        Pa.waggle_interval_s,
        Pb.waggle_interval_s,
        Pc.waggle_interval_s,
        la,
        lb,
        lc,
      ),
    };
  }

  function normalizeArmMotion(r: EmotionArmMotion): EmotionArmMotion {
    let lo = r.min_offset_deg;
    let hi = r.max_offset_deg;
    let ps = r.waggle_period_s;
    let is = r.waggle_interval_s;
    if (lo > hi) {
      const tmp = lo;
      lo = hi;
      hi = tmp;
    }
    if (ps < 0.05) ps = 0.05;
    if (is < 0) is = 0;
    return {
      min_offset_deg: lo,
      max_offset_deg: hi,
      waggle_period_s: ps,
      waggle_interval_s: is,
    };
  }

  function blendedEmotionArmMotion(
    v: number,
    a: number,
  ): EmotionArmMotion | null {
    if (!ready()) return null;
    v = clampf(v, -1, 1);
    a = clampf(a, 0, 1);
    const tri = findTriangle(v, a);
    if (!tri) {
      const idx = nearestAnchor(v, a);
      const e = t.anchors[idx]!.emotion;
      return normalizeArmMotion(armPresetForEmotion(e));
    }
    const [i0, i1, i2] = tri.indices;
    const [l0, l1, l2] = tri.weights;
    const Pa = armPresetForEmotion(t.anchors[i0]!.emotion);
    const Pb = armPresetForEmotion(t.anchors[i1]!.emotion);
    const Pc = armPresetForEmotion(t.anchors[i2]!.emotion);
    return normalizeArmMotion(blendArmThree(Pa, Pb, Pc, l0, l1, l2));
  }

  function blendField(
    a: number,
    b: number,
    c: number,
    la: number,
    lb: number,
    lc: number,
  ): number {
    return Math.round(a * la + b * lb + c * lc);
  }

  function blendThree(
    A: FaceParams,
    B: FaceParams,
    C: FaceParams,
    la: number,
    lb: number,
    lc: number,
  ): FaceParams {
    const out = {} as FaceParams;
    for (const k of paramFields) {
      out[k] = blendField(A[k] | 0, B[k] | 0, C[k] | 0, la, lb, lc);
    }
    return out;
  }

  function nearestAnchor(v: number, a: number): number {
    let bestD = Infinity;
    let best = 0;
    for (let i = 0; i < t.anchors.length; ++i) {
      const an = t.anchors[i]!;
      const dv = v - an.v;
      const da = a - an.a;
      const d = dv * dv + da * da;
      if (d < bestD) {
        bestD = d;
        best = i;
      }
    }
    return best;
  }

  function blendedFaceParams(v: number, a: number): FaceParams | null {
    if (!ready()) return null;
    v = clampf(v, -1, 1);
    a = clampf(a, 0, 1);
    const tri = findTriangle(v, a);
    if (!tri) {
      const idx = nearestAnchor(v, a);
      const e = t.anchors[idx]!.emotion;
      const p = presetForEmotion(e);
      return p ? { ...p } : null;
    }
    const [i0, i1, i2] = tri.indices;
    const [l0, l1, l2] = tri.weights;
    const A = presetForEmotion(t.anchors[i0]!.emotion);
    const B = presetForEmotion(t.anchors[i1]!.emotion);
    const C = presetForEmotion(t.anchors[i2]!.emotion);
    if (!A || !B || !C) return null;
    return blendThree(A, B, C, l0, l1, l2);
  }

  return {
    ready,
    findTriangle,
    blendedFaceParams,
    blendedEmotionArmMotion,
  };
}
