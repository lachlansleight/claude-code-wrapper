/**
 * Face config schema: enums, row types, and helpers.
 * Data tables live in `FACE_CONFIG_DATA.ts` (eventually generated).
 */

export const EXPRESSION_COUNT = 22 as const;

/** PascalCase names in `Face::Expression` enum order (index = enum value). */
export const EXPRESSIONS = [
    "Neutral",
    "Happy",
    "Excited",
    "Joyful",
    "Sad",
    "VerbThinking",
    "VerbReading",
    "VerbWriting",
    "VerbExecuting",
    "VerbStraining",
    "VerbSleeping",
    "VerbWaking",
    "VerbAttractingAttention",
    "Sleepy",
    "Distressed",
    "Blissed",
    "Depressed",
    "Shocked",
    "Disappointed",
    "Cheeky",
    "Gleeful",
    "Frustrated",
] as const;

export type ExpressionName = (typeof EXPRESSIONS)[number];

export enum Expression {
    Neutral = 0,
    Happy,
    Excited,
    Joyful,
    Sad,
    VerbThinking,
    VerbReading,
    VerbWriting,
    VerbExecuting,
    VerbStraining,
    VerbSleeping,
    VerbWaking,
    VerbAttractingAttention,
    Sleepy,
    Distressed,
    Blissed,
    Depressed,
    Shocked,
    Disappointed,
    Cheeky,
    Gleeful,
    Frustrated,
    Count = 22,
}

export enum FieldIndex {
    EyeDy = 0,
    EyeRx,
    EyeOpenAmt,
    EyeArcAmt,
    EyeThick,
    EyeWaveAmp,
    EyeWaveFreq,
    EyeWaveSpeed,
    PupilDx,
    PupilDy,
    PupilR,
    MouthDy,
    MouthRx,
    MouthOpenAmt,
    MouthArcAmt,
    MouthThick,
    MouthWaveAmp,
    MouthWaveFreq,
    MouthWaveSpeed,
    FaceRot,
    FaceY,
    RingR,
    RingG,
    RingB,
    Count = 24,
}

export interface ParamI16 {
    value: number;
    strength: number;
}

export const P = (value: number, strength = 100): ParamI16 => ({
    value,
    strength,
});

export type FaceParamsIndexed = readonly ParamI16[];

export const kVerbKeyframeOverridesMax = 32 as const;
export const kVerbKeyframesMax = 16 as const;

export interface KeyframeOverride {
    field: FieldIndex;
    targetValue: number;
    strength: number;
}

export interface VerbKeyframe {
    time_ms: number;
    override_count: number;
    overrides: readonly KeyframeOverride[];
}

export interface VerbTimeline {
    verb: Expression;
    loop_duration_ms: number;
    keyframe_count: number;
    keyframes: readonly VerbKeyframe[];
}

export interface ArmPreset {
    min_deg: number;
    max_deg: number;
    period_s: number;
    interval_s: number;
}

export enum MotionMode {
    None = 0,
    Static,
    RandomDrift,
    Oscillate,
    Waggle,
    Thinking,
}

export interface ExprMotionRow {
    mode: MotionMode;
    center: number;
    amplitude: number;
    period_ms: number;
    period_jitter_ms: number;
    slew_ms: number;
}

/** Same sentinel as firmware `FaceConfig::kBobAmpFollowEmotionArm` (int16 0x8000). */
export const BOB_AMP_FOLLOW_EMOTION_ARM = -32768;

export enum GazeStyle {
    Off = 0,
    IdleRandom,
    Orbit,
    ScanX,
}

export interface IdleAnimRow {
    blink_period_min_ms: number;
    blink_period_max_ms: number;
    blink_close_ms: number;
    blink_open_ms: number;
    bob_amplitude_px: number;
    gaze_style: GazeStyle;
    gaze_move_ms: number;
    gaze_rand_span_x: number;
    gaze_rand_span_y: number;
    gaze_reroll_min_ms: number;
    gaze_reroll_max_ms: number;
    gaze_scan_period_ms: number;
    gaze_amp_x: number;
    gaze_amp_y: number;
}

export interface EmotionSimConfig {
    tau_ms_activation: number;
    tau_ms_valence: number;
    tau_ms_raw_follow: number;
    snap_hysteresis_dist: number;
    snap_hysteresis_hold_ms: number;
    dist_sq_tie_eps: number;
    baseline_activation: number;
}

export interface FrameAnimConfig {
    mood_ring_tau_ms: number;
    emotion_geometry_smooth_tau_ms: number;
    tick_interval_ms: number;
    tick_interval_stream_ms: number;
    thinking_flip_dur_ms: number;
    thinking_flip_min_ms: number;
    thinking_flip_max_ms: number;
    progress_fade_ms: number;
    effects_fade_ms: number;
    breath_period_ms: number;
    breath_eye_amp_px: number;
    breath_mouth_scale: number;
    emotion_bob_amp_follow_arm: number;
    default_blink_close_ms: number;
    default_blink_open_ms: number;
    default_gaze_move_ms: number;
    invalid_gaze_reroll_fallback_ms: number;
}

export interface VerbSimConfig {
    strain_delay_ms: number;
    default_overlay_duration_ms: number;
}

export interface MotionRuntimeConfig {
    default_static_slew_ms: number;
    default_drift_slew_ms: number;
}

export interface EmotionPoint {
    v: number;
    a: number;
}

export function expressionIndexFromName(name: string): number {
    const i = EXPRESSIONS.indexOf(name as ExpressionName);
    return i >= 0 ? i : 0;
}
