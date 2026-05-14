import { PARAM_FIELDS, type FaceParams, type ParamField } from "./faceParams";

export { PARAM_FIELDS, type FaceParams, type ParamField } from "./faceParams";

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

export type ExpressionName = (typeof EXPRESSIONS)[number];

const EMOTION_NAMES = new Set<string>([
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
]);

export function isEmotionExpression(name: string): boolean {
  return EMOTION_NAMES.has(name);
}

/** Hand-tuned rows; order matches PARAM_FIELDS (ported from frame-controller-v3.js). */
const NEW_TARGETS: Record<string, readonly number[]> = {
  Neutral: [
    +3, +30, +26, 0, +3, 0, 0, 0, 0, +3, +15, +2, +15, +1, 0, +3, 0, 0, 0, 0,
    +3, 0, 0, 0,
  ],
  Happy: [
    +7, +30, +23, 0, +3, 0, 0, 0, 0, +5, +16, +2, +24, +2, +20, +3, 0, 0, 0, 0,
    +7, 0, 0, 0,
  ],
  Excited: [
    -1, +29, +28, 0, +3, 0, 0, 0, 0, 0, +17, +3, +27, +2, +48, +3, 0, 0, 0, 0,
    +3, +40, +252, +79,
  ],
  Joyful: [
    -11, +20, +2, -64, +4, 0, 0, 0, 0, 0, +14, +2, +37, +14, +69, +4, 0, 0, 0,
    0, -13, +255, +228, +38,
  ],
  Sad: [
    +7, +28, +15, 0, +3, 0, 0, 0, 0, +3, +11, -6, +20, +1, -14, +3, 0, +1, +3,
    0, +6, +4, +1, +3,
  ],
  VerbThinking: [
    +2, +28, +26, 0, +3, 0, 0, 0, +9, -8, +15, +1, +12, +1, +15, +3, 0, 0, 0,
    -12, +2, 36, 56, 120,
  ],
  VerbReading: [
    +1, +27, +24, 0, +3, 0, 0, 0, 0, +13, +15, 0, +13, +1, +19, +3, 0, 0, 0, 0,
    +18, 78, 146, 210,
  ],
  VerbWriting: [
    0, +28, +25, +24, +3, 0, 0, 0, 0, -9, +15, 0, +19, +7, +31, +3, 0, 0, 0, 0,
    -13, 104, 118, 228,
  ],
  VerbExecuting: [
    0, +30, +13, 0, +3, 0, 0, 0, 0, -3, +11, 0, +11, +1, +13, +3, 0, +55, 0, 0,
    +1, 156, 64, 216,
  ],
  VerbStraining: [
    +1, +30, +22, 0, +3, 0, +3, +25, 0, -3, +10, +1, +18, +1, 0, +3, +4, +96,
    +364, 0, 0, 210, 75, 220,
  ],
  VerbSleeping: [
    +2, +30, 0, +15, +3, 0, 0, +1, 0, +3, +15, +1, +15, 0, 0, +3, 0, 0, +1, 0,
    +17, 0, 0, 0,
  ],
  OverlayWaking: [
    +2, +31, +34, 0, +3, 0, 0, +1, 0, +3, +13, +1, +9, +12, 0, +3, 0, 0, +1, 0,
    -2, 0, 0, 0,
  ],
  OverlayAttention: [
    +3, +30, +31, 0, +3, 0, +83, +707, 0, +3, +12, 0, +17, +13, +26, +1, 0, +48,
    +707, 0, 0, 255, 20, 40,
  ],
  Sleepy: [
    +15, +28, +17, +24, +3, 0, 0, 0, 0, -13, +15, +2, +13, +2, +8, +3, 0, +17,
    +90, 0, +13, 0, 0, 0,
  ],
  Distressed: [
    +5, +30, +30, 0, +3, 0, 0, 0, 0, +7, +10, -5, +24, +5, -46, +3, 0, 0, 0, 0,
    -17, +255, +48, +24,
  ],
  Blissed: [
    +4, +20, 0, +24, +3, 0, 0, 0, 0, 0, +15, +8, +26, +6, +29, +3, 0, 0, 0, 0,
    +7, 0, 0, 0,
  ],
  Depressed: [
    +23, +30, +8, +74, +3, 0, 0, 0, 0, -2, +6, +4, +13, 0, -11, +3, 0, +17, +90,
    0, +9, 0, 0, 0,
  ],
  Shocked: [
    +2, +30, +37, 0, +3, +1, +85, +720, 0, +3, +9, +15, +17, +13, 0, +1, +2,
    +49, +720, 0, 0, +255, +255, +255,
  ],
  Disappointed: [
    +5, +21, 0, +27, +3, 0, 0, 0, 0, +3, +8, -15, +25, +2, -21, +3, 0, 0, +2, 0,
    0, +225, +53, +93,
  ],
  Cheeky: [
    -10, +28, +17, -131, +3, 0, 0, 0, 0, +3, +15, -18, +20, +2, +34, +3, 0, 0,
    0, 0, -3, 0, 0, 0,
  ],
  Gleeful: [
    -10, +27, +13, -137, +3, 0, 0, +1, 0, +1, +14, -17, +27, +7, +61, +3, 0, 0,
    +1, 0, +2, +39, +248, +78,
  ],
  Frustrated: [
    +1, +30, +22, 0, +3, 0, +1, +5, 0, -3, +10, 0, +18, 0, 0, +3, +4, +96, +348,
    0, +1, +212, +75, +212,
  ],
};

const neutralRow = NEW_TARGETS.Neutral!;

const BASE_TARGETS: Record<string, number[]> = {};
for (const name of EXPRESSIONS) {
  const row = NEW_TARGETS[name];
  BASE_TARGETS[name] = row ? [...row] : [...neutralRow];
}

export function arrToParams(a: readonly number[]): FaceParams {
  const o = {} as FaceParams;
  PARAM_FIELDS.forEach((k, i) => {
    o[k] = a[i] ?? 0;
  });
  return o;
}

export function baseTargetForExpression(name: string): FaceParams {
  const a = BASE_TARGETS[name] ?? BASE_TARGETS.Neutral;
  return arrToParams(a);
}

export function paramFieldsList(): readonly ParamField[] {
  return PARAM_FIELDS;
}

export function expressionsList(): readonly string[] {
  return [...EXPRESSIONS];
}
