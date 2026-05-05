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
    Neutral:           [  2, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  3, 15,
                          0, 15,   +2, 0,  +2, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    Happy:             [  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0, 15,
                          0, 13,   +3, 0,  +3, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    Excited:           [  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   0,  0, 15,
                          0, 15,   +8, 0,  +8, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    Joyful:            [ -4, 24,   -7, 0,  -7, 0, 4,  0, 0, 0,   0,  0,  0,
                          0, 18,    0, 0, +14, 0, 4,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    Sad:               [  2, 30,   +6, 0,  +6, 0, 3,  0, 0, 0,   0,  3, 15,
                          4, 13,   -8, 0,  -8, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    VerbThinking:      [  0, 30,  -30, 0, +30, 0, 3,  0, 0, 0,   7, -9, 15,
                          0, 11,   +3, 0,  +3, 0, 3,  0, 0, 0,
                        -10, 0,    0, 0, 0 ],
    VerbReading:       [  0, 28,  -26, 0, +26, 0, 3,  0, 0, 0,   0,  8, 12,
                          0,  9,   +3, 0,  +3, 0, 3,  0, 0, 0,
                          0, 12,   0, 0, 0 ],
    VerbWriting:       [  0, 30,  -26, 0, +26, 0, 3,  0, 0, 0,   0, -8, 15,
                          0, 15,    0, 0, +14, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    VerbExecuting:     [  0, 30,  -16, 0, +16, 0, 3,  0, 0, 0,   0, -4, 10,
                          0,  9,   +2, 0,  +2, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    VerbStraining:     [  0, 30,  -22, 0, +22, 0, 3,  0, 0, 0,   0, -3, 10,
                          0, 18,    0, 0,   0, 0, 3,  4, 100, 360,
                          0, 0,    0, 0, 0 ],
    VerbSleeping:      [  8, 26,   -2, 0,  +2, 0, 3,  0, 0, 0,   0,  0,  0,
                          0,  9,    0, 0,   0, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    OverlayWaking:     [ -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                          0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
    OverlayAttention:  [ -2, 34,  -34, 0, +34, 0, 3,  0, 0, 0,   0,  0, 18,
                          0,  7,   -9, 0,  +9, 0, 3,  0, 0, 0,
                          0, 0,    0, 0, 0 ],
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

  // Mirrors MotionBehaviors::periodMsFor for the bobbing expressions.
  function motorPeriodMsFor(name) {
    switch (name) {
      case "VerbThinking": return 2000;
      case "VerbWriting": return 840;
      case "VerbExecuting": return 1000;
      case "VerbStraining": return 750;
      case "Joyful": return 900;
      case "Excited": return 1000;
      case "VerbSleeping": return 8000;
      default: return 0;
    }
  }
  function bodyBobFor(name, now) {
    const period = motorPeriodMsFor(name);
    if (period === 0) return 0;
    let amp = 0;
    switch (name) {
      case "VerbSleeping": amp = 10; break;
      case "VerbExecuting":
      case "VerbStraining":
      case "Excited": amp = 5; break;
      case "Joyful": amp = 7; break;
      default: return 0;
    }
    const t = (now % period) / period;
    return -Math.sin(t * 2 * Math.PI) * amp;
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
        const o = sStaticOverride;
        window.RobotFaceV3.renderScene(sprite, o.params, o.blinkAmt, o.gdx, o.gdy, t);
        sCurrentParams = o.params;
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

      if (expr !== "Joyful" && expr !== "VerbSleeping") {
        const b = breathPhase(t) * 1.5;
        p.eye_dy = Math.round(p.eye_dy + b);
        p.mouth_dy = Math.round(p.mouth_dy + b / 2);
      }

      p.face_y = Math.round(p.face_y + bodyBobFor(expr, t));

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
  };
})();
