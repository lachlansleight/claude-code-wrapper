/**
 * Ordered keys matching firmware `FrameController::kBaseTargets` row layout
 * (`Face::FieldIndex` order — keep aligned with `FACE_CONFIG_DATA.ts`).
 */
import type { FaceParamsIndexed, ParamI16 } from "./FACE_CONFIG_DATA";
import { FieldIndex } from "./FACE_CONFIG_DATA";

export const PARAM_FIELDS = [
  "eye_dy",
  "eye_rx",
  "eye_open_amt",
  "eye_arc_amt",
  "eye_thick",
  "eye_wave_amp",
  "eye_wave_freq",
  "eye_wave_speed",
  "pupil_dx",
  "pupil_dy",
  "pupil_r",
  "mouth_dy",
  "mouth_rx",
  "mouth_open_amt",
  "mouth_arc_amt",
  "mouth_thick",
  "mouth_wave_amp",
  "mouth_wave_freq",
  "mouth_wave_speed",
  "face_rot",
  "face_y",
  "ring_r",
  "ring_g",
  "ring_b",
] as const;

export type ParamField = (typeof PARAM_FIELDS)[number];

/** Human-readable labels (verb-timeline wording where it differs from short slider text). */
export const PARAM_FIELD_LABEL = {
  eye_dy: "Eye Position",
  eye_rx: "Eye Width",
  eye_open_amt: "Eye Open",
  eye_arc_amt: "Eye Arc",
  eye_thick: "Eye Thickness",
  eye_wave_amp: "Eye Wave Amplitude",
  eye_wave_freq: "Eye Wave Frequency",
  eye_wave_speed: "Eye Wave Speed",
  pupil_dx: "Look X",
  pupil_dy: "Look Y",
  pupil_r: "Pupil Size",
  mouth_dy: "Mouth Position",
  mouth_rx: "Mouth Width",
  mouth_open_amt: "Mouth Open",
  mouth_arc_amt: "Mouth Arc",
  mouth_thick: "Mouth Thickness",
  mouth_wave_amp: "Mouth Wave Amplitude",
  mouth_wave_freq: "Mouth Wave Frequency",
  mouth_wave_speed: "Mouth Wave Speed",
  face_rot: "Face Rotation",
  face_y: "Face Position",
  ring_r: "Ring Red",
  ring_g: "Ring Green",
  ring_b: "Ring Blue",
} as const satisfies Record<ParamField, string>;

export function paramFieldLabel(f: ParamField): string {
  return PARAM_FIELD_LABEL[f];
}

/** Inspector / verb-timeline row order — sections and groups match `ParamSliderGrid` spacing. */
export interface ParamUiSection {
  readonly section: string;
  readonly groups: readonly (readonly ParamField[])[];
}

export const PARAM_UI_SECTIONS: readonly ParamUiSection[] = [
  {
    section: "Positioning",
    groups: [
      ["eye_dy", "mouth_dy"],
      ["face_y", "face_rot"],
    ],
  },
  {
    section: "Eyes",
    groups: [
      ["eye_rx", "pupil_r"],
      ["eye_open_amt", "eye_arc_amt"],
      ["pupil_dx", "pupil_dy"],
      ["eye_wave_amp", "eye_wave_freq", "eye_wave_speed"],
      ["eye_thick"],
    ],
  },
  {
    section: "Mouth",
    groups: [
      ["mouth_rx"],
      ["mouth_open_amt", "mouth_arc_amt"],
      ["mouth_wave_amp", "mouth_wave_freq", "mouth_wave_speed"],
      ["mouth_thick"],
    ],
  },
  {
    section: "Ring",
    groups: [["ring_r", "ring_g", "ring_b"]],
  },
] as const;

function flattenParamUiFields(
  sections: readonly ParamUiSection[],
): ParamField[] {
  const out: ParamField[] = [];
  for (const s of sections) {
    for (const g of s.groups) {
      for (const f of g) out.push(f);
    }
  }
  return out;
}

/** All face params in UI (inspector) order — use for readouts; keep `PARAM_FIELDS` for firmware row order. */
export const PARAM_FIELDS_UI_ORDER: readonly ParamField[] =
  flattenParamUiFields(PARAM_UI_SECTIONS);

(function validateParamUiLayout(): void {
  if (PARAM_FIELDS_UI_ORDER.length !== PARAM_FIELDS.length) {
    throw new Error(
      `PARAM_UI_SECTIONS lists ${PARAM_FIELDS_UI_ORDER.length} fields; expected ${PARAM_FIELDS.length}.`,
    );
  }
  const seen = new Set<string>();
  for (const f of PARAM_FIELDS_UI_ORDER) {
    if (seen.has(f)) {
      throw new Error(`PARAM_UI_SECTIONS: duplicate field ${f}`);
    }
    seen.add(f);
  }
  for (const f of PARAM_FIELDS) {
    if (!seen.has(f)) {
      throw new Error(`PARAM_UI_SECTIONS: missing field ${f}`);
    }
  }
})();

export type FaceParams = { [K in ParamField]: number };

export function faceParamsFromIndexed(row: readonly ParamI16[]): FaceParams {
  const o = {} as FaceParams;
  for (let i = 0; i < PARAM_FIELDS.length; ++i) {
    o[PARAM_FIELDS[i]!] = row[i]!.value;
  }
  return o;
}

/** Per-field strength 0–100 from an indexed geometry row (emotion base or blended cell). */
export function faceStrengthsFromIndexed(row: readonly ParamI16[]): FaceParams {
  const o = {} as FaceParams;
  for (let i = 0; i < PARAM_FIELDS.length; ++i) {
    let s = Math.round(row[i]!.strength);
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    o[PARAM_FIELDS[i]!] = s;
  }
  return o;
}

/** Update only `value` on existing cells; preserves each field's `strength`. */
export function mergeValuesIntoIndexedRow(
  row: ParamI16[],
  partial: Partial<FaceParams>,
): void {
  for (const k of Object.keys(partial) as ParamField[]) {
    const v = partial[k];
    if (typeof v !== "number" || Number.isNaN(v)) continue;
    const i = PARAM_FIELDS.indexOf(k);
    if (i < 0) continue;
    const cell = row[i]!;
    row[i] = { value: Math.round(v), strength: cell.strength };
  }
}

/** Update only `strength` (clamped 0–100); preserves each field's `value`. */
export function mergeStrengthsIntoIndexedRow(
  row: ParamI16[],
  partial: Partial<FaceParams>,
): void {
  for (const k of Object.keys(partial) as ParamField[]) {
    const s = partial[k];
    if (typeof s !== "number" || Number.isNaN(s)) continue;
    const i = PARAM_FIELDS.indexOf(k);
    if (i < 0) continue;
    const cell = row[i]!;
    let clamped = Math.round(s);
    if (clamped < 0) clamped = 0;
    if (clamped > 100) clamped = 100;
    row[i] = { value: cell.value, strength: clamped };
  }
}

export function indexedFromFaceParams(p: FaceParams): ParamI16[] {
  return PARAM_FIELDS.map((k) => ({ value: p[k] ?? 0, strength: 100 }));
}

export function fieldIndexFromParamField(f: ParamField): FieldIndex {
  const i = PARAM_FIELDS.indexOf(f);
  if (i < 0) return FieldIndex.Count;
  return i as FieldIndex;
}

export function paramFieldFromFieldIndex(i: FieldIndex): ParamField | null {
  const idx = i as number;
  if (idx < 0 || idx >= PARAM_FIELDS.length) return null;
  return PARAM_FIELDS[idx]!;
}
