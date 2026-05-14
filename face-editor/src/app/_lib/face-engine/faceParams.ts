/**
 * Ordered keys matching firmware `FrameController::kBaseTargets` row layout.
 */
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

export type FaceParams = { [K in ParamField]: number };
