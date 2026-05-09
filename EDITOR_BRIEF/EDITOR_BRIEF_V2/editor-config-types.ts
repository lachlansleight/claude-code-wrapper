/**
 * Editor-side schema for generating robot_v3/src/face/FACE_CONFIG_DATA.h
 *
 * This file is intentionally literal and firmware-oriented.
 * Keep enum/member order aligned with firmware.
 */

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
  "OverlayWaking",
  "OverlayAttention",
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

export type Expression = (typeof EXPRESSIONS)[number];

export const NAMED_EMOTIONS = [
  "Neutral",
  "Happy",
  "Excited",
  "Joyful",
  "Sad",
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

export type NamedEmotion = (typeof NAMED_EMOTIONS)[number];

export const FIELD_INDEX = [
  "EyeDy",
  "EyeRx",
  "EyeTopApex",
  "EyeTopCorner",
  "EyeBotApex",
  "EyeBotCorner",
  "EyeThick",
  "EyeWaveAmp",
  "EyeWaveFreq",
  "EyeWaveSpeed",
  "PupilDx",
  "PupilDy",
  "PupilR",
  "MouthDy",
  "MouthRx",
  "MouthTopApex",
  "MouthTopCorner",
  "MouthBotApex",
  "MouthBotCorner",
  "MouthThick",
  "MouthWaveAmp",
  "MouthWaveFreq",
  "MouthWaveSpeed",
  "FaceRot",
  "FaceY",
  "RingR",
  "RingG",
  "RingB",
] as const;

export type FieldIndex = (typeof FIELD_INDEX)[number];

export interface ParamI16 {
  value: number; // int16
  strength: number; // uint8 [0..100]
}

export interface FaceParams {
  eye_dy: ParamI16;
  eye_rx: ParamI16;
  eye_top_apex: ParamI16;
  eye_top_corner: ParamI16;
  eye_bot_apex: ParamI16;
  eye_bot_corner: ParamI16;
  eye_thick: ParamI16;
  eye_wave_amp: ParamI16;
  eye_wave_freq: ParamI16;
  eye_wave_speed: ParamI16;
  pupil_dx: ParamI16;
  pupil_dy: ParamI16;
  pupil_r: ParamI16;
  mouth_dy: ParamI16;
  mouth_rx: ParamI16;
  mouth_top_apex: ParamI16;
  mouth_top_corner: ParamI16;
  mouth_bot_apex: ParamI16;
  mouth_bot_corner: ParamI16;
  mouth_thick: ParamI16;
  mouth_wave_amp: ParamI16;
  mouth_wave_freq: ParamI16;
  mouth_wave_speed: ParamI16;
  face_rot: ParamI16;
  face_y: ParamI16;
  ring_r: ParamI16;
  ring_g: ParamI16;
  ring_b: ParamI16;
}

export interface EmotionPoint {
  v: number; // float [-1..1]
  a: number; // float [0..1]
}

export interface SparseOverride {
  field: FieldIndex;
  value: number; // int16
  strength: number; // uint8 (usually 100 in current firmware data)
}

export interface SparseVerbTimeline {
  verb: Extract<
    Expression,
    | "VerbThinking"
    | "VerbReading"
    | "VerbWriting"
    | "VerbExecuting"
    | "VerbStraining"
    | "VerbSleeping"
  >;
  overrides: SparseOverride[]; // max 32 in firmware struct today
}

export interface ArmPreset {
  min_deg: number; // int16
  max_deg: number; // int16
  period_s: number; // float
  interval_s: number; // float
}

export type MotionMode =
  | "None"
  | "Static"
  | "RandomDrift"
  | "Oscillate"
  | "Waggle"
  | "Thinking";

export interface ExprMotionRow {
  mode: MotionMode;
  center: number; // int8
  amplitude: number; // uint8
  period_ms: number; // uint16
  period_jitter_ms: number; // uint16
  slew_ms: number; // uint16
}

export type GazeStyle = "Off" | "IdleRandom" | "Orbit" | "ScanX";

export interface IdleAnimRow {
  blink_period_min_ms: number; // uint16
  blink_period_max_ms: number; // uint16
  blink_close_ms: number; // uint16
  blink_open_ms: number; // uint16
  bob_amplitude_px: number; // int16 (uses sentinel for follow-emotion-arm)
  gaze_style: GazeStyle;
  gaze_move_ms: number; // uint16
  gaze_rand_span_x: number; // int16
  gaze_rand_span_y: number; // int16
  gaze_reroll_min_ms: number; // uint32
  gaze_reroll_max_ms: number; // uint32
  gaze_scan_period_ms: number; // uint32
  gaze_amp_x: number; // int16
  gaze_amp_y: number; // int16
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

/**
 * Full export payload required to generate FACE_CONFIG_DATA.h.
 */
export interface FaceConfigDataDocument {
  // constants
  bob_amp_follow_emotion_arm_sentinel: number; // int16, currently 0x8000 cast
  expressions: Expression[];
  named_emotions: NamedEmotion[];

  // tables
  emotion_points: Record<NamedEmotion, EmotionPoint>;
  pick_order: NamedEmotion[];
  base_targets: Record<Expression, FaceParams>;
  verb_timelines: SparseVerbTimeline[];
  arm_presets: Record<Expression, ArmPreset>;
  motion_rows: Record<Expression, ExprMotionRow>;
  idle_anim_rows: Record<Expression, IdleAnimRow>;

  // runtime tunables
  emotion_sim: EmotionSimConfig;
  frame_anim: FrameAnimConfig;
  verb_sim: VerbSimConfig;
  motion_runtime: MotionRuntimeConfig;
}

/**
 * Minimal export validation checks before writing C++.
 */
export interface ValidationIssue {
  path: string;
  message: string;
  severity: "error" | "warning";
}

export interface ValidationResult {
  ok: boolean;
  issues: ValidationIssue[];
}

