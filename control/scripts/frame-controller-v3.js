// Port of robot_v3/src/face/FrameController.cpp — drives the v3 face animation.
// Tweens between per-Expression FaceParams targets, with blink, body-bob,
// gaze and the thinking-tilt flip. Static mode bypasses everything for
// hand-tuning via sliders.
//
// Public API:
//   FrameControllerV3.start(canvas)
//   FrameControllerV3.stop()
//   FrameControllerV3.requestExpression(name)
//   FrameControllerV3.currentExpression()
//   FrameControllerV3.expressions()
//   FrameControllerV3.params()              -> live FaceParams (for debug box)
//   FrameControllerV3.paramFields()         -> ordered list of FaceParams keys
//   FrameControllerV3.baseTargetForExpression(name) -> FaceParams snapshot
//   FrameControllerV3.setStaticMode(on)
//   FrameControllerV3.setStaticOverride({ params?, blinkAmt?, gdx?, gdy?, expression? })
//   FrameControllerV3.staticOverride()
//   FrameControllerV3.armOffsetDeg()   -> live servo offset (deg) for rim viz

(function () {
  const EXPRESSIONS = [
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
  ];

  const PARAM_FIELDS = [
    "eye_dy", "eye_rx",
    "eye_top_apex", "eye_top_corner", "eye_bot_apex", "eye_bot_corner", "eye_thick",
    "eye_wave_amp", "eye_wave_freq", "eye_wave_speed",
    "pupil_dx", "pupil_dy", "pupil_r",
    "mouth_dy", "mouth_rx",
    "mouth_top_apex", "mouth_top_corner", "mouth_bot_apex", "mouth_bot_corner", "mouth_thick",
    "mouth_wave_amp", "mouth_wave_freq", "mouth_wave_speed",
    "face_rot", "face_y",
    "ring_r", "ring_g", "ring_b",
  ];

  // Mirrors robot_v3 FrameController.cpp::kBaseTargets. Field order matches
  // FaceParams declaration; keep in lockstep when tuning.
  const BASE_TARGETS = {
      Neutral:             [  2, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  3, 15,
                              0, 15,   +2, 0,  +2, 0, 3,  0, 0, 0,
                              0, 0,    0, 0, 0 ],
Happy:             [  0, 30,  -16, 0, +30, 0, 3,  0, 0, 0,   0,  5,  16,
      0,  +24,   3, 0,  3, 0, 3,  0, 0, 0,
      0, 5,    0, 0, 0 ],
Excited:             [  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0,  17,
      0,  +27,   4, -2,  8, -2, 3,  0, 0, 0,
      0, 0,    40, 255, 80 ],
Joyful:             [ -5, 20,  -15, 0, -6, 0, 4,  0, 0, 0,   0,  0,  14,
  -11,  +37,   3, 0,  24, 0, 4,  0, 0, 0,
  0, -14,    255, 228, 38 ],
Sad:             [  4, 28,  -12, 0, +17, 0, 3,  0, 0, 0,   0,  3,  11,
      4,  +20,   -13, -7,  -11, -8, 3,  0, 0, 0,
      0, 6,    0, 0, 0 ],
VerbThinking:             [  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   7, -9, 15,
      0, 11,   +3, 0,  +3, 0, 3,  0, 0, 0,
    -10, 0,    36, 56, 120 ],
VerbReading:             [  0, 28,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  8, 12,
      0,  9,   +3, 0,  +3, 0, 3,  0, 0, 0,
      0, 12,   78, 146, 210 ],
VerbWriting:             [  0, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0, -8, 15,
      0, 15,    0, 0, +14, 0, 3,  0, 0, 0,
      0, 0,    104, 118, 228 ],
VerbExecuting:             [  0, 30,  -16, 0, +16, 0, 3,  0, 0, 0,   0, -4, 10,
      0,  9,   +2, 0,  +2, 0, 3,  0, 0, 0,
      0, 0,    156, 64, 216 ],
VerbStraining:             [  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
      0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
      0, 0,    210, 75, 220 ],
VerbSleeping:             [  8, 26,   -2, 0,  +2, 0, 3,  0, 0, 0,   0,  0,  15,
      0,  9,    0, 0,   0, 0, 3,  0, 0, 0,
      0, 0,    0, 0, 0 ],
OverlayWaking:             [ -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
      0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
      0, 0,    128, 128, 128 ],
OverlayAttention:             [ -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
      0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
      0, 0,    255, 20, 40 ],
Sleepy:             [  0, 28,  0, 10, +34, 10, 3,  0, 0, 0,   0,  0,  15,
      0,  +13,   0, 0,  3, 0, 3,  0, 17, 90,
      0, 9,    0, 0, 0 ],
Distressed:             [  2, 30,  -26, 0, +33, 0, 3,  0, 0, 0,   0,  7,  10,
      4,  +24,   -19, -7,  -7, 0, 3,  0, 0, 0,
      0, -15,    255, 48, 24 ],
Blissed:             [  1, 20,   +3, 0, +1, 0, 3,  0, 0, 0,   0,  0,  15,
      1,  +26,   3, 0,  13, 0, 3,  0, 0, 0,
      0, 5,    0, 0, 0 ],
Depressed:             [  0, 30,   +16, 10, +34, 11, 3,  0, 0, 0,   0,  20,  6,
      0,  +13,   0, +6,  3, 4, 3,  0, 17, 90,
      0, 9,    0, 0, 0 ],
Shocked:             [ 0, 30,   -34, 0,   39, 0, 3,   1, 85, 720,   0, 3, 9,
     20, 17,  -17, 0,   8, 0, 1,    2, 49, 720,   
     0, 0,     255, 255, 255 ],
Disappointed:             [  2, 30,   +6, 0, +6, 0, 3,  0, 0, 0,   0,  3,  15,
      4,  +13,   -8, 0,  -8, 0, 3,  0, 0, 0,
      0, 0,    229, 54, 95 ],
Cheeky:            [  1, 30,  -31, 0, +8, 0, 3,  0, 0, 0,   0,  3,  15,
      -25,  +15,   11, 0,  8, 0, 3,  0, 0, 0,
      0, -3,    0, 0, 0 ],
Gleeful:           [  1, 27,  -30, 0, -2, 0, 3,  0, 0, 0,   0,  -7,  10,
      -25,  +27,   0, -2,  20, -2, 3,  0, 0, 0,
      0, 5,    39, 248, 78 ],      
  };

  function arrToParams(a) {
    const o = {};
    PARAM_FIELDS.forEach((k, i) => (o[k] = a[i]));
    return o;
  }

  function targetForExpression(name) {
    const a = BASE_TARGETS[name] || BASE_TARGETS.Neutral;
    return arrToParams(a);
  }

  function lerpi(a, b, t) { return Math.round(a + (b - a) * t); }
  function lerpParams(a, b, t) {
    const r = {};
    for (const k of PARAM_FIELDS) r[k] = lerpi(a[k], b[k], t);
    return r;
  }

  // Mirrors MotionBehaviors::periodMsFor for verb/overlay expressions.
  function motorPeriodMsFor(name) {
    switch (name) {
      case "VerbThinking": return 2000;
      case "VerbWriting": return 840;
      case "VerbExecuting": return 1000;
      case "VerbStraining": return 750;
      case "Joyful": return 900;
      case "Excited": return 1000;
      case "VerbSleeping": return 8000;
      case "Sleepy": return 5000;
      case "Distressed": return 900;
      case "Cheeky": return 880;
      case "Gleeful": return 900;
      default: return 0;
    }
  }

  const EMOTION_NAMES = new Set([
    "Neutral", "Happy", "Excited", "Joyful", "Sad", "Sleepy", "Distressed",
    "Blissed", "Depressed", "Shocked", "Disappointed", "Cheeky", "Gleeful",
  ]);
  function isEmotionExpression(name) {
    return EMOTION_NAMES.has(name);
  }

  function vaForExpression(name) {
    const tab = window.EmotionTriangulation;
    if (!tab || !Array.isArray(tab.anchors)) return { v: 0, a: 0.5 };
    const an = tab.anchors.find((x) => x.emotion === name);
    return an ? { v: an.v, a: an.a } : { v: 0, a: 0.5 };
  }

  /** Like firmware MotionBehaviors::periodMsForContext. */
  function motorPeriodMsForContext(expr, blendMode, blendV, blendA) {
    const EB = window.EmotionBlendV3;
    if (blendMode && EB && EB.ready()) {
      const m = EB.blendedEmotionArmMotion(blendV, blendA);
      if (m) {
        return Math.round(Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000);
      }
    }
    if (isEmotionExpression(expr) && EB && EB.ready()) {
      const va = vaForExpression(expr);
      const m = EB.blendedEmotionArmMotion(va.v, va.a);
      if (m) {
        return Math.round(Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000);
      }
    }
    return motorPeriodMsFor(expr);
  }

  function bodyBobFor(expr, now, blendMode, blendV, blendA) {
    const period = motorPeriodMsForContext(expr, blendMode, blendV, blendA);
    if (period === 0) {
      sBodyBobPhaseLastMs = now;
      return 0;
    }

    let amp = 0;
    let integrate = false;
    if (blendMode && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      const m = window.EmotionBlendV3.blendedEmotionArmMotion(blendV, blendA);
      integrate = true;
      if (m && m.min_offset_deg !== m.max_offset_deg) amp = 3;
    } else if (isEmotionExpression(expr) && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      const va = vaForExpression(expr);
      const m = window.EmotionBlendV3.blendedEmotionArmMotion(va.v, va.a);
      integrate = true;
      if (m && m.min_offset_deg !== m.max_offset_deg) amp = 3;
    } else {
      switch (expr) {
        case "VerbSleeping": amp = 10; break;
        case "VerbExecuting":
        case "VerbStraining":
        case "Excited": amp = 5; break;
        case "Joyful": amp = 7; break;
        case "Sleepy": amp = 4; break;
        case "Distressed": amp = 6; break;
        default: amp = 0; break;
      }
      integrate = amp !== 0;
    }

    const twoPi = 2 * Math.PI;
    if (integrate) {
      const dtMs = sBodyBobPhaseLastMs === 0 ? 0 : now - sBodyBobPhaseLastMs;
      sBodyBobPhaseLastMs = now;
      sBodyBobPhaseRad += (twoPi / period) * dtMs;
      sBodyBobPhaseRad %= twoPi;
      if (sBodyBobPhaseRad < 0) sBodyBobPhaseRad += twoPi;
    } else {
      sBodyBobPhaseLastMs = now;
    }

    if (amp === 0) return 0;
    return -Math.sin(sBodyBobPhaseRad) * amp;
  }

  function breathPhase(t) {
    const u = (t % 4000) / 4000;
    return Math.sin(u * 2 * Math.PI);
  }

  // ---- Tunables ----------------------------------------------------------
  const kTweenMs = 250;
  const kTickIntervalMs = 33;
  const kBlinkCloseMs = 80;
  const kBlinkOpenMs = 130;
  const kThinkingFlipDurMs = 600;
  const kThinkingFlipMinMs = 3000;
  const kThinkingFlipMaxMs = 6000;
  const kIdleGlanceTweenMs = 200;

  // ---- Running state -----------------------------------------------------
  let sCurrentExpr = "Neutral";
  let sFrom = targetForExpression("Neutral");
  let sTo = sFrom;
  let sTweenStartMs = 0;
  let sLastExpr = null;

  let sNextBlinkMs = 0;
  let sBlinkStartMs = 0;
  let sBlinkActive = false;

  let sThinkFromSign = 1;
  let sThinkToSign = 1;
  let sThinkFlipStartMs = 0;
  let sNextThinkFlipMs = 0;

  let sIdleGlanceDx = 0;
  let sIdleGlanceDy = 0;
  let sIdleGlanceFromDx = 0;
  let sIdleGlanceFromDy = 0;
  let sIdleGlanceStartMs = 0;
  let sNextIdleGlanceMs = 0;

  let sStartedAtMs = 0;
  let sCurrentParams = sFrom;
  let sLastSettingsVersion = 0;

  let sStaticMode = false;
  let sStaticOverride = {
    params: arrToParams(BASE_TARGETS.Neutral),
    blinkAmt: 0,
    gdx: 0,
    gdy: 0,
    expression: "Neutral",
  };

  // Blend mode: feeds (v, a) every tick through EmotionBlendV3 and
  // renders the resulting params (no animation, no static sliders).
  let sBlendMode = false;
  let sBlendV = 0.0;
  let sBlendA = 0.0;
  let sBlendLastParams = arrToParams(BASE_TARGETS.Neutral);
  let sBlendLastTri = null;  // { indices, weights } for canvas viz.

  /** Integrated body-bob phase (rad); avoids jitter when waggle period changes every frame. */
  let sBodyBobPhaseRad = 0;
  let sBodyBobPhaseLastMs = 0;

  /** Servo offset (deg) for rim hands; emotion uses firmware-style sine + dwell. */
  let sCurrentArmDeg = 0;
  let sArmLogicLastMs = 0;
  let sArmEmotionInOsc = true;
  let sArmEmotionOsc01 = 0;
  let sArmEmotionDwellS = 0;
  let sPrevArmDriverEmotion = false;

  function resetEmotionArmPhase() {
    sArmEmotionInOsc = true;
    sArmEmotionOsc01 = 0;
    sArmEmotionDwellS = 0;
    sArmLogicLastMs = 0;
  }

  function tickEmotionArm(dt, arm) {
    let lo = arm.min_offset_deg;
    let hi = arm.max_offset_deg;
    if (lo > hi) {
      const tmp = lo;
      lo = hi;
      hi = tmp;
    }
    if (lo === hi) return lo;
    const period = Math.max(0.05, arm.waggle_period_s);
    if (sArmEmotionInOsc) {
      sArmEmotionOsc01 += dt / period;
      const oscDraw = sArmEmotionOsc01 >= 1 ? 1 : sArmEmotionOsc01;
      if (sArmEmotionOsc01 >= 1) {
        sArmEmotionOsc01 = 0;
        if (arm.waggle_interval_s < 0.02) {
          /* immediate next arch */
        } else {
          sArmEmotionInOsc = false;
          sArmEmotionDwellS = arm.waggle_interval_s;
        }
      }
      const u = Math.sin(Math.PI * oscDraw);
      return lo + (hi - lo) * u;
    }
    sArmEmotionDwellS -= dt;
    if (sArmEmotionDwellS <= 0) {
      sArmEmotionInOsc = true;
      sArmEmotionOsc01 = 0;
    }
    return lo;
  }

  function verbArmOffset(name, t) {
    switch (name) {
      case "VerbReading": return -8;
      case "OverlayWaking": return 18;
      case "VerbThinking": {
        const T = 2000;
        const u = (t % T) / T;
        return -15 + 5 * Math.sin(u * 2 * Math.PI);
      }
      case "VerbWriting":
        return 5 + 4 * Math.sin((t % 840) / 840 * 2 * Math.PI);
      case "VerbExecuting":
        return -5 + 5 * Math.sin((t % 1000) / 1000 * 2 * Math.PI);
      case "VerbStraining":
        return 5 * Math.sin((t % 750) / 750 * 2 * Math.PI);
      case "VerbSleeping":
        return -20 + 5 * Math.sin((t % 8000) / 8000 * 2 * Math.PI);
      case "OverlayAttention":
        return 15 * Math.sin((t % 900) / 900 * 2 * Math.PI);
      default:
        return 0;
    }
  }

  function updateArmOffset(t, expr) {
    const EB = window.EmotionBlendV3;
    const dt = sArmLogicLastMs === 0 ? 0 : Math.min(0.5, (t - sArmLogicLastMs) / 1000);
    sArmLogicLastMs = t;

    const armDriverEmotion = sBlendMode || isEmotionExpression(expr);
    if (armDriverEmotion && !sPrevArmDriverEmotion) {
      resetEmotionArmPhase();
    }
    sPrevArmDriverEmotion = armDriverEmotion;

    if (sBlendMode && EB && EB.ready()) {
      const arm = EB.blendedEmotionArmMotion(sBlendV, sBlendA);
      sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
      return;
    }

    if (isEmotionExpression(expr) && EB && EB.ready()) {
      const va = vaForExpression(expr);
      const arm = EB.blendedEmotionArmMotion(va.v, va.a);
      sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
      return;
    }

    resetEmotionArmPhase();
    sCurrentArmDeg = verbArmOffset(expr, t);
  }

  /**
   * Rim markers orbit the centre: offset 0° → 3 and 9 o'clock; e.g. -30° →
   * ~4 and ~8 (both shift by the same servo angle). Canvas polar: 0 = +x
   * = 3 o'clock. No extra local spin — only position follows offsetDeg.
   */
  function drawArmOverlay(octx, w, h, offsetDeg) {
    const cx = w * 0.5;
    const cy = h * 0.5;
    const R = Math.min(w, h) * 0.5 - 14;
    const offsetRad = (offsetDeg * Math.PI) / 180;
    const thetaRight = -offsetRad;
    // Y-axis mirror of the right rim point (like meshed gears: same vertical motion,
    // opposite horizontal). Not π − offset — that was 180° rigid rotation of both.
    const thetaLeft = Math.PI - thetaRight;

    function palmAtOrbital(thetaRad) {
      const rx = cx + R * Math.cos(thetaRad);
      const ry = cy + R * Math.sin(thetaRad);
      octx.save();
      octx.translate(rx, ry);
      octx.fillStyle = "rgba(228,232,242,0.92)";
      octx.strokeStyle = "rgba(24,28,40,0.92)";
      octx.lineWidth = 1.25;
      octx.beginPath();
      if (typeof octx.roundRect === "function") {
        octx.roundRect(-6, -18, 12, 22, 5);
      } else {
        octx.moveTo(-5, -18);
        octx.lineTo(5, -18);
        octx.quadraticCurveTo(7, -18, 7, -14);
        octx.lineTo(7, 2);
        octx.quadraticCurveTo(7, 6, 2, 6);
        octx.lineTo(-2, 6);
        octx.quadraticCurveTo(-7, 6, -7, 2);
        octx.lineTo(-7, -14);
        octx.quadraticCurveTo(-7, -18, -5, -18);
        octx.closePath();
      }
      octx.fill();
      octx.stroke();
      octx.restore();
    }

    palmAtOrbital(thetaRight);
    palmAtOrbital(thetaLeft);
  }

  // ---- Helpers -----------------------------------------------------------
  function now() { return performance.now() - sStartedAtMs; }
  function randRange(lo, hi) { return lo + Math.random() * (hi - lo); }
  function randInt(lo, hi) { return Math.floor(randRange(lo, hi)); }

  function blinkPeriodMsFor(name) {
    switch (name) {
      case "Neutral": return randInt(4000, 6500);
      case "VerbThinking": return randInt(2000, 3500);
      case "VerbReading": return randInt(4000, 6000);
      case "VerbWriting": return randInt(3500, 5500);
      case "VerbExecuting":
      case "VerbStraining": return randInt(4500, 7000);
      case "Excited": return randInt(2500, 4000);
      case "Happy": return randInt(3000, 4500);
      default: return 0;
    }
  }
  function scheduleNextBlink(name, from) {
    const p = blinkPeriodMsFor(name);
    sNextBlinkMs = p === 0 ? 0 : from + p;
  }
  function currentBlinkAmount(t) {
    if (!sBlinkActive) return 0;
    const d = t - sBlinkStartMs;
    if (d < kBlinkCloseMs) return d / kBlinkCloseMs;
    const d2 = d - kBlinkCloseMs;
    if (d2 < kBlinkOpenMs) return 1 - d2 / kBlinkOpenMs;
    sBlinkActive = false;
    return 0;
  }

  function currentThinkSign(t) {
    if (sThinkFlipStartMs === 0) return sThinkToSign;
    const u = (t - sThinkFlipStartMs) / kThinkingFlipDurMs;
    return sThinkFromSign + (sThinkToSign - sThinkFromSign) * window.RobotFaceV3.smoothstep01(u);
  }
  function resetThinkTilt(t) {
    sThinkFromSign = 1;
    sThinkToSign = 1;
    sThinkFlipStartMs = 0;
    sNextThinkFlipMs = t + randInt(kThinkingFlipMinMs, kThinkingFlipMaxMs + 1);
  }
  function maybeFlipThinkTilt(t) {
    if (sNextThinkFlipMs === 0 || t < sNextThinkFlipMs) return;
    sThinkFromSign = currentThinkSign(t);
    sThinkToSign = -sThinkFromSign;
    sThinkFlipStartMs = t;
    sNextThinkFlipMs = t + kThinkingFlipDurMs +
      randInt(kThinkingFlipMinMs, kThinkingFlipMaxMs + 1);
  }

  function gazeFor(name, t) {
    let gdx = 0, gdy = 0;
    switch (name) {
      case "Neutral": {
        if (sIdleGlanceStartMs !== 0) {
          const u = window.RobotFaceV3.smoothstep01((t - sIdleGlanceStartMs) / kIdleGlanceTweenMs);
          gdx = lerpi(sIdleGlanceFromDx, sIdleGlanceDx, u);
          gdy = lerpi(sIdleGlanceFromDy, sIdleGlanceDy, u);
        } else {
          gdx = sIdleGlanceDx; gdy = sIdleGlanceDy;
        }
        if (sNextIdleGlanceMs === 0 || t >= sNextIdleGlanceMs) {
          sIdleGlanceFromDx = gdx;
          sIdleGlanceFromDy = gdy;
          sIdleGlanceDx = randInt(-15, 16);
          sIdleGlanceDy = randInt(-10, 11);
          sIdleGlanceStartMs = t;
          sNextIdleGlanceMs = t + randInt(1000, 10001);
        }
        break;
      }
      case "VerbThinking": {
        const u = (t % 900) / 900;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        gdy = Math.round(Math.cos(u * 2 * Math.PI) * 2);
        break;
      }
      case "VerbReading": {
        const u = (t % 1300) / 1300;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 6);
        break;
      }
      case "VerbWriting": {
        const u = (t % 2200) / 2200;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        break;
      }
      case "VerbExecuting":
      case "VerbStraining": {
        const u = (t % 2500) / 2500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 1);
        break;
      }
      case "Excited": {
        const u = (t % 3500) / 3500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 3);
        gdy = Math.round(Math.cos(u * 2 * Math.PI) * 2);
        break;
      }
      case "Happy": {
        const u = (t % 5500) / 5500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        break;
      }
      default: break;
    }
    return [gdx, gdy];
  }

  function onExpressionChange(newExpr, t) {
    const u = (t - sTweenStartMs) / kTweenMs;
    const cur = lerpParams(sFrom, sTo, window.RobotFaceV3.smoothstep01(u));
    if (sLastExpr === "VerbThinking" && newExpr !== "VerbThinking") {
      const sign = currentThinkSign(t);
      cur.face_rot = Math.round(cur.face_rot * sign);
      cur.pupil_dx = Math.round(cur.pupil_dx * sign);
    }
    sFrom = cur;
    sTo = targetForExpression(newExpr);
    sTweenStartMs = t;
    sLastExpr = newExpr;
    sBlinkActive = false;
    scheduleNextBlink(newExpr, t);
    if (newExpr === "VerbThinking") resetThinkTilt(t);
    if (newExpr === "Neutral") {
      sIdleGlanceFromDx = sIdleGlanceDx;
      sIdleGlanceFromDy = sIdleGlanceDy;
      sIdleGlanceStartMs = t;
      sNextIdleGlanceMs = t;
    } else {
      sIdleGlanceDx = 0; sIdleGlanceDy = 0;
      sIdleGlanceFromDx = 0; sIdleGlanceFromDy = 0;
      sIdleGlanceStartMs = 0; sNextIdleGlanceMs = 0;
    }
  }

  // ---- Main loop ---------------------------------------------------------
  let rafHandle = null;
  let sprite = null;
  let outputCanvas = null;
  let lastTickMs = 0;
  const listeners = [];

  function notifyExpression() {
    for (const fn of listeners) fn(sCurrentExpr);
  }

  function pushSpriteToCanvas() {
    if (!outputCanvas) return;
    const octx = outputCanvas.getContext("2d");
    octx.imageSmoothingEnabled = false;
    octx.clearRect(0, 0, outputCanvas.width, outputCanvas.height);
    octx.drawImage(
      sprite.canvas, 0, 0, sprite.width, sprite.height,
      0, 0, outputCanvas.width, outputCanvas.height,
    );
    if (!sStaticMode) {
      drawArmOverlay(octx, outputCanvas.width, outputCanvas.height, sCurrentArmDeg);
    }
  }

  function tick() {
    const t = now();
    const expr = sCurrentExpr;

    const settingsVersion = window.RobotSettings.version();
    if (settingsVersion !== sLastSettingsVersion) {
      sLastSettingsVersion = settingsVersion;
      sTo = targetForExpression(expr);
    }

    if (sStaticMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        sCurrentArmDeg = 0;
        const o = sStaticOverride;
        window.RobotFaceV3.renderScene(sprite, o.params, o.blinkAmt, o.gdx, o.gdy, t);
        sCurrentParams = o.params;
        pushSpriteToCanvas();
      }
      rafHandle = requestAnimationFrame(tick);
      return;
    }

    if (sBlendMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        const blender = window.EmotionBlendV3;
        let p = sBlendLastParams;
        if (blender && blender.ready()) {
          const blended = blender.blendedFaceParams(sBlendV, sBlendA);
          if (blended) p = blended;
          sBlendLastTri = blender.findTriangle(sBlendV, sBlendA);
        }
        sBlendLastParams = p;

        updateArmOffset(t, sCurrentExpr);

        if (sCurrentExpr !== "Joyful" && sCurrentExpr !== "Gleeful" &&
            sCurrentExpr !== "VerbSleeping") {
          const b = breathPhase(t) * 1.5;
          p = { ...p, eye_dy: Math.round(p.eye_dy + b), mouth_dy: Math.round(p.mouth_dy + b / 2) };
        }
        p = {
          ...p,
          face_y: Math.round(p.face_y + bodyBobFor(sCurrentExpr, t, true, sBlendV, sBlendA)),
        };

        window.RobotFaceV3.renderScene(sprite, p, 0, 0, 0, t);
        sCurrentParams = p;
        pushSpriteToCanvas();
      }
      rafHandle = requestAnimationFrame(tick);
      return;
    }

    if (t - lastTickMs >= kTickIntervalMs) {
      lastTickMs = t;

      if (expr !== sLastExpr) onExpressionChange(expr, t);

      const u = (t - sTweenStartMs) / kTweenMs;
      const te = window.RobotFaceV3.smoothstep01(u);
      let p = lerpParams(sFrom, sTo, te);

      if (expr !== "Joyful" && expr !== "Gleeful" && expr !== "VerbSleeping") {
        const b = breathPhase(t) * 1.5;
        p.eye_dy = Math.round(p.eye_dy + b);
        p.mouth_dy = Math.round(p.mouth_dy + b / 2);
      }

      updateArmOffset(t, expr);

      p.face_y = Math.round(p.face_y + bodyBobFor(expr, t, false, 0, 0));

      if (expr === "VerbThinking") {
        maybeFlipThinkTilt(t);
        const sign = currentThinkSign(t);
        p.face_rot = Math.round(p.face_rot * sign);
        p.pupil_dx = Math.round(p.pupil_dx * sign);
      }

      if (!sBlinkActive) {
        if (sNextBlinkMs === 0) scheduleNextBlink(expr, t);
        else if (t >= sNextBlinkMs) {
          sBlinkActive = true;
          sBlinkStartMs = t;
          sNextBlinkMs = 0;
        }
      }
      const blinkAmt = currentBlinkAmount(t);
      if (!sBlinkActive && sNextBlinkMs === 0) scheduleNextBlink(expr, t);

      const [gdx, gdy] = gazeFor(expr, t);

      window.RobotFaceV3.renderScene(sprite, p, blinkAmt, gdx, gdy, t);
      sCurrentParams = p;
      pushSpriteToCanvas();
    }

    rafHandle = requestAnimationFrame(tick);
  }

  function start(canvas) {
    if (rafHandle) return;
    sStartedAtMs = performance.now();
    sprite = new TFT.Sprite(240, 240);
    outputCanvas = canvas;
    sFrom = targetForExpression(sCurrentExpr);
    sTo = sFrom;
    sLastExpr = sCurrentExpr;
    sTweenStartMs = 0;
    sLastSettingsVersion = window.RobotSettings.version();
    resetEmotionArmPhase();
    sPrevArmDriverEmotion = false;
    sCurrentArmDeg = 0;
    rafHandle = requestAnimationFrame(tick);
  }

  function stop() {
    if (rafHandle) cancelAnimationFrame(rafHandle);
    rafHandle = null;
  }

  function requestExpression(name) {
    if (!BASE_TARGETS[name]) return;
    if (name === sCurrentExpr) return;
    sCurrentExpr = name;
    notifyExpression();
  }

  window.FrameControllerV3 = {
    start,
    stop,
    requestExpression,
    currentExpression() { return sCurrentExpr; },
    expressions() { return EXPRESSIONS.slice(); },
    onExpressionChange(fn) { listeners.push(fn); },
    params() { return sCurrentParams; },
    paramFields() { return PARAM_FIELDS.slice(); },
    baseTargetForExpression(name) { return arrToParams(BASE_TARGETS[name] || BASE_TARGETS.Neutral); },
    setStaticMode(on) {
      sStaticMode = !!on;
      if (sStaticMode) {
        sBlendMode = false;
        sStaticOverride.params = { ...sCurrentParams };
      } else {
        sFrom = { ...sCurrentParams };
        sTo = targetForExpression(sCurrentExpr);
        sTweenStartMs = now();
        sLastExpr = sCurrentExpr;
        sBlinkActive = false;
        sNextBlinkMs = 0;
      }
    },
    isStatic() { return sStaticMode; },

    setBlendMode(on) {
      sBlendMode = !!on;
      if (sBlendMode) {
        sStaticMode = false;
        // If already on an emotion expression, keep arm phase across blend toggle.
        sPrevArmDriverEmotion = isEmotionExpression(sCurrentExpr);
      } else {
        sPrevArmDriverEmotion = isEmotionExpression(sCurrentExpr);
        sFrom = { ...sCurrentParams };
        sTo = targetForExpression(sCurrentExpr);
        sTweenStartMs = now();
        sLastExpr = sCurrentExpr;
        sBlinkActive = false;
        sNextBlinkMs = 0;
      }
    },
    isBlend() { return sBlendMode; },
    setBlendVA(v, a) {
      if (typeof v === "number") sBlendV = Math.max(-1, Math.min(1, v));
      if (typeof a === "number") sBlendA = Math.max(0, Math.min(1, a));
    },
    blendVA() { return { v: sBlendV, a: sBlendA }; },
    lastBlendTriangle() { return sBlendLastTri; },
    setStaticOverride(partial) {
      if (!partial) return;
      if (partial.params) sStaticOverride.params = { ...sStaticOverride.params, ...partial.params };
      if (typeof partial.blinkAmt === "number") sStaticOverride.blinkAmt = partial.blinkAmt;
      if (typeof partial.gdx === "number") sStaticOverride.gdx = partial.gdx;
      if (typeof partial.gdy === "number") sStaticOverride.gdy = partial.gdy;
      if (typeof partial.expression === "string") sStaticOverride.expression = partial.expression;
    },
    staticOverride() {
      return {
        params: { ...sStaticOverride.params },
        blinkAmt: sStaticOverride.blinkAmt,
        gdx: sStaticOverride.gdx,
        gdy: sStaticOverride.gdy,
        expression: sStaticOverride.expression,
      };
    },

    /** Centre-relative servo offset (deg) used for rim hands + debug. */
    armOffsetDeg() {
      return sCurrentArmDeg;
    },
  };
})();
