// Port of robot_v3/src/face/FrameController.cpp — drives the v3 face animation.
// Tables and tunables come from face-config-data.js (`window.FaceConfigData`).
//
// Emotion + verb composition: emotion presets carry full FaceParams (with
// implicit strength 100). Verbs are sparse-override timelines that combine
// on top of the underlying emotion via combineEmotionVerbField (mirrors
// robot_v3/src/face/SceneTypes.cpp).
//
// State model:
//   sLatchedEmotion — current emotion (one of 14 NamedEmotion). Tweened.
//   sActiveVerb     — current verb (or null). Sparse override on top.
//   sActiveOverlay  — short transient overlay (currently rendered straight
//                     from BASE_TARGETS preset; firmware uses EffectsRenderer
//                     rim, not geometry — simulator approximation).
//   sCurrentExpr    — derived "effective" expression for grids/labels.
//
// Public API:
//   FrameControllerV3.start(canvas)
//   FrameControllerV3.stop()
//   FrameControllerV3.requestExpression(name)         // dispatcher
//   FrameControllerV3.requestEmotion(name)
//   FrameControllerV3.requestVerb(name | null)
//   FrameControllerV3.requestOverlay(name | null)
//   FrameControllerV3.latchedEmotion()
//   FrameControllerV3.activeVerb()
//   FrameControllerV3.activeOverlay()
//   FrameControllerV3.currentExpression()             // derived
//   FrameControllerV3.expressions()
//   FrameControllerV3.emotions() / verbs() / overlays()
//   FrameControllerV3.params()                        -> live FaceParams
//   FrameControllerV3.paramFields()
//   FrameControllerV3.baseTargetForExpression(name)   // synthesizes verbs
//   FrameControllerV3.setStaticMode(on)
//   FrameControllerV3.setStaticOverride({ params?, blinkAmt?, gdx?, gdy?, expression? })
//   FrameControllerV3.staticOverride()
//   FrameControllerV3.setBlendMode(on) / isBlend() / setBlendVA / blendVA / lastBlendTriangle
//   FrameControllerV3.armOffsetDeg()

(function () {
  const D = window.FaceConfigData;
  if (!D) {
    console.error("frame-controller-v3.js: load face-config-data.js before this script");
    return;
  }

  // Order MUST match Face::FieldIndex in robot_v3/src/face/FacePrimitives.h.
  const PARAM_FIELDS = D.PARAM_FIELDS;
  const FIELD_COUNT = PARAM_FIELDS.length;
  const FIELD_INDEX = {};
  PARAM_FIELDS.forEach((k, i) => (FIELD_INDEX[k] = i));

  const EMOTIONS = D.EMOTIONS;
  const VERBS = D.VERBS;
  const OVERLAYS = D.OVERLAYS;
  const EXPRESSIONS = D.EXPRESSIONS;

  const EMOTION_SET = new Set(EMOTIONS);
  const VERB_SET = new Set(VERBS);
  const OVERLAY_SET = new Set(OVERLAYS);

  function isEmotion(name) { return EMOTION_SET.has(name); }
  function isVerb(name) { return VERB_SET.has(name); }
  function isOverlay(name) { return OVERLAY_SET.has(name); }

  // Emotion presets + simulator overlay scalars (see face-config-data.js).
  const BASE_TARGETS = { ...D.baseTargets, ...D.overlayPresets };

  // Verb sparse overrides: numeric field indices for the hot path.
  const VERB_TIMELINES = {};
  for (const v of VERBS) {
    const list = D.verbTimelines[v];
    VERB_TIMELINES[v] = (list || []).map((o) => ({
      field: FIELD_INDEX[o.field],
      value: o.value,
      strength: o.strength,
    }));
  }

  const frameAnim = D.frameAnim;
  const motionTable = D.motion;
  const idleAnimTable = D.idleAnim;

  // Expand a verb's sparse list into parallel hasField/value/strength arrays.
  // Returns an empty sample for null / unknown / non-verb names.
  function sampleVerbTimeline(verb) {
    const has = new Array(FIELD_COUNT).fill(false);
    const val = new Array(FIELD_COUNT).fill(0);
    const str = new Array(FIELD_COUNT).fill(0);
    const list = verb ? VERB_TIMELINES[verb] : null;
    if (!list) return { has, val, str };
    for (const o of list) {
      has[o.field] = true;
      val[o.field] = o.value;
      str[o.field] = o.strength;
    }
    return { has, val, str };
  }

  // ---- Verb cross-fade ---------------------------------------------------
  // Mirrors robot_v3/src/face/VerbTimeline.cpp. On change of the active verb,
  // snapshot the in-flight effective sample and cross-fade over verb_transition_ms
  // toward the new target's sparse-override sample. Per-field rules:
  //   * both override → lerp value AND strength by t
  //   * only `from` overrides → keep value, scale strength by (1 - t)
  //   * only `to` overrides → keep value, scale strength by t
  const kVerbTransitionMs = frameAnim.verb_transition_ms;

  function emptySample() {
    return {
      has: new Array(FIELD_COUNT).fill(false),
      val: new Array(FIELD_COUNT).fill(0),
      str: new Array(FIELD_COUNT).fill(0),
    };
  }

  let sFromVerbSample = emptySample();
  let sToVerb = null;             // verb name or null = "no verb / empty target"
  let sVerbTransitionStartMs = 0;
  let sFromVerbInitialised = false;

  // Last-rendered values of the per-frame "modification pass" outputs that
  // depend on the active expression (bob amplitude, gaze offset, arm angle).
  // Captured into sFrom* on verb change and lerped toward live values during
  // the 500ms window so they don't snap.
  let sLastBobAmp = 0;
  let sLastGdx = 0;
  let sLastGdy = 0;
  // sCurrentArmDeg already serves as the last-rendered arm.

  let sFromBobAmp = 0;
  let sFromGdx = 0;
  let sFromGdy = 0;
  let sFromArmDeg = 0;

  function transitionT(now) {
    if (!sFromVerbInitialised) return 1;
    return Math.min(1, Math.max(0, (now - sVerbTransitionStartMs) / kVerbTransitionMs));
  }

  function evaluateVerbAt(now) {
    const toSample = sampleVerbTimeline(sToVerb);
    const t = transitionT(now);
    if (!sFromVerbInitialised || t >= 1) return toSample;

    const out = emptySample();
    const oneMinus = 1 - t;
    for (let i = 0; i < FIELD_COUNT; ++i) {
      const fromHas = sFromVerbSample.has[i];
      const nextHas = toSample.has[i];
      if (fromHas && nextHas) {
        out.has[i] = true;
        out.val[i] = Math.round(sFromVerbSample.val[i] * oneMinus + toSample.val[i] * t);
        out.str[i] = Math.round(sFromVerbSample.str[i] * oneMinus + toSample.str[i] * t);
      } else if (fromHas) {
        const s = Math.round(sFromVerbSample.str[i] * oneMinus);
        out.has[i] = s > 0;
        out.val[i] = sFromVerbSample.val[i];
        out.str[i] = Math.max(0, s);
      } else if (nextHas) {
        const s = Math.round(toSample.str[i] * t);
        out.has[i] = s > 0;
        out.val[i] = toSample.val[i];
        out.str[i] = Math.max(0, s);
      }
    }
    return out;
  }

  // Capture all transition snapshots (verb sample + mod outputs) and retarget.
  // Tick calls this when sActiveVerb changes; sampleEffectiveVerb is then a
  // pure read of the new state.
  function noteVerbChange(now, newVerb) {
    sFromVerbSample = evaluateVerbAt(now);
    sFromVerbInitialised = true;
    sToVerb = newVerb;
    sVerbTransitionStartMs = now;

    sFromBobAmp = sLastBobAmp;
    sFromGdx = sLastGdx;
    sFromGdy = sLastGdy;
    sFromArmDeg = sCurrentArmDeg;
  }

  function sampleEffectiveVerb(now) {
    return evaluateVerbAt(now);
  }

  function resetVerbTransition() {
    sFromVerbSample = emptySample();
    sToVerb = null;
    sVerbTransitionStartMs = 0;
    sFromVerbInitialised = false;
    sFromBobAmp = 0;
    sFromGdx = 0;
    sFromGdy = 0;
    sFromArmDeg = 0;
    sLastBobAmp = 0;
    sLastGdx = 0;
    sLastGdy = 0;
  }

  // ---- Combine: emotion ⊕ verb (mirrors SceneTypes.cpp) ------------------
  // Lerp from emotion value to verb value where verb strength is the lerp t,
  // and emotion strength shapes the curve power.
  //   es = 50  → linear
  //   es < 50  → ease-out:  factor = 1 - (1 - t)^p
  //   es > 50  → ease-in:   factor = t^p
  // Power scales linearly from 1 (at es=50) to kMaxPower (at es=0 or es=100).
  const COMBINE_MAX_POWER = 5.0;

  function combineEmotionVerbValue(eValue, eStrength, vHas, vValue, vStrength) {
    if (!vHas) return { value: eValue, strength: eStrength };
    const se = eStrength | 0;
    const sv = vStrength | 0;
    if (se === 0 && sv === 0) return { value: 0, strength: 0 };
    if (sv === 0) return { value: eValue, strength: se };
    if (se === 0) return { value: vValue, strength: sv };

    const t = sv / 100.0;
    let factor;
    if (se === 50) {
      factor = t;
    } else if (se < 50) {
      const power = 1.0 + (50.0 - se) / 50.0 * (COMBINE_MAX_POWER - 1.0);
      factor = 1.0 - Math.pow(1.0 - t, power);
    } else {
      const power = 1.0 + (se - 50.0) / 50.0 * (COMBINE_MAX_POWER - 1.0);
      factor = Math.pow(t, power);
    }

    return {
      value: Math.round(eValue + (vValue - eValue) * factor),
      strength: Math.min(100, Math.max(se, sv)),
    };
  }

  // Apply verb sparse overrides on top of an emotion FaceParams (plain values
  // with implicit strength 100). Returns plain-value FaceParams.
  function combineEmotionVerbFace(emotionParams, verbSample) {
    const out = {};
    for (let i = 0; i < FIELD_COUNT; ++i) {
      const k = PARAM_FIELDS[i];
      const r = combineEmotionVerbValue(
        emotionParams[k] | 0, 100,
        verbSample.has[i], verbSample.val[i], verbSample.str[i],
      );
      out[k] = r.value;
    }
    return out;
  }

  // ---- Helpers -----------------------------------------------------------
  function arrToParams(a) {
    const o = {};
    PARAM_FIELDS.forEach((k, i) => (o[k] = a[i] | 0));
    return o;
  }

  function emotionPreset(name) {
    const a = BASE_TARGETS[name] || BASE_TARGETS.Neutral;
    return arrToParams(a);
  }

  function overlayPreset(name) {
    const a = BASE_TARGETS[name];
    return a ? arrToParams(a) : emotionPreset("Neutral");
  }

  // Synthesised "what would this verb look like on a Neutral underlay?"
  // Used by the Static-mode preset picker so verbs are previewable.
  function verbPresetOnNeutral(verb) {
    const base = emotionPreset("Neutral");
    return combineEmotionVerbFace(base, sampleVerbTimeline(verb));
  }

  function targetForExpression(name) {
    if (isVerb(name)) return verbPresetOnNeutral(name);
    if (isOverlay(name)) return overlayPreset(name);
    return emotionPreset(name);
  }

  function lerpi(a, b, t) { return Math.round(a + (b - a) * t); }
  function lerpParams(a, b, t) {
    const r = {};
    for (const k of PARAM_FIELDS) r[k] = lerpi(a[k], b[k], t);
    return r;
  }

  // LEGACY: pre-v3 period-from-motion-table. Firmware bob uses arm position.
  function motorPeriodMsFor(name) {
    const m = motionTable[name];
    return m ? (m.period_ms | 0) : 0;
  }

  function vaForEmotion(name) {
    const tab = window.EmotionTriangulation;
    if (!tab || !Array.isArray(tab.anchors)) return { v: 0, a: 0.5 };
    const an = tab.anchors.find((x) => x.emotion === name);
    return an ? { v: an.v, a: an.a } : { v: 0, a: 0.5 };
  }

  /** LEGACY — firmware stubbed periodMsForContext; bob follows arm angle. */
  function motorPeriodMsForContext(expr, blendMode, blendV, blendA) {
    const EB = window.EmotionBlendV3;
    if (blendMode && EB && EB.ready()) {
      const m = EB.blendedEmotionArmMotion(blendV, blendA);
      if (m) {
        return Math.round(Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000);
      }
    }
    if (isEmotion(expr) && EB && EB.ready()) {
      const va = vaForEmotion(expr);
      const m = EB.blendedEmotionArmMotion(va.v, va.a);
      if (m) {
        return Math.round(Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000);
      }
    }
    return motorPeriodMsFor(expr);
  }

  // Pure: bob amplitude (px) for the given expression. Snapshotted into
  // sFromBobAmp at verb change so the cross-fade can ramp it.
  function bodyBobAmpFor(expr, blendMode, blendV, blendA) {
    if (blendMode && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      const m = window.EmotionBlendV3.blendedEmotionArmMotion(blendV, blendA);
      return (m && m.min_offset_deg !== m.max_offset_deg)
        ? frameAnim.emotion_bob_amp_follow_arm
        : 0;
    }
    if (isEmotion(expr) && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      const va = vaForEmotion(expr);
      const m = window.EmotionBlendV3.blendedEmotionArmMotion(va.v, va.a);
      return (m && m.min_offset_deg !== m.max_offset_deg)
        ? frameAnim.emotion_bob_amp_follow_arm
        : 0;
    }
    switch (expr) {
      case "VerbSleeping": return 10;
      case "VerbExecuting":
      case "VerbStraining":
      case "Excited": return 5;
      case "Joyful": return 7;
      case "Sleepy": return 4;
      case "Distressed": return 6;
      default: return 0;
    }
  }

  function bodyBobFor(expr, now, blendMode, blendV, blendA) {
    const period = motorPeriodMsForContext(expr, blendMode, blendV, blendA);
    if (period === 0) {
      sBodyBobPhaseLastMs = now;
      return 0;
    }

    const liveAmp = bodyBobAmpFor(expr, blendMode, blendV, blendA);
    const tt = transitionT(now);
    const amp = tt >= 1 ? liveAmp : sFromBobAmp + (liveAmp - sFromBobAmp) * tt;
    sLastBobAmp = amp;

    let integrate = false;
    if (blendMode && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      integrate = true;
    } else if (isEmotion(expr) && window.EmotionBlendV3 && window.EmotionBlendV3.ready()) {
      integrate = true;
    } else {
      // Verb / overlay branch — integrate whenever there's a real bob amp on
      // either side of the transition (so the phase keeps moving while we
      // fade in/out).
      integrate = liveAmp !== 0 || sFromBobAmp !== 0;
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
    const bp = frameAnim.breath_period_ms;
    const u = (t % bp) / bp;
    return Math.sin(u * 2 * Math.PI);
  }

  // ---- Tunables (from face-config-data frameAnim) ------------------------
  const kTweenMs = frameAnim.emotion_geometry_smooth_tau_ms;
  const kTickIntervalMs = frameAnim.tick_interval_ms;
  const kBlinkCloseMs = frameAnim.default_blink_close_ms;
  const kBlinkOpenMs = frameAnim.default_blink_open_ms;
  const kThinkingFlipDurMs = frameAnim.thinking_flip_dur_ms;
  const kThinkingFlipMinMs = frameAnim.thinking_flip_min_ms;
  const kThinkingFlipMaxMs = frameAnim.thinking_flip_max_ms;
  const kIdleGlanceTweenMs = frameAnim.default_gaze_move_ms;

  // ---- Running state -----------------------------------------------------
  let sLatchedEmotion = "Neutral";
  let sActiveVerb = null;       // null or one of VERBS
  let sActiveOverlay = null;    // null or one of OVERLAYS
  let sCurrentExpr = "Neutral"; // derived: overlay > verb > emotion

  // Tween state operates on the underlying emotion preset only. Verb sparse
  // overrides combine on top each frame; overlays bypass tween entirely.
  let sFromEmotion = emotionPreset("Neutral");
  let sToEmotion = sFromEmotion;
  let sTweenStartMs = 0;
  let sLastEmotionTweened = "Neutral";
  let sLastExprForChangeEdge = "Neutral";

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
  let sCurrentParams = sFromEmotion;
  let sLastSettingsVersion = 0;

  let sStaticMode = false;
  let sStaticOverride = {
    params: emotionPreset("Neutral"),
    blinkAmt: 0,
    gdx: 0,
    gdy: 0,
    expression: "Neutral",
  };

  let sBlendMode = false;
  let sBlendV = 0.0;
  let sBlendA = 0.0;
  let sBlendLastParams = emotionPreset("Neutral");
  let sBlendLastTri = null;

  /** Integrated body-bob phase (rad); avoids jitter when waggle period changes every frame. */
  let sBodyBobPhaseRad = 0;
  let sBodyBobPhaseLastMs = 0;

  // Integrated phases for periodic outputs whose period/speed can vary
  // continuously per frame (EmotionBlendV3 smoothly interpolates FaceParams
  // and idle anim as V/A drifts). Computing phase from `now % period` or
  // `speed * now` would re-derive an absolute angle from a moving denominator,
  // producing high-frequency jitter — accumulate `phase += dPhase * dt` instead.
  let sEyeWavePhaseRad = 0;
  let sMouthWavePhaseRad = 0;
  let sWavePhaseLastMs = 0;
  let sGazePhaseRad = 0;
  let sGazePhaseLastMs = 0;

  // Period (ms) for Orbit / ScanX-style gaze — from face-config-data idleAnim.
  function gazePeriodMsFor(name) {
    const ia = idleAnimTable[name];
    return ia ? (ia.gaze_scan_period_ms | 0) : 0;
  }

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
    const m = motionTable[name];
    if (!m) return 0;
    if (m.mode === "Static") return m.center;
    const T = Math.max(1, m.period_ms | 0);
    const u = (t % T) / T;
    return m.center + m.amplitude * Math.sin(u * 2 * Math.PI);
  }

  // Compute the "live" arm angle for the given expression at time t. Mutates
  // the emotion-arm sine integrator state when relevant. The cross-fade
  // toward this value happens in the call site so that verb→verb and
  // emotion→verb transitions glide instead of snapping.
  function computeLiveArmDeg(t, expr) {
    const EB = window.EmotionBlendV3;
    const dt = sArmLogicLastMs === 0 ? 0 : Math.min(0.5, (t - sArmLogicLastMs) / 1000);
    sArmLogicLastMs = t;

    const armDriverEmotion = sBlendMode || isEmotion(expr);
    if (armDriverEmotion && !sPrevArmDriverEmotion) {
      resetEmotionArmPhase();
    }
    sPrevArmDriverEmotion = armDriverEmotion;

    if (sBlendMode && EB && EB.ready()) {
      const arm = EB.blendedEmotionArmMotion(sBlendV, sBlendA);
      return arm ? tickEmotionArm(dt, arm) : 0;
    }

    if (isEmotion(expr) && EB && EB.ready()) {
      const va = vaForEmotion(expr);
      const arm = EB.blendedEmotionArmMotion(va.v, va.a);
      return arm ? tickEmotionArm(dt, arm) : 0;
    }

    resetEmotionArmPhase();
    return verbArmOffset(expr, t);
  }

  function updateArmOffset(t, expr) {
    const live = computeLiveArmDeg(t, expr);
    const tt = transitionT(t);
    sCurrentArmDeg = tt >= 1 ? live : sFromArmDeg + (live - sFromArmDeg) * tt;
  }

  function drawArmOverlay(octx, w, h, offsetDeg) {
    const cx = w * 0.5;
    const cy = h * 0.5;
    const R = Math.min(w, h) * 0.5 - 14;
    const offsetRad = (offsetDeg * Math.PI) / 180;
    const thetaRight = -offsetRad;
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

  function now() { return performance.now() - sStartedAtMs; }
  function randRange(lo, hi) { return lo + Math.random() * (hi - lo); }
  function randInt(lo, hi) { return Math.floor(randRange(lo, hi)); }

  function blinkPeriodMsFor(name) {
    const ia = idleAnimTable[name];
    if (!ia) return 0;
    if (ia.blink_period_min_ms === 0 && ia.blink_period_max_ms === 0) return 0;
    return randInt(ia.blink_period_min_ms, ia.blink_period_max_ms + 1);
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
    // Advance the shared gaze phase regardless of which expression is active.
    // Period is read live so a future change to gazePeriodMsFor can vary it
    // without re-deriving an absolute angle.
    const per = gazePeriodMsFor(name);
    if (per !== 0) {
      const dtMs = sGazePhaseLastMs === 0 ? 0 : t - sGazePhaseLastMs;
      sGazePhaseRad += (2 * Math.PI / per) * dtMs;
      sGazePhaseRad = sGazePhaseRad % (2 * Math.PI);
      if (sGazePhaseRad < 0) sGazePhaseRad += 2 * Math.PI;
    }
    sGazePhaseLastMs = t;

    let gdx = 0, gdy = 0;
    switch (name) {
      case "Neutral": {
        const iaN = idleAnimTable.Neutral;
        const spanX = iaN ? (iaN.gaze_rand_span_x | 0) : 15;
        const spanY = iaN ? (iaN.gaze_rand_span_y | 0) : 10;
        const rerollLo = iaN ? (iaN.gaze_reroll_min_ms | 0) : 1000;
        const rerollHi = iaN ? (iaN.gaze_reroll_max_ms | 0) : 10000;
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
          sIdleGlanceDx = randInt(-spanX, spanX + 1);
          sIdleGlanceDy = randInt(-spanY, spanY + 1);
          sIdleGlanceStartMs = t;
          sNextIdleGlanceMs = t + randInt(rerollLo, rerollHi + 1);
        }
        break;
      }
      case "VerbThinking": {
        const ia = idleAnimTable.VerbThinking;
        const ax = ia ? (ia.gaze_amp_x | 0) : 2;
        const ay = ia ? (ia.gaze_amp_y | 0) : 2;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        gdy = Math.round(Math.cos(sGazePhaseRad) * ay);
        break;
      }
      case "VerbReading": {
        const ia = idleAnimTable.VerbReading;
        const ax = ia ? (ia.gaze_amp_x | 0) : 6;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        break;
      }
      case "VerbWriting": {
        const ia = idleAnimTable.VerbWriting;
        const ax = ia ? (ia.gaze_amp_x | 0) : 2;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        break;
      }
      case "VerbExecuting":
      case "VerbStraining": {
        const ia = idleAnimTable.VerbExecuting;
        const ax = ia ? (ia.gaze_amp_x | 0) : 1;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        break;
      }
      case "Excited": {
        const ia = idleAnimTable.Excited;
        const ax = ia ? (ia.gaze_amp_x | 0) : 3;
        const ay = ia ? (ia.gaze_amp_y | 0) : 2;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        gdy = Math.round(Math.cos(sGazePhaseRad) * ay);
        break;
      }
      case "Happy": {
        const ia = idleAnimTable.Happy;
        const ax = ia ? (ia.gaze_amp_x | 0) : 2;
        gdx = Math.round(Math.sin(sGazePhaseRad) * ax);
        break;
      }
      default: break;
    }
    return [gdx, gdy];
  }

  function effectiveExpression() {
    if (sActiveOverlay) return sActiveOverlay;
    if (sActiveVerb) return sActiveVerb;
    return sLatchedEmotion;
  }

  // Begin a tween from current rendered emotion to the new latched emotion.
  function startEmotionTween(t) {
    sFromEmotion = lerpParams(
      sFromEmotion, sToEmotion,
      window.RobotFaceV3.smoothstep01((t - sTweenStartMs) / kTweenMs),
    );
    sToEmotion = emotionPreset(sLatchedEmotion);
    sTweenStartMs = t;
    sLastEmotionTweened = sLatchedEmotion;
  }

  function onExpressionChange(newExpr, t) {
    if (sLastExprForChangeEdge === "VerbThinking" && newExpr !== "VerbThinking") {
      const sign = currentThinkSign(t);
      // Snap so leaving Thinking doesn't pop. Mirrors firmware behaviour.
      sToEmotion.face_rot = Math.round(sToEmotion.face_rot * sign);
      sToEmotion.pupil_dx = Math.round(sToEmotion.pupil_dx * sign);
    }
    sLastExprForChangeEdge = newExpr;
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
  const exprListeners = [];
  const stateListeners = [];

  function notifyExpression() {
    sCurrentExpr = effectiveExpression();
    for (const fn of exprListeners) fn(sCurrentExpr);
    const snap = {
      emotion: sLatchedEmotion,
      verb: sActiveVerb,
      overlay: sActiveOverlay,
      effective: sCurrentExpr,
    };
    for (const fn of stateListeners) fn(snap);
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
    const expr = effectiveExpression();

    const settingsVersion = window.RobotSettings.version();
    if (settingsVersion !== sLastSettingsVersion) {
      sLastSettingsVersion = settingsVersion;
      sToEmotion = emotionPreset(sLatchedEmotion);
    }

    if (sStaticMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        sCurrentArmDeg = 0;
        const o = sStaticOverride;
        advanceWavePhases(t, o.params);
        window.RobotFaceV3.renderScene(
          sprite, o.params, o.blinkAmt, o.gdx, o.gdy,
          sEyeWavePhaseRad, sMouthWavePhaseRad,
        );
        sCurrentParams = o.params;
        pushSpriteToCanvas();
      }
      rafHandle = requestAnimationFrame(tick);
      return;
    }

    if (t - lastTickMs >= kTickIntervalMs) {
      lastTickMs = t;

      // Edge: latched emotion changed → start tween.
      if (sLatchedEmotion !== sLastEmotionTweened) {
        startEmotionTween(t);
      }
      // Edge: active verb changed → snapshot verb sample + mods, restart
      // 500ms cross-fade. Must run before sampleEffectiveVerb / bob / gaze /
      // arm so the snapshots capture last frame's rendered values.
      if (sActiveVerb !== sToVerb) noteVerbChange(t, sActiveVerb);
      // Edge: effective expression changed → blink/think/glance reset.
      if (expr !== sLastExprForChangeEdge) onExpressionChange(expr, t);

      // Underlying emotion params: blend mode → V/A; otherwise tween.
      let emotionParams;
      if (sBlendMode) {
        const blender = window.EmotionBlendV3;
        if (blender && blender.ready()) {
          const blended = blender.blendedFaceParams(sBlendV, sBlendA);
          if (blended) {
            sBlendLastParams = blended;
            sBlendLastTri = blender.findTriangle(sBlendV, sBlendA);
          }
        }
        emotionParams = { ...sBlendLastParams };
      } else {
        const u = (t - sTweenStartMs) / kTweenMs;
        emotionParams = lerpParams(
          sFromEmotion, sToEmotion,
          window.RobotFaceV3.smoothstep01(u),
        );
      }

      // Composition: overlay > verb-on-emotion > emotion.
      // Sample every frame (even when sActiveVerb is null) so that a
      // ramp-out still runs after the verb is cleared.
      let p;
      if (sActiveOverlay) {
        p = overlayPreset(sActiveOverlay);
      } else {
        const verbSample = sampleEffectiveVerb(t);
        p = combineEmotionVerbFace(emotionParams, verbSample);
      }

      // Breath modulation. Skipped for high-energy / sleeping states.
      if (expr !== "Joyful" && expr !== "Gleeful" && expr !== "VerbSleeping") {
        const b = breathPhase(t) * frameAnim.breath_eye_amp_px;
        p.eye_dy = Math.round(p.eye_dy + b);
        p.mouth_dy = Math.round(p.mouth_dy + b * frameAnim.breath_mouth_scale);
      }

      updateArmOffset(t, expr);
      p.face_y = Math.round(p.face_y + bodyBobFor(expr, t, sBlendMode, sBlendV, sBlendA));

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

      const [liveGdx, liveGdy] = gazeFor(expr, t);
      const ttGaze = transitionT(t);
      const gdx = ttGaze >= 1
        ? liveGdx
        : Math.round(sFromGdx + (liveGdx - sFromGdx) * ttGaze);
      const gdy = ttGaze >= 1
        ? liveGdy
        : Math.round(sFromGdy + (liveGdy - sFromGdy) * ttGaze);
      sLastGdx = gdx;
      sLastGdy = gdy;

      advanceWavePhases(t, p);
      window.RobotFaceV3.renderScene(
        sprite, p, blinkAmt, gdx, gdy,
        sEyeWavePhaseRad, sMouthWavePhaseRad,
      );
      sCurrentParams = p;
      pushSpriteToCanvas();
    }

    rafHandle = requestAnimationFrame(tick);
  }

  // Advance integrated wave phases using the resolved (smoothed) wave_speed
  // values from the params we're about to render. Done after composition so
  // the speed is whatever EmotionBlend produced for this frame.
  function advanceWavePhases(now, params) {
    const dtMs = sWavePhaseLastMs === 0 ? 0 : now - sWavePhaseLastMs;
    sWavePhaseLastMs = now;
    const k = Math.PI / 180000;  // deg/sec * ms → rad
    sEyeWavePhaseRad += (params.eye_wave_speed | 0) * dtMs * k;
    sMouthWavePhaseRad += (params.mouth_wave_speed | 0) * dtMs * k;
    const twoPi = 2 * Math.PI;
    sEyeWavePhaseRad = sEyeWavePhaseRad % twoPi;
    if (sEyeWavePhaseRad < 0) sEyeWavePhaseRad += twoPi;
    sMouthWavePhaseRad = sMouthWavePhaseRad % twoPi;
    if (sMouthWavePhaseRad < 0) sMouthWavePhaseRad += twoPi;
  }

  function start(canvas) {
    if (rafHandle) return;
    sStartedAtMs = performance.now();
    sprite = new TFT.Sprite(240, 240);
    outputCanvas = canvas;
    sFromEmotion = emotionPreset(sLatchedEmotion);
    sToEmotion = sFromEmotion;
    sLastEmotionTweened = sLatchedEmotion;
    sLastExprForChangeEdge = effectiveExpression();
    sCurrentExpr = sLastExprForChangeEdge;
    sTweenStartMs = 0;
    sLastSettingsVersion = window.RobotSettings.version();
    resetVerbTransition();
    resetEmotionArmPhase();
    sEyeWavePhaseRad = 0;
    sMouthWavePhaseRad = 0;
    sWavePhaseLastMs = 0;
    sGazePhaseRad = 0;
    sGazePhaseLastMs = 0;
    sPrevArmDriverEmotion = false;
    sCurrentArmDeg = 0;
    rafHandle = requestAnimationFrame(tick);
  }

  function stop() {
    if (rafHandle) cancelAnimationFrame(rafHandle);
    rafHandle = null;
  }

  // ---- Setters -----------------------------------------------------------
  function requestEmotion(name) {
    if (!isEmotion(name)) return;
    if (sLatchedEmotion === name && !sActiveVerb && !sActiveOverlay) return;
    sLatchedEmotion = name;
    notifyExpression();
  }

  function requestVerb(name) {
    if (name !== null && !isVerb(name)) return;
    if (sActiveVerb === name) return;
    sActiveVerb = name;
    notifyExpression();
  }

  function requestOverlay(name) {
    if (name !== null && !isOverlay(name)) return;
    if (sActiveOverlay === name) return;
    sActiveOverlay = name;
    notifyExpression();
  }

  /**
   * Backwards-compatible single-button entry point. Dispatches by category.
   * Picking an emotion clears any active verb/overlay (matches "click an
   * emotion to see it alone"); picking a verb leaves the latched emotion in
   * place so you can preview verb-on-emotion combine.
   */
  function requestExpression(name) {
    if (isEmotion(name)) {
      sActiveVerb = null;
      sActiveOverlay = null;
      requestEmotion(name);
    } else if (isVerb(name)) {
      sActiveOverlay = null;
      requestVerb(name);
    } else if (isOverlay(name)) {
      requestOverlay(name);
    }
  }

  window.FrameControllerV3 = {
    start,
    stop,
    requestExpression,
    requestEmotion,
    requestVerb,
    requestOverlay,
    latchedEmotion() { return sLatchedEmotion; },
    activeVerb() { return sActiveVerb; },
    activeOverlay() { return sActiveOverlay; },
    currentExpression() { return sCurrentExpr; },
    expressions() { return EXPRESSIONS.slice(); },
    emotions() { return EMOTIONS.slice(); },
    verbs() { return VERBS.slice(); },
    overlays() { return OVERLAYS.slice(); },
    onExpressionChange(fn) { exprListeners.push(fn); },
    onStateChange(fn) { stateListeners.push(fn); },
    params() { return sCurrentParams; },
    paramFields() { return PARAM_FIELDS.slice(); },
    fieldIndex() { return { ...FIELD_INDEX }; },
    baseTargetForExpression(name) { return targetForExpression(name); },
    verbTimeline(name) {
      const list = VERB_TIMELINES[name];
      return list ? list.map((o) => ({ ...o })) : null;
    },
    combineEmotionVerbValue,
    combineEmotionVerbFace,
    sampleVerbTimeline,

    setStaticMode(on) {
      sStaticMode = !!on;
      if (sStaticMode) {
        sBlendMode = false;
        sStaticOverride.params = { ...sCurrentParams };
      } else {
        sFromEmotion = { ...sCurrentParams };
        sToEmotion = emotionPreset(sLatchedEmotion);
        sTweenStartMs = now();
        sLastEmotionTweened = sLatchedEmotion;
        sLastExprForChangeEdge = effectiveExpression();
        sBlinkActive = false;
        sNextBlinkMs = 0;
      }
    },
    isStatic() { return sStaticMode; },

    setBlendMode(on) {
      sBlendMode = !!on;
      if (sBlendMode) {
        sStaticMode = false;
        sPrevArmDriverEmotion = isEmotion(effectiveExpression());
      } else {
        sPrevArmDriverEmotion = isEmotion(effectiveExpression());
        sFromEmotion = { ...sCurrentParams };
        sToEmotion = emotionPreset(sLatchedEmotion);
        sTweenStartMs = now();
        sLastEmotionTweened = sLatchedEmotion;
        sLastExprForChangeEdge = effectiveExpression();
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

    armOffsetDeg() { return sCurrentArmDeg; },
  };
})();
