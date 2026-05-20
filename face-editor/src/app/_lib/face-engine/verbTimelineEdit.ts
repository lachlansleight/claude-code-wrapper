import {
    Expression,
    FieldIndex,
    kVerbKeyframeOverridesMax,
    kVerbKeyframesMax,
} from "./FACE_CONFIG_DATA";
import { PARAM_FIELDS, paramFieldFromFieldIndex, type FaceParams } from "./faceParams";
import type { MutableVerbKeyframe, MutableVerbTimeline } from "./mutableVerbTimelines";

/** Quantize verb playhead / keyframe authoring to render ticks (matches typical frame step). */
export const VERB_PLAYHEAD_QUANT_MS = 60;

export function verbPlayheadMaxMs(loop_duration_ms: number): number {
    return Math.max(loop_duration_ms, 1000);
}

export function snapVerbPlayheadMs(ms: number, loop_duration_ms: number): number {
    const maxMs = verbPlayheadMaxMs(loop_duration_ms);
    const q = VERB_PLAYHEAD_QUANT_MS;
    const cap = Math.floor(maxMs / q) * q;
    const s = Math.round(ms / q) * q;
    return Math.max(0, Math.min(cap, s));
}

/** Upper bound for editor loop length (1 hour). */
const LOOP_DURATION_MS_MAX = 3_600_000;

/**
 * Set `tab.loop_duration_ms`, snapped to `VERB_PLAYHEAD_QUANT_MS`.
 * With two or more keyframes, loop is forced strictly past the last keyframe time
 * so `sampleVerbTimelineFromTable` wrap segments stay valid.
 */
export function applyVerbLoopDurationMs(tab: MutableVerbTimeline, loopMsRaw: number): number {
    const q = VERB_PLAYHEAD_QUANT_MS;
    let L = Math.round(loopMsRaw / q) * q;
    if (!Number.isFinite(L) || L < q) L = q;
    if (L > LOOP_DURATION_MS_MAX) L = Math.floor(LOOP_DURATION_MS_MAX / q) * q;
    if (tab.keyframe_count > 1) {
        const last = tab.keyframes[tab.keyframe_count - 1]?.time_ms ?? 0;
        const minL = Math.max(q, Math.ceil((last + 1) / q) * q);
        if (L < minL) L = minL;
    }
    tab.loop_duration_ms = L;
    return L;
}

export function findLiveVerbTimeline(
    tables: readonly MutableVerbTimeline[],
    verb: Expression
): MutableVerbTimeline | undefined {
    return tables.find(t => t.verb === verb);
}

export function findKeyframeIndexAtTime(tab: MutableVerbTimeline, timeMs: number): number | null {
    const loop = tab.loop_duration_ms;
    const target = snapVerbPlayheadMs(timeMs, loop);
    for (let i = 0; i < tab.keyframe_count; i++) {
        const kf = tab.keyframes[i];
        if (!kf) continue;
        if (snapVerbPlayheadMs(kf.time_ms, loop) === target) return i;
    }
    return null;
}

/** Same keyframe index priority as `applySliderToVerbTimeline` (playhead, then selection). */
export function resolveVerbSliderKeyframeIndex(
    tab: MutableVerbTimeline,
    playheadMs: number,
    selectedKeyframeIndex: number | null
): number | null {
    const atPlayhead = findKeyframeIndexAtTime(tab, playheadMs);
    if (atPlayhead !== null) return atPlayhead;
    if (selectedKeyframeIndex !== null) {
        const s = selectedKeyframeIndex;
        if (s >= 0 && s < tab.keyframe_count) return s;
    }
    return null;
}

function zeroFaceParams(): FaceParams {
    const z = {} as FaceParams;
    for (const f of PARAM_FIELDS) z[f] = 0;
    return z;
}

/** Per-field override strengths on the given keyframe (0 when no override row). */
export function keyframeStrengthFaceParams(
    tab: MutableVerbTimeline,
    keyframeIndex: number | null
): FaceParams {
    const out = zeroFaceParams();
    if (keyframeIndex === null) return out;
    const kf = tab.keyframes[keyframeIndex];
    if (!kf) return out;
    const n = Math.min(kf.override_count, kf.overrides.length);
    for (let i = 0; i < n; i++) {
        const o = kf.overrides[i]!;
        const pf = paramFieldFromFieldIndex(o.field);
        if (!pf) continue;
        let s = Math.round(o.strength);
        if (s < 0) s = 0;
        if (s > 100) s = 100;
        out[pf] = s;
    }
    return out;
}

/**
 * Paused inspector values: `fallback` for fields without an active override on the
 * keyframe, `targetValue` for fields that have one (strength &gt; 0).
 */
export function keyframeValueFaceParams(
    tab: MutableVerbTimeline,
    keyframeIndex: number | null,
    fallback: FaceParams
): FaceParams {
    const out = { ...fallback };
    if (keyframeIndex === null) return out;
    const kf = tab.keyframes[keyframeIndex];
    if (!kf) return out;
    const n = Math.min(kf.override_count, kf.overrides.length);
    for (let i = 0; i < n; i++) {
        const o = kf.overrides[i]!;
        if (o.strength <= 0) continue;
        const pf = paramFieldFromFieldIndex(o.field);
        if (!pf) continue;
        out[pf] = o.targetValue;
    }
    return out;
}

function sortKeyframes(tab: MutableVerbTimeline): void {
    tab.keyframes.sort((a, b) => a.time_ms - b.time_ms);
    tab.keyframe_count = tab.keyframes.length;
}

function upsertOverrideInKeyframe(
    kf: MutableVerbKeyframe,
    field: FieldIndex,
    targetValue: number,
    strength: number
): void {
    const o = kf.overrides;
    for (let i = 0; i < o.length; i++) {
        if (o[i]!.field === field) {
            o[i]!.targetValue = targetValue;
            o[i]!.strength = strength;
            kf.override_count = Math.min(o.length, kVerbKeyframeOverridesMax);
            return;
        }
    }
    if (o.length >= kVerbKeyframeOverridesMax) return;
    o.push({ field, targetValue, strength });
    kf.override_count = o.length;
}

/**
 * Slider mutation: keyframe at playhead → edit it; else selected index → edit that;
 * else new keyframe at playhead.
 */
export function applySliderToVerbTimeline(
    tab: MutableVerbTimeline,
    opts: {
        playheadMs: number;
        selectedKeyframeIndex: number | null;
        field: FieldIndex;
        targetValue: number;
        strength?: number;
    }
): void {
    const strength = opts.strength ?? 100;
    const idx = resolveVerbSliderKeyframeIndex(tab, opts.playheadMs, opts.selectedKeyframeIndex);

    if (idx !== null) {
        const kf = tab.keyframes[idx]!;
        upsertOverrideInKeyframe(kf, opts.field, opts.targetValue, strength);
        return;
    }

    if (tab.keyframe_count >= kVerbKeyframesMax) return;

    const time_ms = snapVerbPlayheadMs(opts.playheadMs, tab.loop_duration_ms);
    tab.keyframes.push({
        time_ms,
        override_count: 1,
        overrides: [{ field: opts.field, targetValue: opts.targetValue, strength }],
    });
    sortKeyframes(tab);
}

/**
 * Edit override strength (0–100) on the resolved keyframe; keeps existing
 * `targetValue` when an override exists, otherwise uses `fallbackTargetValue`
 * (typically the sampled face at the playhead).
 */
export function applyStrengthSliderToVerbTimeline(
    tab: MutableVerbTimeline,
    opts: {
        playheadMs: number;
        selectedKeyframeIndex: number | null;
        field: FieldIndex;
        strength: number;
        fallbackTargetValue: number;
    }
): void {
    let s = Math.round(opts.strength);
    if (s < 0) s = 0;
    if (s > 100) s = 100;

    const idx = resolveVerbSliderKeyframeIndex(tab, opts.playheadMs, opts.selectedKeyframeIndex);

    if (idx !== null) {
        const kf = tab.keyframes[idx]!;
        const existing = kf.overrides.find(x => x.field === opts.field);
        const targetValue = existing ? existing.targetValue : Math.round(opts.fallbackTargetValue);
        upsertOverrideInKeyframe(kf, opts.field, targetValue, s);
        return;
    }

    if (s === 0) return;
    if (tab.keyframe_count >= kVerbKeyframesMax) return;

    const time_ms = snapVerbPlayheadMs(opts.playheadMs, tab.loop_duration_ms);
    tab.keyframes.push({
        time_ms,
        override_count: 1,
        overrides: [
            {
                field: opts.field,
                targetValue: Math.round(opts.fallbackTargetValue),
                strength: s,
            },
        ],
    });
    sortKeyframes(tab);
}

/** Fields that have an explicit override row on the given keyframe (for green UI). */
export function fieldsInKeyframeOverrides(
    tab: MutableVerbTimeline,
    keyframeIndex: number | null
): FieldIndex[] {
    const out: FieldIndex[] = [];
    if (keyframeIndex === null) return out;
    const kf = tab.keyframes[keyframeIndex];
    if (!kf) return out;
    const n = Math.min(kf.override_count, kf.overrides.length);
    for (let i = 0; i < n; i++) {
        const o = kf.overrides[i];
        if (o && o.strength > 0) out.push(o.field);
    }
    return out;
}

/** Drop one field's override from a keyframe; removes the keyframe if it has no overrides left. */
export function removeOverrideFromKeyframe(
    tab: MutableVerbTimeline,
    keyframeIndex: number,
    field: FieldIndex
): void {
    if (keyframeIndex < 0 || keyframeIndex >= tab.keyframe_count) return;
    const kf = tab.keyframes[keyframeIndex];
    if (!kf) return;
    const next = kf.overrides.filter(x => x.field !== field);
    kf.overrides = next;
    kf.override_count = next.length;
    if (kf.override_count === 0) {
        tab.keyframes.splice(keyframeIndex, 1);
        sortKeyframes(tab);
    }
}
