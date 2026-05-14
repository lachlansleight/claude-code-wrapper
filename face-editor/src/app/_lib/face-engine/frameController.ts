/**
 * Port of robot_v3/src/face/FrameController.cpp — v3 face animation driver.
 * Ported from control/scripts/frame-controller-v3.js
 */

import { createEmotionBlend } from "./emotionBlend";
import { EMOTION_TRIANGULATION } from "./emotionTriangulation";
import type { FaceParams } from "./faceParams";
import type { ParamField } from "./faceParams";
import { createFaceRenderer } from "./faceRenderer";
import {
  baseTargetForExpression as presetTargetForExpression,
  expressionsList,
  isEmotionExpression,
  paramFieldsList,
} from "./presets";
import { createRobotSettings } from "./robotSettings";
import { TFTSprite, tft } from "./tftSprite";
import type { BlendTriangle, EmotionArmMotion } from "./types";

function targetForExpression(name: string): FaceParams {
  return presetTargetForExpression(name);
}

function lerpi(a: number, b: number, t: number): number {
  return Math.round(a + (b - a) * t);
}

function lerpParams(a: FaceParams, b: FaceParams, t: number): FaceParams {
  const r = {} as FaceParams;
  for (const k of paramFieldsList()) {
    r[k] = lerpi(a[k], b[k], t);
  }
  return r;
}

function motorPeriodMsFor(name: string): number {
  switch (name) {
    case "VerbThinking":
      return 2000;
    case "VerbWriting":
      return 840;
    case "VerbExecuting":
      return 1000;
    case "VerbStraining":
      return 750;
    case "Joyful":
      return 900;
    case "Excited":
      return 1000;
    case "VerbSleeping":
      return 8000;
    case "Sleepy":
      return 5000;
    case "Distressed":
      return 900;
    case "Cheeky":
      return 880;
    case "Gleeful":
      return 900;
    case "Frustrated":
      return 820;
    default:
      return 0;
  }
}

export interface StaticOverrideState {
  params: FaceParams;
  blinkAmt: number;
  gdx: number;
  gdy: number;
  expression: string;
}

export interface FrameController {
  start(canvas: HTMLCanvasElement): void;
  stop(): void;
  requestExpression(name: string): void;
  currentExpression(): string;
  expressions(): string[];
  onExpressionChange(fn: (name: string) => void): void;
  params(): FaceParams;
  paramFields(): ParamField[];
  baseTargetForExpression(name: string): FaceParams;
  setStaticMode(on: boolean): void;
  isStatic(): boolean;
  setBlendMode(on: boolean): void;
  isBlend(): boolean;
  setBlendVA(v: number, a: number): void;
  blendVA(): { v: number; a: number };
  lastBlendTriangle(): BlendTriangle | null;
  setStaticOverride(partial: {
    params?: Partial<FaceParams>;
    blinkAmt?: number;
    gdx?: number;
    gdy?: number;
    expression?: string;
  }): void;
  staticOverride(): StaticOverrideState;
  armOffsetDeg(): number;
}

export function createFrameController(): FrameController {
  const settings = createRobotSettings();
  const face = createFaceRenderer({ settings, tft });
  const emotionBlend = createEmotionBlend({
    triangulation: EMOTION_TRIANGULATION,
    paramFields: paramFieldsList(),
    baseTargetForExpression: presetTargetForExpression,
  });

  function vaForExpression(name: string): { v: number; a: number } {
    const tab = EMOTION_TRIANGULATION;
    if (!tab || !Array.isArray(tab.anchors)) return { v: 0, a: 0.5 };
    const an = tab.anchors.find((x) => x.emotion === name);
    return an ? { v: an.v, a: an.a } : { v: 0, a: 0.5 };
  }

  function motorPeriodMsForContext(
    expr: string,
    blendMode: boolean,
    blendV: number,
    blendA: number,
  ): number {
    if (blendMode && emotionBlend.ready()) {
      const m = emotionBlend.blendedEmotionArmMotion(blendV, blendA);
      if (m) {
        return Math.round(
          Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000,
        );
      }
    }
    if (isEmotionExpression(expr) && emotionBlend.ready()) {
      const va = vaForExpression(expr);
      const m = emotionBlend.blendedEmotionArmMotion(va.v, va.a);
      if (m) {
        return Math.round(
          Math.max(0.05, m.waggle_period_s + m.waggle_interval_s) * 1000,
        );
      }
    }
    return motorPeriodMsFor(expr);
  }

  const kTweenMs = 250;
  const kTickIntervalMs = 33;
  const kBlinkCloseMs = 80;
  const kBlinkOpenMs = 130;
  const kThinkingFlipDurMs = 600;
  const kThinkingFlipMinMs = 3000;
  const kThinkingFlipMaxMs = 6000;
  const kIdleGlanceTweenMs = 200;

  let sCurrentExpr = "Neutral";
  let sFrom = targetForExpression("Neutral");
  let sTo = sFrom;
  let sTweenStartMs = 0;
  let sLastExpr: string | null = null;

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
  let sCurrentParams: FaceParams = { ...sFrom };
  let sLastSettingsVersion = 0;

  let sStaticMode = false;
  let sStaticOverride: StaticOverrideState = {
    params: { ...presetTargetForExpression("Neutral") },
    blinkAmt: 0,
    gdx: 0,
    gdy: 0,
    expression: "Neutral",
  };

  let sBlendMode = false;
  let sBlendV = 0.0;
  let sBlendA = 0.0;
  let sBlendLastParams: FaceParams = {
    ...presetTargetForExpression("Neutral"),
  };
  let sBlendLastTri: BlendTriangle | null = null;

  let sBodyBobPhaseRad = 0;
  let sBodyBobPhaseLastMs = 0;

  let sCurrentArmDeg = 0;
  let sArmLogicLastMs = 0;
  let sArmEmotionInOsc = true;
  let sArmEmotionOsc01 = 0;
  let sArmEmotionDwellS = 0;
  let sPrevArmDriverEmotion = false;

  function resetEmotionArmPhase(): void {
    sArmEmotionInOsc = true;
    sArmEmotionOsc01 = 0;
    sArmEmotionDwellS = 0;
    sArmLogicLastMs = 0;
  }

  function bodyBobFor(
    expr: string,
    now: number,
    blendMode: boolean,
    blendV: number,
    blendA: number,
  ): number {
    const period = motorPeriodMsForContext(expr, blendMode, blendV, blendA);
    if (period === 0) {
      sBodyBobPhaseLastMs = now;
      return 0;
    }

    let amp = 0;
    let integrate = false;
    if (blendMode && emotionBlend.ready()) {
      const m = emotionBlend.blendedEmotionArmMotion(blendV, blendA);
      integrate = true;
      if (m && m.min_offset_deg !== m.max_offset_deg) amp = 3;
    } else if (isEmotionExpression(expr) && emotionBlend.ready()) {
      const va = vaForExpression(expr);
      const m = emotionBlend.blendedEmotionArmMotion(va.v, va.a);
      integrate = true;
      if (m && m.min_offset_deg !== m.max_offset_deg) amp = 3;
    } else {
      switch (expr) {
        case "VerbSleeping":
          amp = 10;
          break;
        case "VerbExecuting":
        case "VerbStraining":
        case "Excited":
          amp = 5;
          break;
        case "Joyful":
          amp = 7;
          break;
        case "Sleepy":
          amp = 4;
          break;
        case "Distressed":
          amp = 6;
          break;
        default:
          amp = 0;
          break;
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

  function breathPhase(t: number): number {
    const u = (t % 4000) / 4000;
    return Math.sin(u * 2 * Math.PI);
  }

  function tickEmotionArm(dt: number, arm: EmotionArmMotion): number {
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

  function verbArmOffset(name: string, t: number): number {
    switch (name) {
      case "VerbReading":
        return -8;
      case "OverlayWaking":
        return 18;
      case "VerbThinking": {
        const T = 2000;
        const u = (t % T) / T;
        return -15 + 5 * Math.sin(u * 2 * Math.PI);
      }
      case "VerbWriting":
        return 5 + 4 * Math.sin(((t % 840) / 840) * 2 * Math.PI);
      case "VerbExecuting":
        return -5 + 5 * Math.sin(((t % 1000) / 1000) * 2 * Math.PI);
      case "VerbStraining":
        return 5 * Math.sin(((t % 750) / 750) * 2 * Math.PI);
      case "VerbSleeping":
        return -20 + 5 * Math.sin(((t % 8000) / 8000) * 2 * Math.PI);
      case "OverlayAttention":
        return 15 * Math.sin(((t % 900) / 900) * 2 * Math.PI);
      default:
        return 0;
    }
  }

  function updateArmOffset(t: number, expr: string): void {
    const EB = emotionBlend;
    const dt =
      sArmLogicLastMs === 0 ? 0 : Math.min(0.5, (t - sArmLogicLastMs) / 1000);
    sArmLogicLastMs = t;

    const armDriverEmotion = sBlendMode || isEmotionExpression(expr);
    if (armDriverEmotion && !sPrevArmDriverEmotion) {
      resetEmotionArmPhase();
    }
    sPrevArmDriverEmotion = armDriverEmotion;

    if (sBlendMode && EB.ready()) {
      const arm = EB.blendedEmotionArmMotion(sBlendV, sBlendA);
      sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
      return;
    }

    if (isEmotionExpression(expr) && EB.ready()) {
      const va = vaForExpression(expr);
      const arm = EB.blendedEmotionArmMotion(va.v, va.a);
      sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
      return;
    }

    resetEmotionArmPhase();
    sCurrentArmDeg = verbArmOffset(expr, t);
  }

  function drawArmOverlay(
    octx: CanvasRenderingContext2D,
    w: number,
    h: number,
    offsetDeg: number,
  ): void {
    const cx = w * 0.5;
    const cy = h * 0.5;
    const R = Math.min(w, h) * 0.5 - 14;
    const offsetRad = (offsetDeg * Math.PI) / 180;
    const thetaRight = -offsetRad;
    const thetaLeft = Math.PI - thetaRight;

    function palmAtOrbital(thetaRad: number): void {
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

  function now(): number {
    return performance.now() - sStartedAtMs;
  }

  function randRange(lo: number, hi: number): number {
    return lo + Math.random() * (hi - lo);
  }

  function randInt(lo: number, hi: number): number {
    return Math.floor(randRange(lo, hi));
  }

  function blinkPeriodMsFor(name: string): number {
    switch (name) {
      case "Neutral":
        return randInt(4000, 6500);
      case "VerbThinking":
        return randInt(2000, 3500);
      case "VerbReading":
        return randInt(4000, 6000);
      case "VerbWriting":
        return randInt(3500, 5500);
      case "VerbExecuting":
      case "VerbStraining":
        return randInt(4500, 7000);
      case "Excited":
        return randInt(2500, 4000);
      case "Happy":
        return randInt(3000, 4500);
      default:
        return 0;
    }
  }

  function scheduleNextBlink(name: string, from: number): void {
    const p = blinkPeriodMsFor(name);
    sNextBlinkMs = p === 0 ? 0 : from + p;
  }

  function currentBlinkAmount(t: number): number {
    if (!sBlinkActive) return 0;
    const d = t - sBlinkStartMs;
    if (d < kBlinkCloseMs) return d / kBlinkCloseMs;
    const d2 = d - kBlinkCloseMs;
    if (d2 < kBlinkOpenMs) return 1 - d2 / kBlinkOpenMs;
    sBlinkActive = false;
    return 0;
  }

  function currentThinkSign(t: number): number {
    if (sThinkFlipStartMs === 0) return sThinkToSign;
    const u = (t - sThinkFlipStartMs) / kThinkingFlipDurMs;
    return (
      sThinkFromSign + (sThinkToSign - sThinkFromSign) * face.smoothstep01(u)
    );
  }

  function resetThinkTilt(t: number): void {
    sThinkFromSign = 1;
    sThinkToSign = 1;
    sThinkFlipStartMs = 0;
    sNextThinkFlipMs = t + randInt(kThinkingFlipMinMs, kThinkingFlipMaxMs + 1);
  }

  function maybeFlipThinkTilt(t: number): void {
    if (sNextThinkFlipMs === 0 || t < sNextThinkFlipMs) return;
    sThinkFromSign = currentThinkSign(t);
    sThinkToSign = -sThinkFromSign;
    sThinkFlipStartMs = t;
    sNextThinkFlipMs =
      t +
      kThinkingFlipDurMs +
      randInt(kThinkingFlipMinMs, kThinkingFlipMaxMs + 1);
  }

  function gazeFor(name: string, t: number): [number, number] {
    let gdx = 0;
    let gdy = 0;
    switch (name) {
      case "Neutral": {
        if (sIdleGlanceStartMs !== 0) {
          const u = face.smoothstep01(
            (t - sIdleGlanceStartMs) / kIdleGlanceTweenMs,
          );
          gdx = lerpi(sIdleGlanceFromDx, sIdleGlanceDx, u);
          gdy = lerpi(sIdleGlanceFromDy, sIdleGlanceDy, u);
        } else {
          gdx = sIdleGlanceDx;
          gdy = sIdleGlanceDy;
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
      default:
        break;
    }
    return [gdx, gdy];
  }

  function onExpressionChange(newExpr: string, t: number): void {
    const u = (t - sTweenStartMs) / kTweenMs;
    const cur = lerpParams(sFrom, sTo, face.smoothstep01(u));
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
      sIdleGlanceDx = 0;
      sIdleGlanceDy = 0;
      sIdleGlanceFromDx = 0;
      sIdleGlanceFromDy = 0;
      sIdleGlanceStartMs = 0;
      sNextIdleGlanceMs = 0;
    }
  }

  let rafHandle: number | null = null;
  let sprite: TFTSprite | null = null;
  let outputCanvas: HTMLCanvasElement | null = null;
  let lastTickMs = 0;
  const listeners: Array<(name: string) => void> = [];

  function notifyExpression(): void {
    for (const fn of listeners) fn(sCurrentExpr);
  }

  function pushSpriteToCanvas(): void {
    if (!outputCanvas || !sprite) return;
    const octx = outputCanvas.getContext("2d");
    if (!octx) return;
    octx.imageSmoothingEnabled = false;
    octx.clearRect(0, 0, outputCanvas.width, outputCanvas.height);
    octx.drawImage(
      sprite.canvas,
      0,
      0,
      sprite.width,
      sprite.height,
      0,
      0,
      outputCanvas.width,
      outputCanvas.height,
    );
    if (!sStaticMode) {
      drawArmOverlay(
        octx,
        outputCanvas.width,
        outputCanvas.height,
        sCurrentArmDeg,
      );
    }
  }

  function tick(): void {
    const t = now();
    const expr = sCurrentExpr;

    const settingsVersion = settings.version();
    if (settingsVersion !== sLastSettingsVersion) {
      sLastSettingsVersion = settingsVersion;
      sTo = targetForExpression(expr);
    }

    if (sStaticMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        sCurrentArmDeg = 0;
        const o = sStaticOverride;
        face.renderScene(sprite!, o.params, o.blinkAmt, o.gdx, o.gdy, t);
        sCurrentParams = o.params;
        pushSpriteToCanvas();
      }
      rafHandle = requestAnimationFrame(tick);
      return;
    }

    if (sBlendMode) {
      if (t - lastTickMs >= kTickIntervalMs) {
        lastTickMs = t;
        const blender = emotionBlend;
        let p = sBlendLastParams;
        if (blender.ready()) {
          const blended = blender.blendedFaceParams(sBlendV, sBlendA);
          if (blended) p = blended;
          sBlendLastTri = blender.findTriangle(sBlendV, sBlendA);
        }
        sBlendLastParams = p;

        updateArmOffset(t, sCurrentExpr);

        if (
          sCurrentExpr !== "Joyful" &&
          sCurrentExpr !== "Gleeful" &&
          sCurrentExpr !== "VerbSleeping"
        ) {
          const b = breathPhase(t) * 1.5;
          p = {
            ...p,
            eye_dy: Math.round(p.eye_dy + b),
            mouth_dy: Math.round(p.mouth_dy + b / 2),
          };
        }
        p = {
          ...p,
          face_y: Math.round(
            p.face_y + bodyBobFor(sCurrentExpr, t, true, sBlendV, sBlendA),
          ),
        };

        face.renderScene(sprite!, p, 0, 0, 0, t);
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
      const te = face.smoothstep01(u);
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

      face.renderScene(sprite!, p, blinkAmt, gdx, gdy, t);
      sCurrentParams = p;
      pushSpriteToCanvas();
    }

    rafHandle = requestAnimationFrame(tick);
  }

  const knownExpressions = new Set(expressionsList());

  return {
    start(canvas: HTMLCanvasElement): void {
      if (rafHandle) return;
      sStartedAtMs = performance.now();
      sprite = new TFTSprite(240, 240);
      outputCanvas = canvas;
      sFrom = targetForExpression(sCurrentExpr);
      sTo = sFrom;
      sLastExpr = sCurrentExpr;
      sTweenStartMs = 0;
      sLastSettingsVersion = settings.version();
      resetEmotionArmPhase();
      sPrevArmDriverEmotion = false;
      sCurrentArmDeg = 0;
      rafHandle = requestAnimationFrame(tick);
    },

    stop(): void {
      if (rafHandle) cancelAnimationFrame(rafHandle);
      rafHandle = null;
    },

    requestExpression(name: string): void {
      if (!knownExpressions.has(name)) return;
      if (name === sCurrentExpr) return;
      sCurrentExpr = name;
      notifyExpression();
    },

    currentExpression(): string {
      return sCurrentExpr;
    },

    expressions(): string[] {
      return [...expressionsList()];
    },

    onExpressionChange(fn: (name: string) => void): void {
      listeners.push(fn);
    },

    params(): FaceParams {
      return sCurrentParams;
    },

    paramFields(): ParamField[] {
      return [...paramFieldsList()];
    },

    baseTargetForExpression(name: string): FaceParams {
      return presetTargetForExpression(name);
    },

    setStaticMode(on: boolean): void {
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

    isStatic(): boolean {
      return sStaticMode;
    },

    setBlendMode(on: boolean): void {
      sBlendMode = !!on;
      if (sBlendMode) {
        sStaticMode = false;
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

    isBlend(): boolean {
      return sBlendMode;
    },

    setBlendVA(v: number, a: number): void {
      if (typeof v === "number") sBlendV = Math.max(-1, Math.min(1, v));
      if (typeof a === "number") sBlendA = Math.max(0, Math.min(1, a));
    },

    blendVA(): { v: number; a: number } {
      return { v: sBlendV, a: sBlendA };
    },

    lastBlendTriangle(): BlendTriangle | null {
      return sBlendLastTri;
    },

    setStaticOverride(partial: {
      params?: Partial<FaceParams>;
      blinkAmt?: number;
      gdx?: number;
      gdy?: number;
      expression?: string;
    }): void {
      if (!partial) return;
      if (partial.params) {
        sStaticOverride.params = {
          ...sStaticOverride.params,
          ...partial.params,
        };
      }
      if (typeof partial.blinkAmt === "number")
        sStaticOverride.blinkAmt = partial.blinkAmt;
      if (typeof partial.gdx === "number") sStaticOverride.gdx = partial.gdx;
      if (typeof partial.gdy === "number") sStaticOverride.gdy = partial.gdy;
      if (typeof partial.expression === "string")
        sStaticOverride.expression = partial.expression;
    },

    staticOverride(): StaticOverrideState {
      return {
        params: { ...sStaticOverride.params },
        blinkAmt: sStaticOverride.blinkAmt,
        gdx: sStaticOverride.gdx,
        gdy: sStaticOverride.gdy,
        expression: sStaticOverride.expression,
      };
    },

    armOffsetDeg(): number {
      return sCurrentArmDeg;
    },
  };
}
