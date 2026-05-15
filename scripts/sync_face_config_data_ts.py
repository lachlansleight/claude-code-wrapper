#!/usr/bin/env python3
"""Emit face-editor/src/app/_lib/face-engine/FACE_CONFIG_DATA.ts from FACE_CONFIG_DATA.h.

Run after editing the header:
  python scripts/sync_face_config_data_ts.py
"""

from __future__ import annotations

import re
import textwrap
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
H = REPO / "robot_v3" / "src" / "face" / "FACE_CONFIG_DATA.h"
OUT = REPO / "face-editor" / "src" / "app" / "_lib" / "face-engine" / "FACE_CONFIG_DATA.ts"

FACE_P = re.compile(r"FACE_P\(\s*(-?\d+)\s*\)")


def extract_face_p_values(row_inner: str) -> list[int]:
    return [int(m.group(1)) for m in FACE_P.finditer(row_inner)]


def take_brace_group(s: str, start: int) -> tuple[str, int]:
    """If s[start]=='{', return substring through matching '}' inclusive and index after it."""
    if start >= len(s) or s[start] != "{":
        raise ValueError(f"expected '{{' at {start}")
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return s[start : i + 1], i + 1
        i += 1
    raise ValueError("unbalanced braces")


def parse_verb_timeline_blocks(verb_inner: str, fi_map: dict[str, int]) -> list[str]:
    """Parse kVerbTimelines body into TS fragments (one per verb)."""
    ko_re = re.compile(r"KO\s*\(\s*Face::FieldIndex::(\w+)\s*,\s*(-?\d+)\s*\)")
    pos = 0
    verb_ts: list[str] = []
    s = verb_inner.strip()
    while pos < len(s):
        while pos < len(s) and s[pos].isspace():
            pos += 1
        if pos >= len(s):
            break
        tl_block, pos = take_brace_group(s, pos)
        m = re.match(
            r"\{\s*Face::Expression::(\w+)\s*,\s*(\d+)u\s*,\s*(\d+)u\s*,\s*",
            tl_block,
        )
        if not m:
            raise SystemExit(f"bad VerbTimeline header: {tl_block[:120]!r}")
        verb, loop_d, kf_count_s = m.group(1), m.group(2), m.group(3)
        kf_count = int(kf_count_s)
        rest = tl_block[m.end() :].lstrip()
        kfs_array, _ = take_brace_group(rest, 0)
        inner_kfs = kfs_array.strip()[1:-1].strip()

        keyframe_chunks: list[str] = []
        kpos = 0
        while kpos < len(inner_kfs):
            while kpos < len(inner_kfs) and inner_kfs[kpos].isspace():
                kpos += 1
            if kpos >= len(inner_kfs):
                break
            kf_blk, kpos = take_brace_group(inner_kfs, kpos)
            keyframe_chunks.append(kf_blk)
            while kpos < len(inner_kfs) and inner_kfs[kpos] in ", \t\n\r":
                kpos += 1

        if len(keyframe_chunks) != kf_count:
            raise SystemExit(
                f"verb {verb}: keyframe_count is {kf_count} but parsed "
                f"{len(keyframe_chunks)} keyframe(s)"
            )

        kf_ts_parts: list[str] = []
        for kf_blk in keyframe_chunks:
            mk = re.match(r"\{\s*(\d+)u\s*,\s*(\d+)u\s*,\s*", kf_blk)
            if not mk:
                raise SystemExit(f"verb {verb}: bad keyframe: {kf_blk[:100]!r}")
            t0, oc_s = mk.group(1), mk.group(2)
            oc = int(oc_s)
            tail = kf_blk[mk.end() :].lstrip()
            obr, _ = take_brace_group(tail, 0)
            inner_ko = obr.strip()[1:-1].strip()
            pairs = ko_re.findall(inner_ko)
            if oc != len(pairs):
                raise SystemExit(
                    f"verb {verb} @ t={t0}ms: override_count {oc} != KO count {len(pairs)}"
                )
            olist = []
            for fn, val in pairs:
                if fn not in fi_map:
                    raise SystemExit(f"unknown field {fn} in verb {verb}")
                olist.append(
                    f"{{ field: FieldIndex.{fn}, targetValue: {val}, strength: 100 }}"
                )
            inner_list = ",\n          ".join(olist)
            kf_ts_parts.append(
                f"""      {{
        time_ms: {t0},
        override_count: {oc},
        overrides: [
          {inner_list},
        ],
      }},"""
            )

        kfs_joined = "\n".join(kf_ts_parts).rstrip()
        verb_ts.append(
            textwrap.dedent(
                f"""\
  {{
    verb: Expression.{verb},
    loop_duration_ms: {loop_d},
    keyframe_count: {kf_count_s},
    keyframes: [
{kfs_joined}
    ],
  }},"""
            ).rstrip()
        )

        while pos < len(s) and s[pos].isspace():
            pos += 1
        if pos < len(s) and s[pos] == ",":
            pos += 1

    return verb_ts


def main() -> None:
    text = H.read_text(encoding="utf-8")

    # kBaseTargets rows: /* Name */ { FACE_P... }
    base_block = re.search(
        r"static const Face::FaceParams kBaseTargets\[[^\]]+\] = \{(.*?)\n\};",
        text,
        re.S,
    )
    if not base_block:
        raise SystemExit("could not find kBaseTargets")
    base_rows = re.findall(r"/\* ([^*]+) \*/\s*\{([^}]+)\}", base_block.group(1), re.S)
    if len(base_rows) != 22:
        raise SystemExit(f"expected 22 kBaseTargets rows, got {len(base_rows)}")

    base_ts = []
    for name, inner in base_rows:
        vals = extract_face_p_values(inner)
        if len(vals) != 24:
            raise SystemExit(f"kBaseTargets row {name}: expected 24 FACE_P, got {len(vals)}")
        inner_ts = ", ".join(f"P({v})" for v in vals)
        base_ts.append(f"  // {name.strip()}\n  [{inner_ts}],")

    # kVerbTimelines: VerbTimeline keyframes (KO macros)
    vm = re.search(
        r"static constexpr VerbTimeline kVerbTimelines\[\] = \{(.*)\n\};\n#undef KO",
        text,
        re.S,
    )
    if not vm:
        raise SystemExit("could not find kVerbTimelines (VerbTimeline)")
    verb_inner = vm.group(1)
    field_names = [
        "EyeDy",
        "EyeRx",
        "EyeOpenAmt",
        "EyeArcAmt",
        "EyeThick",
        "EyeWaveAmp",
        "EyeWaveFreq",
        "EyeWaveSpeed",
        "PupilDx",
        "PupilDy",
        "PupilR",
        "MouthDy",
        "MouthRx",
        "MouthOpenAmt",
        "MouthArcAmt",
        "MouthThick",
        "MouthWaveAmp",
        "MouthWaveFreq",
        "MouthWaveSpeed",
        "FaceRot",
        "FaceY",
        "RingR",
        "RingG",
        "RingB",
    ]
    fi_map = {n: i for i, n in enumerate(field_names)}
    verb_ts = parse_verb_timeline_blocks(verb_inner, fi_map)
    if len(verb_ts) != 6:
        raise SystemExit(f"expected 6 verb timelines, parsed {len(verb_ts)}")

    # kArmPresets — four numbers per line in comment blocks
    arm_block = re.search(
        r"static constexpr ArmPreset kArmPresets\[[^\]]+\] = \{(.*?)\n\};",
        text,
        re.S,
    )
    if not arm_block:
        raise SystemExit("could not find kArmPresets")
    arm_lines = []
    for line in arm_block.group(1).splitlines():
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        m = re.match(
            r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*([-0-9.]+)f\s*,\s*([-0-9.]+)f\s*\}\s*,(?:\s*//\s*(.*))?",
            line,
        )
        if not m:
            raise SystemExit(f"bad arm row: {line!r}")
        arm_lines.append(
            f"  {{ min_deg: {m.group(1)}, max_deg: {m.group(2)}, "
            f"period_s: {m.group(3)}, interval_s: {m.group(4)} }},"
        )
    if len(arm_lines) != 22:
        raise SystemExit(f"expected 22 arm presets, got {len(arm_lines)}")

    # kMotion — {MotionMode::X, ...}
    motion_block = re.search(
        r"static constexpr ExprMotionRow kMotion\[[^\]]+\] = \{(.*?)\n\};",
        text,
        re.S,
    )
    if not motion_block:
        raise SystemExit("could not find kMotion")
    motion_rows = []
    mode_map = {
        "None": "MotionMode.None",
        "Static": "MotionMode.Static",
        "RandomDrift": "MotionMode.RandomDrift",
        "Oscillate": "MotionMode.Oscillate",
        "Waggle": "MotionMode.Waggle",
        "Thinking": "MotionMode.Thinking",
    }
    for line in motion_block.group(1).splitlines():
        line = line.strip()
        if not line:
            continue
        line = re.sub(r"^/\*[^*]+\*/\s*", "", line)
        if not line or line.startswith("//"):
            continue
        m = re.match(
            r"\{\s*MotionMode::(\w+)\s*,\s*(-?\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}\s*,?",
            line,
        )
        if not m:
            raise SystemExit(f"bad motion row: {line!r}")
        mode = mode_map[m.group(1)]
        motion_rows.append(
            "  { "
            f"mode: {mode}, center: {m.group(2)}, amplitude: {m.group(3)}, "
            f"period_ms: {m.group(4)}, period_jitter_ms: {m.group(5)}, slew_ms: {m.group(6)} "
            "},"
        )

    # kIdleAnim — multi-line rows with GazeStyle::
    idle_block = re.search(
        r"static constexpr IdleAnimRow kIdleAnim\[[^\]]+\] = \{(.*?)\n\};",
        text,
        re.S,
    )
    if not idle_block:
        raise SystemExit("could not find kIdleAnim")
    idle_body = idle_block.group(1)
    gaze_map = {
        "Off": "GazeStyle.Off",
        "IdleRandom": "GazeStyle.IdleRandom",
        "Orbit": "GazeStyle.Orbit",
        "ScanX": "GazeStyle.ScanX",
    }
    idle_rows = []
    for chunk in re.finditer(
        r"/\* ([^*]+) \*/\s*\{([^}]+)\}", idle_body, re.S
    ):
        name, inner = chunk.group(1).strip(), chunk.group(2)
        inner_one = " ".join(inner.split())
        # bob sentinel
        inner_one = inner_one.replace(
            "kBobAmpFollowEmotionArm", "BOB_AMP_FOLLOW_EMOTION_ARM"
        )
        # GazeStyle::X -> GazeStyle.X
        for gs, rep in gaze_map.items():
            inner_one = inner_one.replace(f"GazeStyle::{gs}", rep)
        # strip trailing comma spaces
        parts = [p.strip() for p in inner_one.split(",") if p.strip()]
        if len(parts) != 14:
            raise SystemExit(f"idle row {name}: expected 14 fields, got {parts!r}")
        keys = [
            "blink_period_min_ms",
            "blink_period_max_ms",
            "blink_close_ms",
            "blink_open_ms",
            "bob_amplitude_px",
            "gaze_style",
            "gaze_move_ms",
            "gaze_rand_span_x",
            "gaze_rand_span_y",
            "gaze_reroll_min_ms",
            "gaze_reroll_max_ms",
            "gaze_scan_period_ms",
            "gaze_amp_x",
            "gaze_amp_y",
        ]
        obj = ", ".join(f"{k}: {v}" for k, v in zip(keys, parts))
        idle_rows.append(f"  // {name}\n  {{ {obj} }},")

    # kFrameAnim etc. — hand-parse single struct initializer lines
    def float_struct(name: str, pattern: str, fields: list[str]) -> str:
        m = re.search(pattern, text, re.S)
        if not m:
            raise SystemExit(f"missing {name}")
        inner = m.group(1)
        nums = re.findall(r"([-0-9.]+)\s*(?:f)?\s*,", inner)
        if len(nums) != len(fields):
            raise SystemExit(f"{name}: field count mismatch {len(nums)} vs {len(fields)}")
        pairs = ",\n  ".join(f"{k}: {v}" for k, v in zip(fields, nums))
        return f"export const {name} = {{\n  {pairs},\n}} as const;\n"

    frame_anim = float_struct(
        "kFrameAnim",
        r"static constexpr FrameAnimConfig kFrameAnim = \{(.*?)\};",
        [
            "mood_ring_tau_ms",
            "emotion_geometry_smooth_tau_ms",
            "tick_interval_ms",
            "tick_interval_stream_ms",
            "thinking_flip_dur_ms",
            "thinking_flip_min_ms",
            "thinking_flip_max_ms",
            "progress_fade_ms",
            "effects_fade_ms",
            "breath_period_ms",
            "breath_eye_amp_px",
            "breath_mouth_scale",
            "emotion_bob_amp_follow_arm",
            "default_blink_close_ms",
            "default_blink_open_ms",
            "default_gaze_move_ms",
            "invalid_gaze_reroll_fallback_ms",
        ],
    )

    emotion_sim = float_struct(
        "kEmotionSim",
        r"static constexpr EmotionSimConfig kEmotionSim = \{(.*?)\};",
        [
            "tau_ms_activation",
            "tau_ms_valence",
            "tau_ms_raw_follow",
            "snap_hysteresis_dist",
            "snap_hysteresis_hold_ms",
            "dist_sq_tie_eps",
            "baseline_activation",
        ],
    )

    verb_sim_m = re.search(
        r"static constexpr VerbSimConfig kVerbSim = \{([^}]+)\};",
        text,
        re.S,
    )
    if not verb_sim_m:
        raise SystemExit("kVerbSim")
    nums = re.findall(r"\b(\d+)\b", verb_sim_m.group(1))
    if len(nums) < 2:
        raise SystemExit(f"kVerbSim nums {nums}")
    verb_sim = (
        "export const kVerbSim = {\n"
        f"  strain_delay_ms: {nums[0]},\n"
        f"  default_overlay_duration_ms: {nums[1]},\n"
        "} as const;\n"
    )

    motion_rt_m = re.search(
        r"static constexpr MotionRuntimeConfig kMotionRuntime = \{([^}]+)\};",
        text,
        re.S,
    )
    if not motion_rt_m:
        raise SystemExit("kMotionRuntime")
    mrt = re.findall(r"\b(\d+)\b", motion_rt_m.group(1))
    if len(mrt) < 2:
        raise SystemExit(f"kMotionRuntime nums {mrt}")
    motion_rt = (
        "export const kMotionRuntime = {\n"
        f"  default_static_slew_ms: {mrt[0]},\n"
        f"  default_drift_slew_ms: {mrt[1]},\n"
        "} as const;\n"
    )

    out = f'''/**
 * Mirror of `robot_v3/src/face/FACE_CONFIG_DATA.h` (+ `VerbTimeline.h` transition ms).
 * **Generated** — run `python scripts/sync_face_config_data_ts.py` after editing the header.
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

export enum Expression {{
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
}}

export const kExpressionIsEmotion: readonly boolean[] = [
  true, true, true, true, true, false, false, false, false, false, false,
  false, false, true, true, true, true, true, true, true, true, true,
] as const;

export const kEmotionNames = [
  "neutral", "happy", "excited", "joyful", "sad", "sleepy",
  "distressed", "blissed", "depressed", "shocked", "disappointed", "cheeky",
  "gleeful", "frustrated",
] as const;

export const kEmotionPoints: readonly {{ readonly v: number; readonly a: number }}[] = [
  {{ v: 0.0, a: 0.5 }},
  {{ v: 0.5, a: 0.5 }},
  {{ v: 1.0, a: 0.6 }},
  {{ v: 1.0, a: 1.0 }},
  {{ v: -0.5, a: 0.5 }},
  {{ v: -0.2, a: 0.0 }},
  {{ v: -1.0, a: 1.0 }},
  {{ v: 1.0, a: 0.0 }},
  {{ v: -1.0, a: 0.0 }},
  {{ v: -0.3, a: 1.0 }},
  {{ v: -1.0, a: 0.3 }},
  {{ v: 0.5, a: 0.7 }},
  {{ v: 0.6, a: 1.0 }},
  {{ v: -0.6, a: 0.8 }},
] as const;

/** NamedEmotion enum discriminant order; tie-break (earlier wins). */
export const kPickOrderIndices = [
  12, 11, 5, 6, 13, 9, 7, 8, 10, 0, 1, 2, 3, 4,
] as const;

/** Maps NamedEmotion index → Face::Expression index. */
export const kNamedEmotionToExpressionIndex: readonly number[] = [
  0, 1, 2, 3, 4, 13, 14, 15, 16, 17, 18, 19, 20, 21,
] as const;

export enum FieldIndex {{
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
}}

export interface ParamI16 {{
  value: number;
  strength: number;
}}

export const P = (value: number, strength = 100): ParamI16 => ({{ value, strength }});

export type FaceParamsIndexed = readonly ParamI16[];

export const kBaseTargets: readonly FaceParamsIndexed[] = [
{chr(10).join(base_ts)}
] as const;

export const kVerbKeyframeOverridesMax = 32 as const;
export const kVerbKeyframesMax = 16 as const;

export interface KeyframeOverride {{
  field: FieldIndex;
  targetValue: number;
  strength: number;
}}

export interface VerbKeyframe {{
  time_ms: number;
  override_count: number;
  overrides: readonly KeyframeOverride[];
}}

export interface VerbTimeline {{
  verb: Expression;
  loop_duration_ms: number;
  keyframe_count: number;
  keyframes: readonly VerbKeyframe[];
}}

export const kVerbTimelines: readonly VerbTimeline[] = [
{chr(10).join(verb_ts)}
] as const;

export const kVerbTimelineCount = kVerbTimelines.length;

export interface ArmPreset {{
  min_deg: number;
  max_deg: number;
  period_s: number;
  interval_s: number;
}}

export const kArmPresets: readonly ArmPreset[] = [
{chr(10).join(arm_lines)}
] as const;

export enum MotionMode {{
  None = 0,
  Static,
  RandomDrift,
  Oscillate,
  Waggle,
  Thinking,
}}

export interface ExprMotionRow {{
  mode: MotionMode;
  center: number;
  amplitude: number;
  period_ms: number;
  period_jitter_ms: number;
  slew_ms: number;
}}

export const kMotion: readonly ExprMotionRow[] = [
{chr(10).join(motion_rows)}
] as const;

/** Same sentinel as firmware `FaceConfig::kBobAmpFollowEmotionArm` (int16 0x8000). */
export const BOB_AMP_FOLLOW_EMOTION_ARM = -32768;

export enum GazeStyle {{
  Off = 0,
  IdleRandom,
  Orbit,
  ScanX,
}}

export interface IdleAnimRow {{
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
}}

export const kIdleAnim: readonly IdleAnimRow[] = [
{chr(10).join(idle_rows)}
] as const;

{emotion_sim}
{frame_anim}
{verb_sim}
{motion_rt}

/** `Face::kVerbTransitionDurMs` / `VerbTimeline.h` */
export const kVerbTransitionDurMs = 500 as const;

export function expressionIndexFromName(name: string): number {{
  const i = EXPRESSIONS.indexOf(name as ExpressionName);
  return i >= 0 ? i : 0;
}}

export function isEmotionExpressionIndex(idx: number): boolean {{
  return idx >= 0 && idx < kExpressionIsEmotion.length && !!kExpressionIsEmotion[idx];
}}

export function armPresetForExpressionIndex(idx: number): ArmPreset {{
  if (idx < 0 || idx >= kArmPresets.length) return kArmPresets[0]!;
  return kArmPresets[idx]!;
}}

export function expressionForNamedEmotionIndex(namedIdx: number): number {{
  if (namedIdx < 0 || namedIdx >= kNamedEmotionToExpressionIndex.length) return 0;
  return kNamedEmotionToExpressionIndex[namedIdx]!;
}}
'''

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(out, encoding="utf-8")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
