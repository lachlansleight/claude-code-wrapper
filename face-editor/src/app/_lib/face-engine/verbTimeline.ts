/**
 * Port of `robot_v3/src/face/VerbTimeline.cpp` + `kVerbTransitionDurMs` from `VerbTimeline.h`.
 */

import {
    Expression,
    FieldIndex,
    type ParamI16,
    kVerbKeyframeOverridesMax,
    kVerbTimelines,
    kVerbTransitionDurMs,
} from "./FACE_CONFIG_DATA";

const FIELD_COUNT = FieldIndex.Count;

function clamp01f(x: number): number {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

function tableFor(verb: Expression): (typeof kVerbTimelines)[number] | undefined {
    for (const tab of kVerbTimelines) {
        if (tab.verb === verb) return tab;
    }
    return undefined;
}

/** Minimal shape for sampling (shipped `VerbTimeline` or editor `MutableVerbTimeline`). */
export type VerbTimelineSampleSource = {
    loop_duration_ms: number;
    keyframe_count: number;
    keyframes: readonly {
        time_ms: number;
        override_count: number;
        overrides: readonly {
            field: FieldIndex;
            targetValue: number;
            strength: number;
        }[];
    }[];
};

export type VerbTimelineTableResolver = (verb: Expression) => VerbTimelineSampleSource | undefined;

/** True when `kVerbTimelines` contains a row for this expression index. */
export function expressionUsesVerbTimeline(e: Expression): boolean {
    return kVerbTimelines.some(tab => tab.verb === e);
}

/** @deprecated Use `expressionUsesVerbTimeline` (name aligned with firmware). */
export const isVerbExpression = expressionUsesVerbTimeline;

function cumulativeState(
    tab: VerbTimelineSampleSource,
    uptoIdx: number,
    hasField: boolean[],
    fieldVals: ParamI16[]
): void {
    for (let i = 0; i < FIELD_COUNT; ++i) {
        hasField[i] = false;
        fieldVals[i] = { value: 0, strength: 0 };
    }
    if (uptoIdx < 0 || uptoIdx >= tab.keyframe_count) return;
    for (let j = 0; j <= uptoIdx; j++) {
        const kf = tab.keyframes[j]!;
        const n = kf.override_count;
        for (let i = 0; i < n && i < kVerbKeyframeOverridesMax; i++) {
            const o = kf.overrides[i]!;
            const fi = o.field;
            if (fi >= FIELD_COUNT) continue;
            if (o.strength === 0) {
                hasField[fi] = false;
            } else {
                hasField[fi] = true;
                fieldVals[fi] = { value: o.targetValue, strength: o.strength };
            }
        }
    }
}

function lerpFieldSnapshots(
    has0: boolean[],
    v0: ParamI16[],
    has1: boolean[],
    v1: ParamI16[],
    u: number,
    outHas: boolean[],
    outVals: ParamI16[]
): void {
    const om = 1 - u;
    for (let fi = 0; fi < FIELD_COUNT; fi++) {
        const h0 = has0[fi]!;
        const h1 = has1[fi]!;
        if (!h0 && !h1) {
            outHas[fi] = false;
            outVals[fi] = { value: 0, strength: 0 };
        } else if (h0 && h1) {
            outHas[fi] = true;
            outVals[fi] = {
                value: Math.round(v0[fi]!.value * om + v1[fi]!.value * u),
                strength: Math.round(v0[fi]!.strength * om + v1[fi]!.strength * u),
            };
        } else if (h0) {
            const s = Math.round(v0[fi]!.strength * om);
            outHas[fi] = s > 0;
            outVals[fi] = {
                value: v0[fi]!.value,
                strength: s < 0 ? 0 : s,
            };
        } else {
            const s = Math.round(v1[fi]!.strength * u);
            outHas[fi] = s > 0;
            outVals[fi] = {
                value: v1[fi]!.value,
                strength: s < 0 ? 0 : s,
            };
        }
    }
}

export function sampleVerbTimelineFromTable(
    tab: VerbTimelineSampleSource,
    timeInVerbMs: number,
    hasField: boolean[],
    fieldVals: ParamI16[]
): void {
    for (let i = 0; i < FIELD_COUNT; ++i) {
        hasField[i] = false;
        fieldVals[i] = { value: 0, strength: 0 };
    }

    const K = tab.keyframe_count;
    if (K === 0) return;

    const L = tab.loop_duration_ms;

    if (K === 1) {
        cumulativeState(tab, 0, hasField, fieldVals);
        return;
    }
    if (L === 0) {
        cumulativeState(tab, K - 1, hasField, fieldVals);
        return;
    }

    const tMod = timeInVerbMs % L;
    const kfs = tab.keyframes;

    let i0 = 0;
    let i1 = 0;
    let u = 0;

    if (tMod >= kfs[K - 1]!.time_ms) {
        i0 = K - 1;
        i1 = 0;
        const span = L - kfs[K - 1]!.time_ms + kfs[0]!.time_ms;
        u = span > 0 ? (tMod - kfs[K - 1]!.time_ms) / span : 0;
    } else {
        let seg = 0;
        for (; seg + 1 < K; seg++) {
            if (tMod < kfs[seg + 1]!.time_ms) break;
        }
        i0 = seg;
        i1 = seg + 1;
        const t0 = kfs[i0]!.time_ms;
        const t1 = kfs[i1]!.time_ms;
        const dt = t1 - t0;
        u = dt > 0 ? (tMod - t0) / dt : 0;
    }

    const leftHas = new Array(FIELD_COUNT).fill(false);
    const leftVals: ParamI16[] = Array.from({ length: FIELD_COUNT }, () => ({
        value: 0,
        strength: 0,
    }));
    const rightHas = new Array(FIELD_COUNT).fill(false);
    const rightVals: ParamI16[] = Array.from({ length: FIELD_COUNT }, () => ({
        value: 0,
        strength: 0,
    }));
    cumulativeState(tab, i0, leftHas, leftVals);
    cumulativeState(tab, i1, rightHas, rightVals);
    lerpFieldSnapshots(leftHas, leftVals, rightHas, rightVals, u, hasField, fieldVals);
}

export function sampleVerbTimeline(
    verb: Expression,
    timeInVerbMs: number,
    hasField: boolean[],
    fieldVals: ParamI16[]
): void {
    const tab = tableFor(verb);
    if (!tab) return;
    sampleVerbTimelineFromTable(tab, timeInVerbMs, hasField, fieldVals);
}

interface Snapshot {
    has: boolean[];
    value: Int16Array;
    strength: Uint8Array;
}

const sFromSnapshot: Snapshot = {
    has: new Array(FIELD_COUNT).fill(false),
    value: new Int16Array(FIELD_COUNT),
    strength: new Uint8Array(FIELD_COUNT),
};
let sToVerb: Expression = Expression.Count;
let sTransitionStartMs = 0;
let sFromInitialised = false;

function resolveTabForVerb(
    verb: Expression,
    resolveTable?: VerbTimelineTableResolver
): VerbTimelineSampleSource | undefined {
    return resolveTable?.(verb) ?? tableFor(verb);
}

function evaluate(
    nowMs: number,
    timeInVerbMs: number,
    outHas: boolean[],
    outVals: ParamI16[],
    resolveTable?: VerbTimelineTableResolver
): void {
    const toHas = new Array(FIELD_COUNT).fill(false);
    const toVals: ParamI16[] = Array.from({ length: FIELD_COUNT }, () => ({
        value: 0,
        strength: 0,
    }));

    if (expressionUsesVerbTimeline(sToVerb)) {
        const tab = resolveTabForVerb(sToVerb, resolveTable);
        if (tab) {
            sampleVerbTimelineFromTable(tab, timeInVerbMs, toHas, toVals);
        }
    }

    const elapsed = nowMs - sTransitionStartMs;
    const t = clamp01f(elapsed / kVerbTransitionDurMs);

    if (!sFromInitialised || t >= 1.0) {
        for (let i = 0; i < FIELD_COUNT; ++i) {
            outHas[i] = toHas[i]!;
            outVals[i] = { ...toVals[i]! };
        }
        return;
    }

    const oneMinus = 1.0 - t;
    for (let i = 0; i < FIELD_COUNT; ++i) {
        const fromHas = sFromSnapshot.has[i];
        const nextHas = toHas[i];
        if (fromHas && nextHas) {
            const v = sFromSnapshot.value[i]! * oneMinus + toVals[i]!.value * t;
            const s = sFromSnapshot.strength[i]! * oneMinus + toVals[i]!.strength * t;
            outHas[i] = true;
            outVals[i] = {
                value: Math.round(v),
                strength: Math.round(s),
            };
        } else if (fromHas) {
            const s = Math.round(sFromSnapshot.strength[i]! * oneMinus);
            outHas[i] = s > 0;
            outVals[i] = {
                value: sFromSnapshot.value[i]!,
                strength: s < 0 ? 0 : s,
            };
        } else if (nextHas) {
            const s = Math.round(toVals[i]!.strength * t);
            outHas[i] = s > 0;
            outVals[i] = {
                value: toVals[i]!.value,
                strength: s < 0 ? 0 : s,
            };
        } else {
            outHas[i] = false;
            outVals[i] = { value: 0, strength: 0 };
        }
    }
}

export function sampleEffectiveVerb(
    currentVerbExpression: Expression,
    nowMs: number,
    timeInVerbMs: number,
    hasField: boolean[],
    fieldVals: ParamI16[],
    resolveTable?: VerbTimelineTableResolver
): void {
    if (currentVerbExpression !== sToVerb) {
        const tmpHas = new Array(FIELD_COUNT).fill(false);
        const tmpVals: ParamI16[] = Array.from({ length: FIELD_COUNT }, () => ({
            value: 0,
            strength: 0,
        }));
        evaluate(nowMs, timeInVerbMs, tmpHas, tmpVals, resolveTable);
        for (let i = 0; i < FIELD_COUNT; ++i) {
            sFromSnapshot.has[i] = tmpHas[i]!;
            sFromSnapshot.value[i] = tmpVals[i]!.value;
            sFromSnapshot.strength[i] = tmpVals[i]!.strength;
        }
        sFromInitialised = true;
        sToVerb = currentVerbExpression;
        sTransitionStartMs = nowMs;
    }

    evaluate(nowMs, timeInVerbMs, hasField, fieldVals, resolveTable);
}

export function resetVerbTransition(): void {
    sFromSnapshot.has.fill(false);
    sFromSnapshot.value.fill(0);
    sFromSnapshot.strength.fill(0);
    sToVerb = Expression.Count;
    sTransitionStartMs = 0;
    sFromInitialised = false;
}

export function verbTransitionT(nowMs: number): number {
    if (!sFromInitialised) return 1.0;
    const elapsed = nowMs - sTransitionStartMs;
    return clamp01f(elapsed / kVerbTransitionDurMs);
}
