import type { KeyframeOverride, VerbKeyframe, VerbTimeline } from "./FACE_CONFIG_DATA";
import { kVerbTimelines } from "./FACE_CONFIG_DATA";

/** Editor session copy — same shape as `VerbTimeline` but deep-mutable arrays. */
export type MutableKeyframeOverride = {
    field: KeyframeOverride["field"];
    targetValue: number;
    strength: number;
};

export type MutableVerbKeyframe = {
    time_ms: number;
    override_count: number;
    overrides: MutableKeyframeOverride[];
};

export type MutableVerbTimeline = {
    /** `Face::Expression` discriminant (expression table index). */
    verb: number;
    loop_duration_ms: number;
    keyframe_count: number;
    keyframes: MutableVerbKeyframe[];
};

function cloneOverride(o: KeyframeOverride): MutableKeyframeOverride {
    return { field: o.field, targetValue: o.targetValue, strength: o.strength };
}

function cloneKeyframe(k: VerbKeyframe): MutableVerbKeyframe {
    return {
        time_ms: k.time_ms,
        override_count: k.override_count,
        overrides: k.overrides.map(cloneOverride),
    };
}

/** Deep clone of shipped `kVerbTimelines` for editor-local mutation (lost on refresh). */
export function cloneMutableVerbTimelines(): MutableVerbTimeline[] {
    return kVerbTimelines.map(tab => ({
        verb: tab.verb,
        loop_duration_ms: tab.loop_duration_ms,
        keyframe_count: tab.keyframe_count,
        keyframes: tab.keyframes.map(cloneKeyframe),
    }));
}
