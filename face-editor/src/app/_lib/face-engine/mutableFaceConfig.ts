import {
    buildEmotionTriangulationFromPoints,
    cloneMutableEmotionTriangulation,
} from "./emotionTriangulationLive";
import type { FaceConfigState } from "./faceConfigState";
import {
    BOB_AMP_FOLLOW_EMOTION_ARM,
    EXPRESSIONS,
    kArmPresets,
    kBaseTargets,
    kEmotionNames,
    kEmotionPoints,
    kEmotionSim,
    kExpressionIsEmotion,
    kFrameAnim,
    kIdleAnim,
    kMotion,
    kMotionRuntime,
    kNamedEmotionToExpressionIndex,
    kPickOrderIndices,
    kVerbSim,
    kVerbTimelines,
    kVerbTransitionDurMs,
} from "./FACE_CONFIG_DATA";
import type { ParamI16 } from "./faceConfigTypes";
import { kVerbKeyframeOverridesMax, kVerbKeyframesMax } from "./faceConfigTypes";
import { ensureSystemVerbTimelines } from "./faceConfigMutations";
import { cloneMutableVerbTimelines, type MutableVerbTimeline } from "./mutableVerbTimelines";

function cloneBaseTargets(src: readonly (readonly ParamI16[])[]): ParamI16[][] {
    return src.map(row => row.map(c => ({ value: c.value, strength: c.strength })));
}

function cloneArmPresets(src: typeof kArmPresets): FaceConfigState["armPresets"] {
    return src.map(p => ({ ...p }));
}

function cloneMotion(src: typeof kMotion): FaceConfigState["motion"] {
    return src.map(m => ({ ...m }));
}

function cloneIdleAnim(src: typeof kIdleAnim): FaceConfigState["idleAnim"] {
    return src.map(r => ({ ...r }));
}

/** Deep clone of shipped tables into a mutable `FaceConfigState`. */
export function cloneFaceConfigState(src: FaceConfigState): FaceConfigState {
    return {
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
        armPresets: cloneArmPresets(src.armPresets),
        motion: cloneMotion(src.motion),
        bobAmpFollowEmotionArm: src.bobAmpFollowEmotionArm,
        idleAnim: cloneIdleAnim(src.idleAnim),
        emotionSim: { ...src.emotionSim },
        frameAnim: { ...src.frameAnim },
        verbSim: { ...src.verbSim },
        motionRuntime: { ...src.motionRuntime },
        verbTransitionDurMs: src.verbTransitionDurMs,
        emotionTriangulation: cloneMutableEmotionTriangulation(src.emotionTriangulation),
    };
}

/** Build mutable session state from the shipped `FACE_CONFIG_DATA` + triangulation modules. */
export function buildFaceConfigStateFromSource(): FaceConfigState {
    const emotionNames = [...kEmotionNames];
    const emotionPoints = kEmotionPoints.map(p => ({ v: p.v, a: p.a }));
    const emotionTriangulation = buildEmotionTriangulationFromPoints(emotionNames, emotionPoints);
    const base: FaceConfigState = {
        schemaVersion: 2,
        expressions: [...EXPRESSIONS],
        expressionIsEmotion: [...kExpressionIsEmotion],
        emotionNames,
        emotionPoints,
        pickOrderIndices: [...kPickOrderIndices],
        namedEmotionToExpressionIndex: [...kNamedEmotionToExpressionIndex],
        baseTargets: cloneBaseTargets(kBaseTargets),
        verbKeyframeOverridesMax: kVerbKeyframeOverridesMax,
        verbKeyframesMax: kVerbKeyframesMax,
        verbTimelines: cloneMutableVerbTimelines() as MutableVerbTimeline[],
        armPresets: cloneArmPresets(kArmPresets),
        motion: cloneMotion(kMotion),
        bobAmpFollowEmotionArm: BOB_AMP_FOLLOW_EMOTION_ARM,
        idleAnim: cloneIdleAnim(kIdleAnim),
        emotionSim: { ...kEmotionSim },
        frameAnim: { ...kFrameAnim },
        verbSim: { ...kVerbSim },
        motionRuntime: { ...kMotionRuntime },
        verbTransitionDurMs: kVerbTransitionDurMs,
        emotionTriangulation,
    };
    return ensureSystemVerbTimelines(base);
}
