import {
    buildEmotionTriangulationFromPoints,
    cloneMutableEmotionTriangulation,
} from "./emotionTriangulationLive";
import type { FaceConfigState } from "./faceConfigState";
import {
    BOB_AMP_FOLLOW_EMOTION_ARM,
    kVerbKeyframeOverridesMax,
    kVerbKeyframesMax,
    type IdleAnimRow,
    type ParamI16,
} from "./faceConfigTypes";
import { FieldIndex, P } from "./faceConfigTypes";
import { ensureSystemVerbTimelines } from "./faceConfigMutations";
import { migrateFaceConfigToSchemaV3 } from "../face-config-codegen/migrateSchemaV3";
import { cloneMutableVerbTimelines, type MutableVerbTimeline } from "./mutableVerbTimelines";

const FIELD_COUNT = FieldIndex.Count;

function cloneBaseTargets(src: readonly (readonly ParamI16[])[]): ParamI16[][] {
    return src.map(row => {
        const cells = row.map(c => ({ value: c.value, strength: c.strength }));
        if (cells.length >= FIELD_COUNT) return cells.slice(0, FIELD_COUNT);
        const pad = Array.from({ length: FIELD_COUNT - cells.length }, () => P(0, 0));
        return [...cells, ...pad];
    });
}

function cloneIdleAnim(src: readonly IdleAnimRow[]): FaceConfigState["idleAnim"] {
    return src.map(r => ({ ...r }));
}

/** Deep clone of shipped tables into a mutable `FaceConfigState`. */
export function cloneFaceConfigState(src: FaceConfigState): FaceConfigState {
    return migrateFaceConfigToSchemaV3({
        schemaVersion: src.schemaVersion,
        expressions: [...src.expressions],
        expressionIsEmotion: [...src.expressionIsEmotion],
        emotionNames: [...src.emotionNames],
        emotionPoints: src.emotionPoints.map(p => ({ ...p })),
        pickOrderIndices: [...src.pickOrderIndices],
        namedEmotionToExpressionIndex: [...src.namedEmotionToExpressionIndex],
        baseTargets: cloneBaseTargets(src.baseTargets),
        verbKeyframeOverridesMax: src.verbKeyframeOverridesMax,
        verbKeyframesMax: src.verbKeyframesMax,
        verbTimelines: src.verbTimelines.map(tab => ({
            verb: tab.verb,
            loop_duration_ms: tab.loop_duration_ms,
            keyframe_count: tab.keyframe_count,
            keyframes: tab.keyframes.map(kf => ({
                time_ms: kf.time_ms,
                override_count: kf.override_count,
                overrides: kf.overrides.map(o => ({ ...o })),
            })),
        })),
        bobAmpFollowEmotionArm: src.bobAmpFollowEmotionArm,
        idleAnim: cloneIdleAnim(src.idleAnim),
        emotionSim: { ...src.emotionSim },
        frameAnim: { ...src.frameAnim },
        verbSim: { ...src.verbSim },
        verbTransitionDurMs: src.verbTransitionDurMs,
        emotionTriangulation: cloneMutableEmotionTriangulation(src.emotionTriangulation),
    });
}

/** Build mutable session state from shipped tables (requires regenerated `FACE_CONFIG_DATA.ts`). */
export function buildFaceConfigStateFromSource(): FaceConfigState {
    // Dynamic import so a stale generated file does not break snapshot-only codegen.
    const data = require("./FACE_CONFIG_DATA") as typeof import("./FACE_CONFIG_DATA");
    const emotionNames = [...data.kEmotionNames];
    const emotionPoints = data.kEmotionPoints.map((p: { v: number; a: number }) => ({
        v: p.v,
        a: p.a,
    }));
    const emotionTriangulation = buildEmotionTriangulationFromPoints(emotionNames, emotionPoints);
    const base: FaceConfigState = {
        schemaVersion: 3,
        expressions: [...data.EXPRESSIONS],
        expressionIsEmotion: [...data.kExpressionIsEmotion],
        emotionNames,
        emotionPoints,
        pickOrderIndices: [...data.kPickOrderIndices],
        namedEmotionToExpressionIndex: [...data.kNamedEmotionToExpressionIndex],
        baseTargets: cloneBaseTargets(data.kBaseTargets),
        verbKeyframeOverridesMax: data.kVerbKeyframeOverridesMax,
        verbKeyframesMax: data.kVerbKeyframesMax,
        verbTimelines: cloneMutableVerbTimelines() as MutableVerbTimeline[],
        bobAmpFollowEmotionArm: BOB_AMP_FOLLOW_EMOTION_ARM,
        idleAnim: cloneIdleAnim(data.kIdleAnim),
        emotionSim: { ...data.kEmotionSim },
        frameAnim: { ...data.kFrameAnim },
        verbSim: { ...data.kVerbSim },
        verbTransitionDurMs: data.kVerbTransitionDurMs,
        emotionTriangulation,
    };
    return ensureSystemVerbTimelines(base);
}
