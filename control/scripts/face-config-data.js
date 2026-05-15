// JS sibling of robot_v3/src/face/FACE_CONFIG_DATA.h. Hand-mirrored — when
// you edit one, edit the other in lockstep. The simulator reads everything
// it needs from `window.FaceConfigData` so this is the single source of
// face-data truth on the JS side.
//
// Field-for-field correspondence with the C++ tables:
//
//   PARAM_FIELDS                  ↔ Face::FieldIndex
//   Expression / EMOTIONS / ...   ↔ Face::Expression / EmotionSystem::NamedEmotion
//   expressionIsEmotion           ↔ FaceConfig::kExpressionIsEmotion
//   namedEmotionToExpression      ↔ FaceConfig::kNamedEmotionToExpression
//   emotionPoints                 ↔ FaceConfig::kEmotionPoints
//   baseTargets                   ↔ FaceConfig::kBaseTargets   (emotions only;
//                                    verbs come from verbTimelines, overlays
//                                    from overlayPresets — sim approximates
//                                    overlays as scalar presets since firmware
//                                    paints them via EffectsRenderer)
//   verbTimelines                 ↔ FaceConfig::kVerbTimelines
//   armPresets                    ↔ FaceConfig::kArmPresets    (emotion rows)
//   motion                        ↔ FaceConfig::kMotion
//   idleAnim                      ↔ FaceConfig::kIdleAnim
//   emotionSim / frameAnim /
//     verbSim                     ↔ kEmotionSim / kFrameAnim / kVerbSim
//
// Strength on emotion preset rows is implicit 100 (mirrors the FACE_P macro
// in firmware). Verb sparse-override entries carry explicit strength so they
// can ramp via the new emotion+verb combine curve.

(function () {
  // ---- Field index / param names (Face::FieldIndex order) ----------------
  const PARAM_FIELDS = [
    "eye_dy", "eye_rx",
    "eye_open_amt", "eye_arc_amt", "eye_thick",
    "eye_wave_amp", "eye_wave_freq", "eye_wave_speed",
    "pupil_dx", "pupil_dy", "pupil_r",
    "mouth_dy", "mouth_rx",
    "mouth_open_amt", "mouth_arc_amt", "mouth_thick",
    "mouth_wave_amp", "mouth_wave_freq", "mouth_wave_speed",
    "face_rot", "face_y",
    "ring_r", "ring_g", "ring_b",
  ];

  // ---- Expression / NamedEmotion enums -----------------------------------
  // Order MUST match Face::Expression in FACE_CONFIG_DATA.h.
  const EXPRESSIONS = [
    "Neutral", "Happy", "Excited", "Joyful", "Sad",
    "VerbThinking", "VerbReading", "VerbWriting", "VerbExecuting", "VerbStraining", "VerbSleeping",
    "VerbWaking", "VerbAttractingAttention",
    "Sleepy", "Distressed", "Blissed", "Depressed", "Shocked",
    "Disappointed", "Cheeky", "Gleeful", "Frustrated",
  ];
  const EMOTIONS = [
    "Neutral", "Happy", "Excited", "Joyful", "Sad",
    "Sleepy", "Distressed", "Blissed", "Depressed", "Shocked",
    "Disappointed", "Cheeky", "Gleeful", "Frustrated",
  ];
  const VERBS = [
    "VerbThinking", "VerbReading", "VerbWriting",
    "VerbExecuting", "VerbStraining", "VerbSleeping",
  ];
  const OVERLAYS = ["VerbWaking", "VerbAttractingAttention"];

  const expressionIsEmotion = {};
  EXPRESSIONS.forEach((e) => (expressionIsEmotion[e] = EMOTIONS.includes(e)));

  // 1:1 with the firmware lookup table. Identity in the simulator (we already
  // use Expression names everywhere) but kept for parity / discoverability.
  const namedEmotionToExpression = {};
  EMOTIONS.forEach((e) => (namedEmotionToExpression[e] = e));

  // ---- (v, a) anchors (kEmotionPoints) -----------------------------------
  const emotionPoints = {
    Neutral:      { v:  0.0, a: 0.5 },
    Happy:        { v:  0.5, a: 0.5 },
    Excited:      { v:  1.0, a: 0.6 },
    Joyful:       { v:  1.0, a: 1.0 },
    Sad:          { v: -0.5, a: 0.5 },
    Sleepy:       { v: -0.2, a: 0.0 },
    Distressed:   { v: -1.0, a: 1.0 },
    Blissed:      { v:  1.0, a: 0.0 },
    Depressed:    { v: -1.0, a: 0.0 },
    Shocked:      { v: -0.3, a: 1.0 },
    Disappointed: { v: -1.0, a: 0.3 },
    Cheeky:       { v:  0.5, a: 0.7 },
    Gleeful:      { v:  0.6, a: 1.0 },
    Frustrated:   { v: -0.6, a: 0.8 },
  };

  // ---- Per-expression FaceParams presets (kBaseTargets) ------------------
  // Emotion rows: strength implicit 100. Verb/overlay rows mirror firmware
  // tuned geometry (open_amt / arc_amt); simulator uses baseTargets for verbs
  // when not sampling sparse timelines the same way — keep in lockstep.
  const baseTargets = {
    Neutral:      [  3, 30,  26, 0, 3,  0, 0, 0,  0, 3, 15,  2, 15, 1, 0, 3,  0, 0, 0,  0, 3,  0, 0, 0 ],
    Happy:        [  7, 30,  23, 0, 3,  0, 0, 0,  0, 5, 16,  2, 24, 2, 20, 3,  0, 0, 0,  0, 7,  0, 0, 0 ],
    Excited:      [ -1, 29,  28, 0, 3,  0, 0, 0,  0, 0, 17,  3, 27, 2, 48, 3,  0, 0, 0,  0, 3, 40, 252, 79 ],
    Joyful:       [ -11, 20, 2, -64, 4,  0, 0, 0,  0, 0, 14,  2, 37, 14, 69, 4,  0, 0, 0,  0, -13, 255, 228, 38 ],
    Sad:          [  7, 28,  15, 0, 3,  0, 0, 0,  0, 3, 11, -6, 20, 1, -14, 3,  0, 1, 3,  0, 6,  4, 1, 3 ],
    Sleepy:       [ 15, 28, 17, 24, 3,  0, 0, 0,  0, -13, 15,  2, 13, 2, 8, 3,  0, 17, 90,  0, 13, 0, 0, 0 ],
    Distressed:   [  5, 30,  30, 0, 3,  0, 0, 0,  0, 7, 10, -5, 24, 5, -46, 3,  0, 0, 0,  0, -17, 255, 48, 24 ],
    Blissed:      [  4, 20,  0, 24, 3,  0, 0, 0,  0, 0, 15,  8, 26, 6, 29, 3,  0, 0, 0,  0, 7,  0, 0, 0 ],
    Depressed:    [ 23, 30,  8, 74, 3,  0, 0, 0,  0, -2, 6,  4, 13, 0, -11, 3,  0, 17, 90,  0, 9,  0, 0, 0 ],
    Shocked:      [  2, 30,  37, 0, 3,  1, 85, 720,  0, 3, 9,  15, 17, 13, 0, 1,  2, 49, 720,  0, 0, 255, 255, 255 ],
    Disappointed: [  5, 21,  0, 27, 3,  0, 0, 0,  0, 3, 8, -15, 25, 2, -21, 3,  0, 0, 2,  0, 0, 225, 53, 93 ],
    Cheeky:       [ -10, 28, 17, -131, 3,  0, 0, 0,  0, 3, 15, -18, 20, 2, 34, 3,  0, 0, 0,  0, -3, 0, 0, 0 ],
    Gleeful:      [ -10, 27, 13, -137, 3,  0, 0, 1,  0, 1, 14, -17, 27, 7, 61, 3,  0, 0, 1,  0, 2, 39, 248, 78 ],
    Frustrated:   [  1, 30,  22, 0, 3,  0, 1, 5,  0, -3, 10,  0, 18, 0, 0, 3,  4, 96, 348,  0, 1, 212, 75, 212 ],
  };

  // Simulator-only: rough scalar presets used in place of the firmware's
  // EffectsRenderer rim animations.
  const overlayPresets = {
    VerbWaking:    [  2, 31,  34, 0, 3,  0, 0, 1,  0, 3, 13,  1, 9, 12, 0, 3,  0, 0, 1,  0, -2, 0, 0, 0 ],
    VerbAttractingAttention: [  3, 30,  31, 0, 3,  0, 83, 707,  0, 3, 12,  0, 17, 13, 26, 1,  0, 48, 707,  0, 0, 255, 20, 40 ],
  };

  // ---- Verb sparse override timelines (kVerbTimelines) -------------------
  // Each entry: { field: <PARAM_FIELDS name>, value, strength }.
  function ov(field, value, strength) {
    return { field, value, strength: strength === undefined ? 100 : strength };
  }
  const verbTimelines = {
    VerbThinking: [
      ov("eye_dy", 2), ov("eye_rx", 28),
      ov("eye_open_amt", 26), ov("eye_arc_amt", 0),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 0), ov("eye_wave_speed", 0),
      ov("pupil_dx", 9), ov("pupil_dy", -8), ov("pupil_r", 15),
      ov("mouth_dy", 1), ov("mouth_rx", 12),
      ov("mouth_open_amt", 1), ov("mouth_arc_amt", 15),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 0), ov("mouth_wave_freq", 0), ov("mouth_wave_speed", 0),
      ov("face_rot", -12), ov("face_y", 2),
      ov("ring_r", 36), ov("ring_g", 56), ov("ring_b", 120),
    ],
    VerbReading: [
      ov("eye_dy", 1), ov("eye_rx", 27),
      ov("eye_open_amt", 24), ov("eye_arc_amt", 0),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 0), ov("eye_wave_speed", 0),
      ov("pupil_dx", 0), ov("pupil_dy", 13), ov("pupil_r", 15),
      ov("mouth_dy", 0), ov("mouth_rx", 13),
      ov("mouth_open_amt", 1), ov("mouth_arc_amt", 19),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 0), ov("mouth_wave_freq", 0), ov("mouth_wave_speed", 0),
      ov("face_rot", 0), ov("face_y", 18),
      ov("ring_r", 78), ov("ring_g", 146), ov("ring_b", 210),
    ],
    VerbWriting: [
      ov("eye_dy", 0), ov("eye_rx", 28),
      ov("eye_open_amt", 25), ov("eye_arc_amt", 24),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 0), ov("eye_wave_speed", 0),
      ov("pupil_dx", 0), ov("pupil_dy", -9), ov("pupil_r", 15),
      ov("mouth_dy", 0), ov("mouth_rx", 19),
      ov("mouth_open_amt", 7), ov("mouth_arc_amt", 31),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 0), ov("mouth_wave_freq", 0), ov("mouth_wave_speed", 0),
      ov("face_rot", 0), ov("face_y", -13),
      ov("ring_r", 104), ov("ring_g", 118), ov("ring_b", 228),
    ],
    VerbExecuting: [
      ov("eye_dy", 0), ov("eye_rx", 30),
      ov("eye_open_amt", 13), ov("eye_arc_amt", 0),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 0), ov("eye_wave_speed", 0),
      ov("pupil_dx", 0), ov("pupil_dy", -3), ov("pupil_r", 11),
      ov("mouth_dy", 0), ov("mouth_rx", 11),
      ov("mouth_open_amt", 1), ov("mouth_arc_amt", 13),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 0), ov("mouth_wave_freq", 55), ov("mouth_wave_speed", 0),
      ov("face_rot", 0), ov("face_y", 1),
      ov("ring_r", 156), ov("ring_g", 64), ov("ring_b", 216),
    ],
    VerbStraining: [
      ov("eye_dy", 1), ov("eye_rx", 30),
      ov("eye_open_amt", 22), ov("eye_arc_amt", 0),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 3), ov("eye_wave_speed", 25),
      ov("pupil_dx", 0), ov("pupil_dy", -3), ov("pupil_r", 10),
      ov("mouth_dy", 1), ov("mouth_rx", 18),
      ov("mouth_open_amt", 1), ov("mouth_arc_amt", 0),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 4), ov("mouth_wave_freq", 96), ov("mouth_wave_speed", 364),
      ov("face_rot", 0), ov("face_y", 0),
      ov("ring_r", 210), ov("ring_g", 75), ov("ring_b", 220),
    ],
    VerbSleeping: [
      ov("eye_dy", 2), ov("eye_rx", 30),
      ov("eye_open_amt", 0), ov("eye_arc_amt", 15),
      ov("eye_thick", 3),
      ov("eye_wave_amp", 0), ov("eye_wave_freq", 0), ov("eye_wave_speed", 1),
      ov("pupil_dx", 0), ov("pupil_dy", 3), ov("pupil_r", 15),
      ov("mouth_dy", 1), ov("mouth_rx", 15),
      ov("mouth_open_amt", 0), ov("mouth_arc_amt", 0),
      ov("mouth_thick", 3),
      ov("mouth_wave_amp", 0), ov("mouth_wave_freq", 0), ov("mouth_wave_speed", 1),
      ov("face_rot", 0), ov("face_y", 17),
      ov("ring_r", 0), ov("ring_g", 0), ov("ring_b", 0),
    ],
  };

  // ---- Arm presets per emotion (kArmPresets) -----------------------------
  // Only emotion rows — verbs/overlays drive arm via the motion table below.
  const armPresets = {
    Neutral:      { min_offset_deg: -25, max_offset_deg: -15, period_s: 2.0, interval_s: 1.0 },
    Happy:        { min_offset_deg: -23, max_offset_deg:  -7, period_s: 1.5, interval_s: 0.5 },
    Excited:      { min_offset_deg: -15, max_offset_deg:  -5, period_s: 1.0, interval_s: 0.0 },
    Joyful:       { min_offset_deg:  10, max_offset_deg:  25, period_s: 0.9, interval_s: 0.2 },
    Sad:          { min_offset_deg: -25, max_offset_deg: -15, period_s: 2.0, interval_s: 1.0 },
    Sleepy:       { min_offset_deg: -25, max_offset_deg: -20, period_s: 3.0, interval_s: 6.0 },
    Distressed:   { min_offset_deg: -15, max_offset_deg:  -5, period_s: 1.0, interval_s: 0.0 },
    Blissed:      { min_offset_deg: -25, max_offset_deg: -20, period_s: 3.0, interval_s: 6.0 },
    Depressed:    { min_offset_deg: -25, max_offset_deg: -20, period_s: 3.0, interval_s: 6.0 },
    Shocked:      { min_offset_deg: -15, max_offset_deg:  -5, period_s: 1.0, interval_s: 0.0 },
    Disappointed: { min_offset_deg: -23, max_offset_deg:  -7, period_s: 1.5, interval_s: 0.5 },
    Cheeky:       { min_offset_deg: -20, max_offset_deg:  -5, period_s: 1.4, interval_s: 0.45 },
    Gleeful:      { min_offset_deg:  10, max_offset_deg:  25, period_s: 0.9, interval_s: 0.2 },
    Frustrated:   { min_offset_deg: -18, max_offset_deg:  -8, period_s: 1.1, interval_s: 0.15 },
  };

  // ---- Per-expression motion mode (kMotion) ------------------------------
  const motion = {
    Neutral:          { mode: "RandomDrift", center: -20, amplitude: 5,  period_ms: 5000, period_jitter_ms: 5000, slew_ms: 500 },
    Happy:            { mode: "RandomDrift", center: -15, amplitude: 8,  period_ms: 2000, period_jitter_ms: 1000, slew_ms: 500 },
    Excited:          { mode: "Oscillate",   center: -10, amplitude: 5,  period_ms: 1000, period_jitter_ms: 0,    slew_ms: 0   },
    Joyful:           { mode: "Waggle",      center:   0, amplitude: 15, period_ms:  900, period_jitter_ms: 0,    slew_ms: 0   },
    Sad:              { mode: "None",        center:   0, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    VerbThinking:     { mode: "Thinking",    center: -15, amplitude: 5,  period_ms: 2000, period_jitter_ms: 0,    slew_ms: 0   },
    VerbReading:      { mode: "Static",      center:  -8, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    VerbWriting:      { mode: "Oscillate",   center:   5, amplitude: 4,  period_ms:  840, period_jitter_ms: 0,    slew_ms: 250 },
    VerbExecuting:    { mode: "Oscillate",   center:  -5, amplitude: 5,  period_ms: 1000, period_jitter_ms: 0,    slew_ms: 0   },
    VerbStraining:    { mode: "Oscillate",   center:   0, amplitude: 5,  period_ms:  750, period_jitter_ms: 0,    slew_ms: 0   },
    VerbSleeping:     { mode: "Oscillate",   center: -20, amplitude: 5,  period_ms: 8000, period_jitter_ms: 0,    slew_ms: 0   },
    VerbWaking:    { mode: "Static",      center:  18, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    VerbAttractingAttention: { mode: "Waggle",      center:   0, amplitude: 15, period_ms:  900, period_jitter_ms: 0,    slew_ms: 0   },
    Sleepy:           { mode: "Oscillate",   center: -18, amplitude: 4,  period_ms: 5000, period_jitter_ms: 0,    slew_ms: 0   },
    Distressed:       { mode: "Oscillate",   center:   0, amplitude: 6,  period_ms:  900, period_jitter_ms: 0,    slew_ms: 0   },
    Blissed:          { mode: "RandomDrift", center: -10, amplitude: 6,  period_ms: 3000, period_jitter_ms: 1500, slew_ms: 500 },
    Depressed:        { mode: "None",        center:   0, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    Shocked:          { mode: "Static",      center:   0, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    Disappointed:     { mode: "None",        center:   0, amplitude: 0,  period_ms:    0, period_jitter_ms: 0,    slew_ms: 0   },
    Cheeky:           { mode: "Waggle",      center:   0, amplitude: 12, period_ms:  880, period_jitter_ms: 0,    slew_ms: 0   },
    Gleeful:          { mode: "Waggle",      center:   0, amplitude: 15, period_ms:  900, period_jitter_ms: 0,    slew_ms: 0   },
    Frustrated:       { mode: "Oscillate",   center:   0, amplitude: 6,  period_ms:  820, period_jitter_ms: 0,    slew_ms: 0   },
  };

  // ---- Per-expression idle anim (kIdleAnim) ------------------------------
  // bob_amplitude_px === BOB_AMP_FOLLOW_EMOTION_ARM means "follow the
  // emotion-arm waggle heuristic"; the simulator falls back to
  // frameAnim.emotion_bob_amp_follow_arm when that's the case AND the active
  // emotion has a non-flat waggle range.
  const BOB_AMP_FOLLOW_EMOTION_ARM = -32768;
  const FOLLOW = BOB_AMP_FOLLOW_EMOTION_ARM;
  const idleAnim = {
    Neutral:          { blink_period_min_ms: 4000, blink_period_max_ms: 6499, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "IdleRandom", gaze_move_ms: 200, gaze_rand_span_x: 15, gaze_rand_span_y: 10, gaze_reroll_min_ms: 1000, gaze_reroll_max_ms: 10000, gaze_scan_period_ms: 0,    gaze_amp_x: 0, gaze_amp_y: 0 },
    Happy:            { blink_period_min_ms: 3000, blink_period_max_ms: 4499, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "ScanX",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 5500, gaze_amp_x: 2, gaze_amp_y: 0 },
    Excited:          { blink_period_min_ms: 2500, blink_period_max_ms: 3999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Orbit",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 3500, gaze_amp_x: 3, gaze_amp_y: 2 },
    Joyful:           { blink_period_min_ms:    0, blink_period_max_ms:    0, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Sad:              { blink_period_min_ms:    0, blink_period_max_ms:    0, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    VerbThinking:     { blink_period_min_ms: 2000, blink_period_max_ms: 3499, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  0,     gaze_style: "Orbit",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:  900, gaze_amp_x: 2, gaze_amp_y: 2 },
    VerbReading:      { blink_period_min_ms: 4000, blink_period_max_ms: 5999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  0,     gaze_style: "ScanX",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 1300, gaze_amp_x: 6, gaze_amp_y: 0 },
    VerbWriting:      { blink_period_min_ms: 3500, blink_period_max_ms: 5499, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  0,     gaze_style: "ScanX",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 2200, gaze_amp_x: 2, gaze_amp_y: 0 },
    VerbExecuting:    { blink_period_min_ms: 4500, blink_period_max_ms: 6999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  5,     gaze_style: "ScanX",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 2500, gaze_amp_x: 1, gaze_amp_y: 0 },
    VerbStraining:    { blink_period_min_ms: 4500, blink_period_max_ms: 6999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  5,     gaze_style: "ScanX",      gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms: 2500, gaze_amp_x: 1, gaze_amp_y: 0 },
    VerbSleeping:     { blink_period_min_ms:    0, blink_period_max_ms:    0, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: 10,     gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    VerbWaking:    { blink_period_min_ms:    0, blink_period_max_ms:    0, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  0,     gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    VerbAttractingAttention: { blink_period_min_ms:    0, blink_period_max_ms:    0, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px:  0,     gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Sleepy:           { blink_period_min_ms: 5000, blink_period_max_ms: 7999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Distressed:       { blink_period_min_ms: 2000, blink_period_max_ms: 3999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Blissed:          { blink_period_min_ms: 3500, blink_period_max_ms: 5499, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Depressed:        { blink_period_min_ms: 2000, blink_period_max_ms: 3999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Shocked:          { blink_period_min_ms: 2000, blink_period_max_ms: 3999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Disappointed:     { blink_period_min_ms: 2000, blink_period_max_ms: 3999, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Cheeky:           { blink_period_min_ms: 2800, blink_period_max_ms: 4199, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Gleeful:          { blink_period_min_ms: 2200, blink_period_max_ms: 3799, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
    Frustrated:       { blink_period_min_ms: 1800, blink_period_max_ms: 3199, blink_close_ms: 80, blink_open_ms: 130, bob_amplitude_px: FOLLOW, gaze_style: "Off",        gaze_move_ms:   0, gaze_rand_span_x:  0, gaze_rand_span_y:  0, gaze_reroll_min_ms:    0, gaze_reroll_max_ms:     0, gaze_scan_period_ms:    0, gaze_amp_x: 0, gaze_amp_y: 0 },
  };

  // ---- Sim configs (kEmotionSim, kFrameAnim, kVerbSim) -------------------
  const emotionSim = {
    tau_ms_activation: 6000.0,
    tau_ms_valence: 90000.0,
    tau_ms_raw_follow: 50.0,
    snap_hysteresis_dist: 0.05,
    snap_hysteresis_hold_ms: 100,
    dist_sq_tie_eps: 1e-7,
    baseline_activation: 0.5,
  };
  const frameAnim = {
    mood_ring_tau_ms: 200.0,
    emotion_geometry_smooth_tau_ms: 250.0,
    tick_interval_ms: 33,
    tick_interval_stream_ms: 16,
    thinking_flip_dur_ms: 600,
    thinking_flip_min_ms: 3000,
    thinking_flip_max_ms: 6000,
    progress_fade_ms: 280,
    effects_fade_ms: 100,
    breath_period_ms: 4000,
    breath_eye_amp_px: 1.5,
    breath_mouth_scale: 0.5,
    emotion_bob_amp_follow_arm: 3,
    default_blink_close_ms: 80,
    default_blink_open_ms: 130,
    default_gaze_move_ms: 200,
    invalid_gaze_reroll_fallback_ms: 1000,
    // mirrors Face::kVerbTransitionDurMs
    verb_transition_ms: 500,
  };
  const verbSim = {
    strain_delay_ms: 5000,
    default_overlay_duration_ms: 1000,
  };

  window.FaceConfigData = {
    PARAM_FIELDS,
    EXPRESSIONS, EMOTIONS, VERBS, OVERLAYS,
    expressionIsEmotion,
    namedEmotionToExpression,
    emotionPoints,
    baseTargets,
    overlayPresets,
    verbTimelines,
    armPresets,
    motion,
    idleAnim,
    BOB_AMP_FOLLOW_EMOTION_ARM,
    emotionSim,
    frameAnim,
    verbSim,
  };
})();
