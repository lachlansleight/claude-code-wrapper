import { Expression, FieldIndex, GazeStyle, MotionMode, P } from "../face-engine/faceConfigTypes";
import type { FaceConfigState } from "../face-engine/faceConfigState";
import { bobAmpTsLiteral, fieldIndexEnumName, fmtTsFloat } from "./format";

function emitVerbTimelinesTs(config: FaceConfigState): string {
    const blocks: string[] = [];
    for (const tab of config.verbTimelines) {
        const verbName = Expression[tab.verb];
        const kfBlocks = tab.keyframes.map(kf => {
            const overrides = kf.overrides
                .map(
                    o =>
                        `          { field: FieldIndex.${fieldIndexEnumName(o.field)}, targetValue: ${o.targetValue}, strength: ${o.strength} },`
                )
                .join("\n");
            return `      {
        time_ms: ${kf.time_ms},
        override_count: ${kf.override_count},
        overrides: [
${overrides}
        ],
      }`;
        });
        blocks.push(`  {
    verb: Expression.${verbName},
    loop_duration_ms: ${tab.loop_duration_ms},
    keyframe_count: ${tab.keyframe_count},
    keyframes: [
${kfBlocks.join(",\n")},
    ],
  }`);
    }
    return `export const kVerbTimelines: readonly VerbTimeline[] = [
${blocks.join(",\n")},
] as const;`;
}

export function emitFaceConfigTs(config: FaceConfigState): string {
    const baseBlocks = config.baseTargets.map((row, i) => {
        const name = config.expressions[i]!;
        const cells = row.map(c => `    P(${c.value}, ${c.strength}),`).join("\n");
        return `  // ${name}\n  [\n${cells}\n  ]`;
    });

    const armLines = config.armPresets.map(
        p =>
            `  { min_deg: ${p.min_deg}, max_deg: ${p.max_deg}, period_s: ${p.period_s}, interval_s: ${p.interval_s} },`
    );

    const motionLines = config.motion.map((m, i) => {
        const name = config.expressions[i]!;
        const mode = MotionMode[m.mode];
        return `  // ${name}
  {
    mode: MotionMode.${mode},
    center: ${m.center},
    amplitude: ${m.amplitude},
    period_ms: ${m.period_ms},
    period_jitter_ms: ${m.period_jitter_ms},
    slew_ms: ${m.slew_ms},
  }`;
    });

    const idleLines = config.idleAnim.map((row, i) => {
        const name = config.expressions[i]!;
        const gaze = GazeStyle[row.gaze_style];
        return `  // ${name}
  {
    blink_period_min_ms: ${row.blink_period_min_ms},
    blink_period_max_ms: ${row.blink_period_max_ms},
    blink_close_ms: ${row.blink_close_ms},
    blink_open_ms: ${row.blink_open_ms},
    bob_amplitude_px: ${bobAmpTsLiteral(row.bob_amplitude_px)},
    gaze_style: GazeStyle.${gaze},
    gaze_move_ms: ${row.gaze_move_ms},
    gaze_rand_span_x: ${row.gaze_rand_span_x},
    gaze_rand_span_y: ${row.gaze_rand_span_y},
    gaze_reroll_min_ms: ${row.gaze_reroll_min_ms},
    gaze_reroll_max_ms: ${row.gaze_reroll_max_ms},
    gaze_scan_period_ms: ${row.gaze_scan_period_ms},
    gaze_amp_x: ${row.gaze_amp_x},
    gaze_amp_y: ${row.gaze_amp_y},
  }`;
    });

    const s = config.emotionSim;
    const f = config.frameAnim;

    return `/**
 * GENERATED — face-editor POST /api/saveData. Do not edit by hand.
 * Types/helpers in faceConfigTypes.ts.
 */

export * from "./faceConfigTypes";
export * from "./faceConfigHelpers";

import {
  BOB_AMP_FOLLOW_EMOTION_ARM,
  Expression,
  FieldIndex,
  GazeStyle,
  MotionMode,
  P,
  type ArmPreset,
  type ExprMotionRow,
  type FaceParamsIndexed,
  type IdleAnimRow,
  type VerbTimeline,
} from "./faceConfigTypes";

export const kExpressionIsEmotion: readonly boolean[] = [
  ${config.expressionIsEmotion.map(b => String(b)).join(",\n  ")},
] as const;

export const kEmotionNames = [
  ${config.emotionNames.map(n => `"${n}"`).join(",\n  ")},
] as const;

export const kEmotionPoints: readonly {
  readonly v: number;
  readonly a: number;
}[] = [
${config.emotionPoints.map(p => `  { v: ${fmtTsFloat(p.v)}, a: ${fmtTsFloat(p.a)} },`).join("\n")}
] as const;

export const kPickOrderIndices = [
  ${config.pickOrderIndices.join(", ")},
] as const;

export const kNamedEmotionToExpressionIndex: readonly number[] = [
  ${config.namedEmotionToExpressionIndex.join(", ")},
] as const;

export const kBaseTargets: readonly FaceParamsIndexed[] = [
${baseBlocks.join(",\n")},
] as const;

export const kVerbKeyframeOverridesMax = ${config.verbKeyframeOverridesMax} as const;
export const kVerbKeyframesMax = ${config.verbKeyframesMax} as const;

${emitVerbTimelinesTs(config)}

export const kVerbTimelineCount = kVerbTimelines.length;

export const kArmPresets: readonly ArmPreset[] = [
${armLines.join("\n")}
] as const;

export const kMotion: readonly ExprMotionRow[] = [
${motionLines.join(",\n")},
] as const;

export const kIdleAnim: readonly IdleAnimRow[] = [
${idleLines.join(",\n")},
] as const;

export const kEmotionSim = {
  tau_ms_activation: ${s.tau_ms_activation},
  tau_ms_valence: ${s.tau_ms_valence},
  tau_ms_raw_follow: ${s.tau_ms_raw_follow},
  snap_hysteresis_dist: ${s.snap_hysteresis_dist},
  snap_hysteresis_hold_ms: ${s.snap_hysteresis_hold_ms},
  dist_sq_tie_eps: ${s.dist_sq_tie_eps},
  baseline_activation: ${s.baseline_activation},
} as const;

export const kFrameAnim = {
  mood_ring_tau_ms: ${f.mood_ring_tau_ms},
  emotion_geometry_smooth_tau_ms: ${f.emotion_geometry_smooth_tau_ms},
  tick_interval_ms: ${f.tick_interval_ms},
  tick_interval_stream_ms: ${f.tick_interval_stream_ms},
  thinking_flip_dur_ms: ${f.thinking_flip_dur_ms},
  thinking_flip_min_ms: ${f.thinking_flip_min_ms},
  thinking_flip_max_ms: ${f.thinking_flip_max_ms},
  progress_fade_ms: ${f.progress_fade_ms},
  effects_fade_ms: ${f.effects_fade_ms},
  breath_period_ms: ${f.breath_period_ms},
  breath_eye_amp_px: ${f.breath_eye_amp_px},
  breath_mouth_scale: ${f.breath_mouth_scale},
  emotion_bob_amp_follow_arm: ${f.emotion_bob_amp_follow_arm},
  default_blink_close_ms: ${f.default_blink_close_ms},
  default_blink_open_ms: ${f.default_blink_open_ms},
  default_gaze_move_ms: ${f.default_gaze_move_ms},
  invalid_gaze_reroll_fallback_ms: ${f.invalid_gaze_reroll_fallback_ms},
} as const;

export const kVerbSim = {
  strain_delay_ms: ${config.verbSim.strain_delay_ms},
  default_overlay_duration_ms: ${config.verbSim.default_overlay_duration_ms},
} as const;

export const kMotionRuntime = {
  default_static_slew_ms: ${config.motionRuntime.default_static_slew_ms},
  default_drift_slew_ms: ${config.motionRuntime.default_drift_slew_ms},
} as const;

export const kVerbTransitionDurMs = ${config.verbTransitionDurMs} as const;
`;
}
