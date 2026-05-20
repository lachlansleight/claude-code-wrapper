import type { MutableEmotionTriangulation } from "./emotionTriangulationLive";
import type {
    EmotionPoint,
    EmotionSimConfig,
    FrameAnimConfig,
    IdleAnimRow,
    ParamI16,
    VerbSimConfig,
} from "./faceConfigTypes";
import type { MutableVerbTimeline } from "./mutableVerbTimelines";

/** Full editor session copy of shipped face config + live triangulation. */
export interface FaceConfigState {
    /** Snapshot / codegen schema version (3 = arm fields in FaceParams). */
    schemaVersion?: number;
    expressions: string[];
    expressionIsEmotion: boolean[];
    emotionNames: string[];
    emotionPoints: EmotionPoint[];
    pickOrderIndices: number[];
    namedEmotionToExpressionIndex: number[];
    baseTargets: ParamI16[][];
    verbKeyframeOverridesMax: number;
    verbKeyframesMax: number;
    verbTimelines: MutableVerbTimeline[];
    bobAmpFollowEmotionArm: number;
    idleAnim: IdleAnimRow[];
    emotionSim: EmotionSimConfig;
    frameAnim: FrameAnimConfig;
    verbSim: VerbSimConfig;
    verbTransitionDurMs: number;
    emotionTriangulation: MutableEmotionTriangulation;
}
