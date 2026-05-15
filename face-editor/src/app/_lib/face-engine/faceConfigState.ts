import type { MutableEmotionTriangulation } from "./emotionTriangulationLive";
import type {
    ArmPreset,
    EmotionPoint,
    EmotionSimConfig,
    ExprMotionRow,
    FrameAnimConfig,
    IdleAnimRow,
    MotionRuntimeConfig,
    ParamI16,
    VerbSimConfig,
} from "./faceConfigTypes";
import type { MutableVerbTimeline } from "./mutableVerbTimelines";

/** Full editor session copy of shipped face config + live triangulation. */
export interface FaceConfigState {
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
    armPresets: ArmPreset[];
    motion: ExprMotionRow[];
    bobAmpFollowEmotionArm: number;
    idleAnim: IdleAnimRow[];
    emotionSim: EmotionSimConfig;
    frameAnim: FrameAnimConfig;
    verbSim: VerbSimConfig;
    motionRuntime: MotionRuntimeConfig;
    verbTransitionDurMs: number;
    emotionTriangulation: MutableEmotionTriangulation;
}
