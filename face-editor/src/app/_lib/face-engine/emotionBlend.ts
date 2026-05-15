/**
 * Port of `robot_v3/src/behaviour/EmotionBlend.cpp` — barycentric blend using
 * `kBaseTargets`, `kArmPresets`, `kIdleAnim` from `FACE_CONFIG_DATA.ts`.
 */

import {
    type ArmPreset,
    type IdleAnimRow,
    type ParamI16,
    FieldIndex,
    GazeStyle,
    BOB_AMP_FOLLOW_EMOTION_ARM,
    kArmPresets,
    kBaseTargets,
    kIdleAnim,
    expressionIndexFromName,
} from "./FACE_CONFIG_DATA";
import type { FaceParams } from "./faceParams";
import { PARAM_FIELDS, faceParamsFromIndexed } from "./faceParams";
import type { BlendTriangle, EmotionArmMotion, EmotionTriangulationTable } from "./types";

const BARY_EPS = 1e-5;

function clampf(x: number, lo: number, hi: number): number {
    return x < lo ? lo : x > hi ? hi : x;
}

function blendParam3(
    a: ParamI16,
    b: ParamI16,
    c: ParamI16,
    la: number,
    lb: number,
    lc: number
): ParamI16 {
    const wa = la * a.strength;
    const wb = lb * b.strength;
    const wc = lc * c.strength;
    const W = wa + wb + wc;
    if (W > 1e-6) {
        const value = Math.round((wa * a.value + wb * b.value + wc * c.value) / W);
        let strength = Math.round(la * a.strength + lb * b.strength + lc * c.strength);
        if (strength > 100) strength = 100;
        return { value, strength };
    }
    const value = Math.round(la * a.value + lb * b.value + lc * c.value);
    return { value, strength: 0 };
}

function blendFloat(a: number, b: number, c: number, la: number, lb: number, lc: number): number {
    return a * la + b * lb + c * lc;
}

function blendArmThree(
    A: ArmPreset,
    B: ArmPreset,
    C: ArmPreset,
    la: number,
    lb: number,
    lc: number
): EmotionArmMotion {
    const blendI = (a: number, b: number, c: number, la2: number, lb2: number, lc2: number) =>
        Math.round(a * la2 + b * lb2 + c * lc2);
    return {
        min_offset_deg: blendI(A.min_deg, B.min_deg, C.min_deg, la, lb, lc),
        max_offset_deg: blendI(A.max_deg, B.max_deg, C.max_deg, la, lb, lc),
        waggle_period_s: blendFloat(A.period_s, B.period_s, C.period_s, la, lb, lc),
        waggle_interval_s: blendFloat(A.interval_s, B.interval_s, C.interval_s, la, lb, lc),
    };
}

function winningGazeStyle(
    ga: GazeStyle,
    gb: GazeStyle,
    gc: GazeStyle,
    la: number,
    lb: number,
    lc: number
): GazeStyle {
    if (la >= lb && la >= lc) return ga;
    if (lb >= lc) return gb;
    return gc;
}

function blendIdleThree(
    A: IdleAnimRow,
    B: IdleAnimRow,
    C: IdleAnimRow,
    la: number,
    lb: number,
    lc: number
): IdleAnimRow {
    const r: IdleAnimRow = {
        blink_period_min_ms: Math.round(
            blendFloat(
                A.blink_period_min_ms,
                B.blink_period_min_ms,
                C.blink_period_min_ms,
                la,
                lb,
                lc
            )
        ),
        blink_period_max_ms: Math.round(
            blendFloat(
                A.blink_period_max_ms,
                B.blink_period_max_ms,
                C.blink_period_max_ms,
                la,
                lb,
                lc
            )
        ),
        blink_close_ms: Math.round(
            blendFloat(A.blink_close_ms, B.blink_close_ms, C.blink_close_ms, la, lb, lc)
        ),
        blink_open_ms: Math.round(
            blendFloat(A.blink_open_ms, B.blink_open_ms, C.blink_open_ms, la, lb, lc)
        ),
        bob_amplitude_px: 0,
        gaze_style: GazeStyle.Off,
        gaze_move_ms: 0,
        gaze_rand_span_x: 0,
        gaze_rand_span_y: 0,
        gaze_reroll_min_ms: 0,
        gaze_reroll_max_ms: 0,
        gaze_scan_period_ms: 0,
        gaze_amp_x: 0,
        gaze_amp_y: 0,
    };

    const allBobHeur =
        A.bob_amplitude_px === BOB_AMP_FOLLOW_EMOTION_ARM &&
        B.bob_amplitude_px === BOB_AMP_FOLLOW_EMOTION_ARM &&
        C.bob_amplitude_px === BOB_AMP_FOLLOW_EMOTION_ARM;
    if (allBobHeur) {
        r.bob_amplitude_px = BOB_AMP_FOLLOW_EMOTION_ARM;
    } else {
        const nz = (x: number) => (x === BOB_AMP_FOLLOW_EMOTION_ARM ? 0.0 : x);
        r.bob_amplitude_px = Math.round(
            blendFloat(
                nz(A.bob_amplitude_px),
                nz(B.bob_amplitude_px),
                nz(C.bob_amplitude_px),
                la,
                lb,
                lc
            )
        );
    }
    r.gaze_style = winningGazeStyle(A.gaze_style, B.gaze_style, C.gaze_style, la, lb, lc);
    r.gaze_move_ms = Math.round(
        blendFloat(A.gaze_move_ms, B.gaze_move_ms, C.gaze_move_ms, la, lb, lc)
    );
    r.gaze_rand_span_x = Math.round(
        blendFloat(A.gaze_rand_span_x, B.gaze_rand_span_x, C.gaze_rand_span_x, la, lb, lc)
    );
    r.gaze_rand_span_y = Math.round(
        blendFloat(A.gaze_rand_span_y, B.gaze_rand_span_y, C.gaze_rand_span_y, la, lb, lc)
    );
    r.gaze_reroll_min_ms = Math.round(
        blendFloat(A.gaze_reroll_min_ms, B.gaze_reroll_min_ms, C.gaze_reroll_min_ms, la, lb, lc)
    );
    r.gaze_reroll_max_ms = Math.round(
        blendFloat(A.gaze_reroll_max_ms, B.gaze_reroll_max_ms, C.gaze_reroll_max_ms, la, lb, lc)
    );
    r.gaze_scan_period_ms = Math.round(
        blendFloat(A.gaze_scan_period_ms, B.gaze_scan_period_ms, C.gaze_scan_period_ms, la, lb, lc)
    );
    r.gaze_amp_x = Math.round(blendFloat(A.gaze_amp_x, B.gaze_amp_x, C.gaze_amp_x, la, lb, lc));
    r.gaze_amp_y = Math.round(blendFloat(A.gaze_amp_y, B.gaze_amp_y, C.gaze_amp_y, la, lb, lc));
    if (r.blink_period_max_ms < r.blink_period_min_ms) {
        r.blink_period_max_ms = r.blink_period_min_ms;
    }
    return r;
}

export interface EmotionBlendDeps {
    triangulation: EmotionTriangulationTable;
    /** When set, blend geometry rows come from this table (same shape as `kBaseTargets`). */
    baseTargets?: readonly (readonly ParamI16[])[];
}

export interface EmotionBlendApi {
    ready(): boolean;
    findTriangle(v: number, a: number): BlendTriangle | null;
    blendedFaceParams(v: number, a: number): FaceParams | null;
    blendedFaceParamsIndexed(v: number, a: number): ParamI16[] | null;
    blendedEmotionArmMotion(v: number, a: number): EmotionArmMotion | null;
    blendedIdleAnim(v: number, a: number): IdleAnimRow | null;
}

export function createEmotionBlend(deps: EmotionBlendDeps): EmotionBlendApi {
    const { triangulation: t } = deps;
    const baseTable = deps.baseTargets ?? kBaseTargets;

    function ready(): boolean {
        return !!(t && Array.isArray(t.anchors) && Array.isArray(t.triangles));
    }

    function barycentric(
        v: number,
        a: number,
        i0: number,
        i1: number,
        i2: number
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

    function baseRowForAnchorEmotion(emotion: string): readonly ParamI16[] {
        const idx = expressionIndexFromName(emotion);
        if (idx < 0 || idx >= baseTable.length) return kBaseTargets[0]!;
        return baseTable[idx]!;
    }

    function armPresetForExpressionIndex(exIdx: number): ArmPreset {
        if (exIdx < 0 || exIdx >= kArmPresets.length) return kArmPresets[0]!;
        return kArmPresets[exIdx]!;
    }

    function blendThreeIndexed(
        A: readonly ParamI16[],
        B: readonly ParamI16[],
        C: readonly ParamI16[],
        la: number,
        lb: number,
        lc: number
    ): ParamI16[] {
        const out: ParamI16[] = [];
        for (let i = 0; i < FieldIndex.Count; ++i) {
            out.push(blendParam3(A[i]!, B[i]!, C[i]!, la, lb, lc));
        }
        return out;
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

    function blendedFaceParamsIndexed(v: number, a: number): ParamI16[] | null {
        if (!ready()) return null;
        v = clampf(v, -1, 1);
        a = clampf(a, 0, 1);
        const tri = findTriangle(v, a);
        if (!tri) {
            const idx = nearestAnchor(v, a);
            const e = t.anchors[idx]!.emotion;
            return [...baseRowForAnchorEmotion(e)];
        }
        const [i0, i1, i2] = tri.indices;
        const [l0, l1, l2] = tri.weights;
        const eA = t.anchors[i0]!.emotion;
        const eB = t.anchors[i1]!.emotion;
        const eC = t.anchors[i2]!.emotion;
        return blendThreeIndexed(
            baseRowForAnchorEmotion(eA),
            baseRowForAnchorEmotion(eB),
            baseRowForAnchorEmotion(eC),
            l0,
            l1,
            l2
        );
    }

    function blendedEmotionArmMotion(v: number, a: number): EmotionArmMotion | null {
        if (!ready()) return null;
        v = clampf(v, -1, 1);
        a = clampf(a, 0, 1);
        const tri = findTriangle(v, a);
        if (!tri) {
            const idx = nearestAnchor(v, a);
            const e = t.anchors[idx]!.emotion;
            const ex = expressionIndexFromName(e);
            const p = armPresetForExpressionIndex(ex);
            const one: EmotionArmMotion = {
                min_offset_deg: p.min_deg,
                max_offset_deg: p.max_deg,
                waggle_period_s: p.period_s < 0.05 ? 0.05 : p.period_s,
                waggle_interval_s: p.interval_s < 0 ? 0 : p.interval_s,
            };
            if (one.min_offset_deg > one.max_offset_deg) {
                const tmp = one.min_offset_deg;
                one.min_offset_deg = one.max_offset_deg;
                one.max_offset_deg = tmp;
            }
            return normalizeArmMotion(one);
        }
        const [i0, i1, i2] = tri.indices;
        const [l0, l1, l2] = tri.weights;
        const eA = expressionIndexFromName(t.anchors[i0]!.emotion);
        const eB = expressionIndexFromName(t.anchors[i1]!.emotion);
        const eC = expressionIndexFromName(t.anchors[i2]!.emotion);
        let r = blendArmThree(
            armPresetForExpressionIndex(eA),
            armPresetForExpressionIndex(eB),
            armPresetForExpressionIndex(eC),
            l0,
            l1,
            l2
        );
        if (r.min_offset_deg > r.max_offset_deg) {
            const tmp = r.min_offset_deg;
            r.min_offset_deg = r.max_offset_deg;
            r.max_offset_deg = tmp;
        }
        if (r.waggle_period_s < 0.05) r.waggle_period_s = 0.05;
        if (r.waggle_interval_s < 0) r.waggle_interval_s = 0;
        return r;
    }

    function blendedIdleAnim(v: number, a: number): IdleAnimRow | null {
        if (!ready()) return null;
        v = clampf(v, -1, 1);
        a = clampf(a, 0, 1);
        const tri = findTriangle(v, a);
        if (!tri) {
            const idx = nearestAnchor(v, a);
            const e = t.anchors[idx]!.emotion;
            const ex = expressionIndexFromName(e);
            return { ...kIdleAnim[ex]! };
        }
        const [i0, i1, i2] = tri.indices;
        const [l0, l1, l2] = tri.weights;
        const eA = expressionIndexFromName(t.anchors[i0]!.emotion);
        const eB = expressionIndexFromName(t.anchors[i1]!.emotion);
        const eC = expressionIndexFromName(t.anchors[i2]!.emotion);
        return blendIdleThree(kIdleAnim[eA]!, kIdleAnim[eB]!, kIdleAnim[eC]!, l0, l1, l2);
    }

    function blendedFaceParams(v: number, a: number): FaceParams | null {
        const row = blendedFaceParamsIndexed(v, a);
        return row ? faceParamsFromIndexed(row) : null;
    }

    return {
        ready,
        findTriangle,
        blendedFaceParams,
        blendedFaceParamsIndexed,
        blendedEmotionArmMotion,
        blendedIdleAnim,
    };
}
