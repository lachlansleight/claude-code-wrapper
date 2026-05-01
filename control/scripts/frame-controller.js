// Port of robot_v2/FrameController.cpp — drives the face animation. Reads
// the current Personality state and tweens between per-state FaceParams
// targets each tick.
//
// Public API:
//   FrameController.start(spriteCanvas)  — begin requestAnimationFrame loop
//   FrameController.stop()
//   FrameController.params()             — current interpolated params (debug)

(function () {
  const STATE_ORDER = [
    "idle",
    "thinking",
    "reading",
    "writing",
    "executing",
    "executing_long",
    "finished",
    "excited",
    "ready",
    "waking",
    "sleep",
    "blocked",
    "wants_attention",
  ];

  // Mirrors kBaseTargets in FrameController.cpp. Keep order locked to STATE_ORDER.
  // Field order: { eye_dy, eye_rx, eye_ry, eye_stroke, eye_curve,
  //                pupil_dx, pupil_dy, pupil_r, mouth_dy, mouth_w,
  //                mouth_curve, mouth_open_h, mouth_thick, face_rot, face_y,
  //                ring_r, ring_g, ring_b }
  const BASE_TARGETS = {
    idle:            [  2, 30, 26, 3,   0,  0,  3, 15,  0, 30,  -2,  0, 3,   0,  0, 0, 0, 0 ],
    thinking:        [  0, 30, 30, 3,   0,  7, -9, 15,  0, 22,  -3,  0, 3, -10,  0, 0, 0, 0 ],
    reading:         [  0, 28, 26, 3,   0,  0,  8, 12,  0, 18,  -3,  0, 3,   0, 12, 0, 0, 0 ],
    writing:         [  0, 30, 26, 3,   0,  0, -8, 15,  0, 30,  -1, 14, 3,   0,  0, 0, 0, 0 ],
    executing:       [  0, 30, 16, 3,   0,  0, -4, 10,  0, 18,  -2,  0, 3,   0,  0, 0, 0, 0 ],
    executing_long:  [  0, 30, 22, 3,   0,  0, -3, 10,  0, 18,   0,  0, 3,   0,  0, 0, 0, 0 ],
    finished:        [ -4, 24,  4, 4,   7,  0,  0,  0,  0, 36,  -1, 14, 4,   0,  0, 0, 0, 0 ],
    excited:         [  0, 30, 30, 3,   0,  0,  0, 15,  0, 30,  -8,  0, 3,   0,  0, 0, 0, 0 ],
    ready:           [  0, 30, 30, 3,   0,  0,  0, 15,  0, 26,  -3,  0, 3,   0,  0, 0, 0, 0 ],
    waking:          [ -2, 34, 34, 3,   0,  0,  0, 18,  0, 14,   0,  9, 3,   0,  0, 0, 0, 0 ],
    sleep:           [  8, 26,  2, 3,   0,  0,  0,  0,  0, 18,   0,  0, 3,   0,  0, 0, 0, 0 ],
    blocked:         [  2, 30, 22, 3,  -6,  0,  3, 15,  4, 26,   8,  0, 3,   0,  0, 0, 0, 0 ],
    wants_attention: [ -2, 34, 34, 3,   0,  0,  0, 18,  0, 14,   0,  9, 3,   0,  0, 0, 0, 0 ],
  };

  const PARAM_FIELDS = [
    "eye_dy", "eye_rx", "eye_ry", "eye_stroke", "eye_curve",
    "pupil_dx", "pupil_dy", "pupil_r", "mouth_dy", "mouth_w",
    "mouth_curve", "mouth_open_h", "mouth_thick", "face_rot", "face_y",
    "ring_r", "ring_g", "ring_b",
  ];

  function arrToParams(a) {
    const o = {};
    PARAM_FIELDS.forEach((k, i) => (o[k] = a[i]));
    return o;
  }

  // Mood ring colour per state — not yet wired into the face draw (mood
  // ring renderer pending) but kept here so the table stays in lockstep
  // with FrameController.cpp.
  function moodRgbForState(s) {
    const settings = window.RobotSettings;
    switch (s) {
      case "thinking": return settings.rgb("thinking");
      case "reading": return settings.rgb("reading");
      case "writing": return settings.rgb("writing");
      case "executing": return settings.rgb("executing");
      case "executing_long": return settings.rgb("executing_long");
      case "finished": return settings.rgb("finished");
      case "excited": return settings.rgb("excited");
      case "blocked": return settings.rgb("blocked");
      case "wants_attention": return settings.rgb("wants_at");
      default: return settings.rgb("background");
    }
  }

  function targetForState(s) {
    const base = BASE_TARGETS[s] || BASE_TARGETS.idle;
    const p = arrToParams(base);
    const [r, g, b] = moodRgbForState(s);
    p.ring_r = r;
    p.ring_g = g;
    p.ring_b = b;
    return p;
  }

  function lerpi(a, b, t) {
    return Math.round(a + (b - a) * t);
  }

  function lerpParams(a, b, t) {
    const r = {};
    for (const k of PARAM_FIELDS) r[k] = lerpi(a[k], b[k], t);
    return r;
  }

  // Period table for body-bob — mirrors MotionBehaviors::periodMsFor for the
  // states that have a stable rhythmic motor period.
  function motorPeriodMsFor(s) {
    switch (s) {
      case "thinking": return 2000;
      case "writing": return 840;
      case "executing": return 1000;
      case "executing_long": return 750;
      case "finished": return 900;
      case "excited": return 1000;
      case "sleep": return 8000;
      default: return 0;
    }
  }

  function bodyBobFor(s, now) {
    const period = motorPeriodMsFor(s);
    if (period === 0) return 0;
    let amp = 0;
    switch (s) {
      case "sleep": amp = 10; break;
      case "executing":
      case "executing_long":
      case "excited": amp = 5; break;
      case "finished": amp = 7; break;
      default: return 0;
    }
    const t = (now % period) / period;
    return -Math.sin(t * 2 * Math.PI) * amp;
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
  let sFrom = arrToParams(BASE_TARGETS.sleep);
  let sTo = sFrom;
  let sTweenStartMs = 0;
  let sLastState = null;

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
  let sLastSettingsVersion = 0;
  let sCurrentParams = sFrom;

  // Static mode — when true, the state machine + tween + modulators are
  // bypassed and the override below is rendered as-is each tick. Useful
  // for tuning FaceParams by hand.
  let sStaticMode = false;
  let sStaticOverride = {
    params: arrToParams(BASE_TARGETS.idle),
    blinkAmt: 0,
    gdx: 0,
    gdy: 0,
    state: "idle",
  };

  // ---- Helpers -----------------------------------------------------------
  function now() {
    return performance.now() - sStartedAtMs;
  }
  function randRange(lo, hi) {
    return lo + Math.random() * (hi - lo);
  }
  function randInt(lo, hi) {
    return Math.floor(randRange(lo, hi));
  }

  function blinkPeriodMsFor(s) {
    switch (s) {
      case "idle": return randInt(4000, 6500);
      case "thinking": return randInt(2000, 3500);
      case "reading": return randInt(4000, 6000);
      case "writing": return randInt(3500, 5500);
      case "executing":
      case "executing_long": return randInt(4500, 7000);
      case "excited": return randInt(2500, 4000);
      case "ready": return randInt(3000, 4500);
      default: return 0;
    }
  }

  function scheduleNextBlink(s, from) {
    const p = blinkPeriodMsFor(s);
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
    return sThinkFromSign + (sThinkToSign - sThinkFromSign) * window.RobotFace.smoothstep01(u);
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

  function gazeFor(s, t) {
    let gdx = 0, gdy = 0;
    switch (s) {
      case "idle": {
        if (sIdleGlanceStartMs !== 0) {
          const u = window.RobotFace.smoothstep01((t - sIdleGlanceStartMs) / kIdleGlanceTweenMs);
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
      case "thinking": {
        const u = (t % 900) / 900;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        gdy = Math.round(Math.cos(u * 2 * Math.PI) * 2);
        break;
      }
      case "reading": {
        const u = (t % 1300) / 1300;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 6);
        break;
      }
      case "writing": {
        const u = (t % 2200) / 2200;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        break;
      }
      case "executing":
      case "executing_long": {
        const u = (t % 2500) / 2500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 1);
        break;
      }
      case "excited": {
        const u = (t % 3500) / 3500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 3);
        gdy = Math.round(Math.cos(u * 2 * Math.PI) * 2);
        break;
      }
      case "ready": {
        const u = (t % 5500) / 5500;
        gdx = Math.round(Math.sin(u * 2 * Math.PI) * 2);
        break;
      }
      default: break;
    }
    return [gdx, gdy];
  }

  function breathPhase(t) {
    const u = (t % 4000) / 4000;
    return Math.sin(u * 2 * Math.PI);
  }

  function onStateChange(newState, t) {
    const u = (t - sTweenStartMs) / kTweenMs;
    const cur = lerpParams(sFrom, sTo, window.RobotFace.smoothstep01(u));
    if (sLastState === "thinking" && newState !== "thinking") {
      const sign = currentThinkSign(t);
      cur.face_rot = Math.round(cur.face_rot * sign);
      cur.pupil_dx = Math.round(cur.pupil_dx * sign);
    }
    sFrom = cur;
    sTo = targetForState(newState);
    sTweenStartMs = t;
    sLastState = newState;
    sBlinkActive = false;
    scheduleNextBlink(newState, t);
    if (newState === "thinking") resetThinkTilt(t);
    if (newState === "idle") {
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

  function tick() {
    const t = now();
    const s = window.Personality.current();

    const settingsVersion = window.RobotSettings.version();
    if (settingsVersion !== sLastSettingsVersion) {
      sLastSettingsVersion = settingsVersion;
      sTo = targetForState(s);
    }

    if (sStaticMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        const o = sStaticOverride;
        window.RobotFace.renderScene(sprite, o.params, o.blinkAmt, o.gdx, o.gdy, o.state);
        sCurrentParams = o.params;
        if (outputCanvas) {
          const octx = outputCanvas.getContext("2d");
          octx.imageSmoothingEnabled = false;
          octx.clearRect(0, 0, outputCanvas.width, outputCanvas.height);
          octx.drawImage(
            sprite.canvas, 0, 0, sprite.width, sprite.height,
            0, 0, outputCanvas.width, outputCanvas.height,
          );
        }
      }
      rafHandle = requestAnimationFrame(tick);
      return;
    }

    if (t - lastTickMs >= kTickIntervalMs) {
      lastTickMs = t;

      if (s !== sLastState) onStateChange(s, t);

      const u = (t - sTweenStartMs) / kTweenMs;
      const te = window.RobotFace.smoothstep01(u);
      let p = lerpParams(sFrom, sTo, te);

      if (s !== "finished" && s !== "sleep") {
        const b = breathPhase(t) * 1.5;
        p.eye_dy = Math.round(p.eye_dy + b);
        p.mouth_dy = Math.round(p.mouth_dy + b / 2);
      }

      p.face_y = Math.round(p.face_y + bodyBobFor(s, t));

      if (s === "thinking") {
        maybeFlipThinkTilt(t);
        const sign = currentThinkSign(t);
        p.face_rot = Math.round(p.face_rot * sign);
        p.pupil_dx = Math.round(p.pupil_dx * sign);
      }

      if (!sBlinkActive) {
        if (sNextBlinkMs === 0) {
          scheduleNextBlink(s, t);
        } else if (t >= sNextBlinkMs) {
          sBlinkActive = true;
          sBlinkStartMs = t;
          sNextBlinkMs = 0;
        }
      }
      const blinkAmt = currentBlinkAmount(t);
      if (!sBlinkActive && sNextBlinkMs === 0) scheduleNextBlink(s, t);

      const [gdx, gdy] = gazeFor(s, t);

      window.RobotFace.renderScene(sprite, p, blinkAmt, gdx, gdy, s);
      sCurrentParams = p;

      if (outputCanvas) {
        const octx = outputCanvas.getContext("2d");
        octx.imageSmoothingEnabled = false;
        octx.clearRect(0, 0, outputCanvas.width, outputCanvas.height);
        octx.drawImage(
          sprite.canvas,
          0, 0, sprite.width, sprite.height,
          0, 0, outputCanvas.width, outputCanvas.height,
        );
      }
    }

    rafHandle = requestAnimationFrame(tick);
  }

  function start(canvas) {
    if (rafHandle) return;
    sStartedAtMs = performance.now();
    sprite = new TFT.Sprite(240, 240);
    outputCanvas = canvas;
    sFrom = targetForState(window.Personality.current());
    sTo = sFrom;
    sLastState = window.Personality.current();
    sTweenStartMs = 0;
    sLastSettingsVersion = window.RobotSettings.version();
    rafHandle = requestAnimationFrame(tick);
  }

  function stop() {
    if (rafHandle) cancelAnimationFrame(rafHandle);
    rafHandle = null;
  }

  window.FrameController = {
    start,
    stop,
    params() { return sCurrentParams; },
    paramFields() { return PARAM_FIELDS.slice(); },
    baseTargetForState(s) { return arrToParams(BASE_TARGETS[s] || BASE_TARGETS.idle); },
    setStaticMode(on) {
      sStaticMode = !!on;
      if (sStaticMode) {
        // When entering static, seed the override from the live frame so
        // the user starts from what's currently on screen.
        sStaticOverride.params = { ...sCurrentParams };
      } else {
        // Re-arm tween from current frame back to the active state target.
        sFrom = { ...sCurrentParams };
        sTo = targetForState(window.Personality.current());
        sTweenStartMs = now();
        sLastState = window.Personality.current();
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
      if (typeof partial.state === "string") sStaticOverride.state = partial.state;
    },
    staticOverride() {
      return {
        params: { ...sStaticOverride.params },
        blinkAmt: sStaticOverride.blinkAmt,
        gdx: sStaticOverride.gdx,
        gdy: sStaticOverride.gdy,
        state: sStaticOverride.state,
      };
    },
  };
})();
