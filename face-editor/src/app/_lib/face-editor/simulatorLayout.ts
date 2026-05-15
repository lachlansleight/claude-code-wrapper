import type { ParamField } from "../face-engine/faceParams";
import { PARAM_UI_SECTIONS, paramFieldLabel } from "../face-engine/faceParams";

export type SliderRowDef = [
  field: string,
  label: string,
  min: number,
  max: number,
  step: number,
  reversed?: boolean,
];

export interface SliderSectionDef {
  section: string;
  groups: SliderRowDef[][];
}

/** Slider min / max / step (and optional RTL range) — labels come from `paramFieldLabel`. */
const PARAM_SLIDER_RANGE: Record<
  ParamField,
  readonly [min: number, max: number, step: number, reversed?: boolean]
> = {
  eye_dy: [-30, 30, 1],
  mouth_dy: [-30, 30, 1],
  face_y: [-30, 30, 1],
  face_rot: [-90, 90, 1],
  eye_rx: [0, 35, 1],
  pupil_r: [0, 50, 1],
  eye_open_amt: [0, 50, 1],
  eye_arc_amt: [-200, 200, 1],
  pupil_dx: [-50, 50, 1],
  pupil_dy: [-50, 50, 1],
  eye_wave_amp: [0, 20, 1],
  eye_wave_freq: [0, 100, 1],
  eye_wave_speed: [0, 1000, 10],
  eye_thick: [0, 10, 1],
  mouth_rx: [0, 100, 1],
  mouth_open_amt: [0, 50, 1],
  mouth_arc_amt: [-200, 200, 1],
  mouth_wave_amp: [0, 20, 1],
  mouth_wave_freq: [0, 100, 1],
  mouth_wave_speed: [0, 1000, 10],
  mouth_thick: [0, 10, 1],
  ring_r: [0, 255, 1],
  ring_g: [0, 255, 1],
  ring_b: [0, 255, 1],
};

function sliderRowForField(f: ParamField): SliderRowDef {
  const spec = PARAM_SLIDER_RANGE[f]!;
  const [min, max, step, reversed] = spec;
  if (reversed !== undefined) {
    return [f, paramFieldLabel(f), min, max, step, reversed];
  }
  return [f, paramFieldLabel(f), min, max, step];
}

export const PARAM_LAYOUT: SliderSectionDef[] = PARAM_UI_SECTIONS.map(
  (sec) => ({
    section: sec.section,
    groups: sec.groups.map((grp) => grp.map((f) => sliderRowForField(f))),
  }),
);

export const MOD_LAYOUT: SliderRowDef[][] = [
  [
    ["blinkAmt", "Blink", 0, 1, 0.01],
    ["gdx", "Gaze X", -50, 50, 1],
    ["gdy", "Gaze Y", -50, 50, 1],
  ],
];

export const EMOTION_COLOR: Record<string, string> = {
  Neutral: "#8b93a7",
  Happy: "#58d68d",
  Excited: "#6ea8ff",
  Joyful: "#ffd166",
  Sad: "#ff7b7b",
  Sleepy: "#5a6a8c",
  Distressed: "#e8944a",
  Blissed: "#c9a0ff",
  Depressed: "#6b7588",
  Shocked: "#5cd4f0",
  Disappointed: "#a0826e",
  Cheeky: "#f2b5c6",
  Gleeful: "#b8f25c",
  Frustrated: "#d26a37",
};

export const VERB_MAP: Record<string, string> = {
  VerbThinking: "thinking",
  VerbReading: "reading",
  VerbWriting: "writing",
  VerbExecuting: "executing",
  VerbStraining: "straining",
  VerbSleeping: "sleeping",
};

export const OVERLAY_MAP: Record<string, string> = {
  OverlayWaking: "waking",
  OverlayAttention: "attracting_attention",
};

export function buildParamRanges(): Record<
  string,
  { label: string; min: number; max: number; step: number }
> {
  const PARAM_RANGES: Record<
    string,
    { label: string; min: number; max: number; step: number }
  > = {};
  for (const sec of PARAM_LAYOUT) {
    for (const grp of sec.groups) {
      for (const [field, label, min, max, step] of grp) {
        PARAM_RANGES[field] = { label, min, max, step };
      }
    }
  }
  return PARAM_RANGES;
}

export function buildModRanges(): Record<
  string,
  { label: string; min: number; max: number; step: number }
> {
  const MOD_RANGES: Record<
    string,
    { label: string; min: number; max: number; step: number }
  > = {};
  for (const grp of MOD_LAYOUT) {
    for (const [field, label, min, max, step] of grp) {
      MOD_RANGES[field] = { label, min, max, step };
    }
  }
  return MOD_RANGES;
}

export function isEmotionExprName(name: string): boolean {
  return !name.startsWith("Verb") && !name.startsWith("Overlay");
}
