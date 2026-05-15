/**
 * Port of `robot_v3/src/face/FrameController.cpp` — tick order matches firmware
 * (text-stream / progress-fade paths omitted; not used by the face simulator).
 */

import { createEmotionBlend, type EmotionBlendApi } from "./emotionBlend";
import {
    BOB_AMP_FOLLOW_EMOTION_ARM,
    Expression,
    FieldIndex,
    GazeStyle,
    EXPRESSIONS,
    MotionMode,
    type IdleAnimRow,
    type ParamI16,
    kFrameAnim,
    kIdleAnim,
    kMotion,
    expressionIndexFromName,
    isEmotionExpressionIndex,
} from "./FACE_CONFIG_DATA";
import { EMOTION_TRIANGULATION } from "./emotionTriangulation";
import { cloneMutableEmotionTriangulation } from "./emotionTriangulationLive";
import { cloneMutableBaseTargets } from "./mutableBaseTargets";
import { cloneMutableVerbTimelines, type MutableVerbTimeline } from "./mutableVerbTimelines";
import {
    PARAM_FIELDS,
    faceParamsFromIndexed,
    faceStrengthsFromIndexed,
    indexedFromFaceParams,
    mergeStrengthsIntoIndexedRow,
    mergeValuesIntoIndexedRow,
    type FaceParams,
    type ParamField,
} from "./faceParams";
import { createFaceRenderer } from "./faceRenderer";
import {
    baseTargetForExpression as presetTargetForExpression,
    expressionsList,
    isEmotionExpression,
    paramFieldsList,
} from "./presets";
import { createRobotSettings } from "./robotSettings";
import {
    cloneFaceParamsIndexed,
    combineEmotionVerbFace,
    smoothFaceValuesToward,
} from "./sceneFaceCombine";
import { TFTSprite, tft } from "./tftSprite";
import type { BlendTriangle, EmotionArmMotion, EmotionTriangulationTable } from "./types";
import {
    expressionUsesVerbTimeline,
    isVerbExpression,
    resetVerbTransition,
    sampleEffectiveVerb,
    type VerbTimelineTableResolver,
    verbTransitionT,
} from "./verbTimeline";

const PI = Math.PI;

function lerpi16(a: number, b: number, t: number): number {
    return Math.round(a + (b - a) * t);
}

function smoothstep01(t: number): number {
    const x = t < 0 ? 0 : t > 1 ? 1 : t;
    return x * x * (3 - 2 * x);
}

function randInt(lo: number, hi: number): number {
    return Math.floor(Math.random() * (hi - lo + 1)) + lo;
}

function periodMsForExpressionIndex(idx: number): number {
    const m = kMotion[idx];
    if (!m) return 0;
    switch (m.mode) {
        case MotionMode.Oscillate:
        case MotionMode.Waggle:
        case MotionMode.Thinking:
            return m.period_ms;
        default:
            return 0;
    }
}

function periodMsForContext(
    tri: EmotionTriangulationTable,
    exprIdx: number,
    blendMode: boolean,
    blendV: number,
    blendA: number,
    emotionBlend: ReturnType<typeof createEmotionBlend>
): number {
    if (isEmotionExpressionIndex(exprIdx) && emotionBlend.ready()) {
        if (blendMode) {
            const m = emotionBlend.blendedEmotionArmMotion(blendV, blendA);
            if (m) {
                const total = m.waggle_period_s + m.waggle_interval_s;
                let msf = total * 1000.0;
                if (msf < 50.0) msf = 50.0;
                if (msf > 65535.0) return 65535;
                return Math.round(msf);
            }
        } else {
            const { v, a } = anchorVaForExprIdx(tri, exprIdx);
            const m = emotionBlend.blendedEmotionArmMotion(v, a);
            if (m) {
                const total = m.waggle_period_s + m.waggle_interval_s;
                let msf = total * 1000.0;
                if (msf < 50.0) msf = 50.0;
                if (msf > 65535.0) return 65535;
                return Math.round(msf);
            }
        }
    }
    return periodMsForExpressionIndex(exprIdx);
}

function anchorVaForExprIdx(
    tri: EmotionTriangulationTable,
    exprIdx: number
): { v: number; a: number } {
    const an = tri.anchors.find(x => expressionIndexFromName(x.emotion) === exprIdx);
    return { v: an?.v ?? 0, a: an?.a ?? 0.5 };
}

function idleFor(
    tri: EmotionTriangulationTable,
    exprIdx: number,
    blendMode: boolean,
    blendV: number,
    blendA: number,
    emotionBlend: ReturnType<typeof createEmotionBlend>
): IdleAnimRow {
    if (isEmotionExpressionIndex(exprIdx)) {
        if (blendMode && emotionBlend.ready()) {
            const row = emotionBlend.blendedIdleAnim(blendV, blendA);
            if (row) return row;
        }
        if (!blendMode && emotionBlend.ready()) {
            const an = tri.anchors.find(x => expressionIndexFromName(x.emotion) === exprIdx);
            if (an) {
                const row = emotionBlend.blendedIdleAnim(an.v, an.a);
                if (row) return row;
            }
        }
    }
    return { ...kIdleAnim[exprIdx]! };
}

function bodyBobAmpFor(
    tri: EmotionTriangulationTable,
    exprIdx: number,
    idle: IdleAnimRow,
    blendMode: boolean,
    blendV: number,
    blendA: number,
    emotionBlend: ReturnType<typeof createEmotionBlend>
): number {
    if (idle.bob_amplitude_px === BOB_AMP_FOLLOW_EMOTION_ARM) {
        if (isEmotionExpressionIndex(exprIdx) && emotionBlend.ready()) {
            const { v, a } = anchorVaForExprIdx(tri, exprIdx);
            const m = blendMode
                ? emotionBlend.blendedEmotionArmMotion(blendV, blendA)
                : emotionBlend.blendedEmotionArmMotion(v, a);
            if (m && m.min_offset_deg !== m.max_offset_deg) {
                return kFrameAnim.emotion_bob_amp_follow_arm;
            }
        }
        return 0;
    }
    return idle.bob_amplitude_px;
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
    /** Shipped preset row (ignores editor mutations to live base targets). */
    baseTargetForExpression(name: string): FaceParams;
    /** Current editor-local base row as `FaceParams` (mutable copy). */
    liveBaseFaceParams(name: string): FaceParams;
    /** Per-field strengths 0–100 for that emotion's base row in the live table. */
    liveBaseFaceStrengths(name: string): FaceParams;
    patchLiveBaseFaceParams(name: string, partial: Partial<FaceParams>): void;
    patchLiveBaseFaceStrengths(name: string, partial: Partial<FaceParams>): void;
    setStaticMode(on: boolean): void;
    isStatic(): boolean;
    setBlendMode(on: boolean): void;
    isBlend(): boolean;
    setBlendVA(v: number, a: number): void;
    blendVA(): { v: number; a: number };
    lastBlendTriangle(): BlendTriangle | null;
    /** Same blend instance used for ticks and the V/A diagram. */
    emotionBlendApi(): EmotionBlendApi;
    /** Mutable live triangulation (clone of shipped data); safe to edit in the editor. */
    emotionTriangulation(): EmotionTriangulationTable;
    /** Session-local verb keyframe tables (clone of shipped `kVerbTimelines`; lost on refresh). */
    verbTimelines(): MutableVerbTimeline[];
    /** When set, `tick()` uses `timeMs` as verb phase for that verb instead of wall-clock verb time. */
    setVerbTimelinePreview(p: { verb: Expression; timeMs: number } | null): void;
    /**
     * While verb timeline preview is active, smooth the base face toward this blended
     * V/A point instead of the verb expression row in `kBaseTargets`.
     */
    setVerbPreviewBaseVa(v: number, a: number): void;
    verbPreviewBaseVa(): { v: number; a: number };
    /**
     * When blend mode is on, optionally sample this verb timeline on top of the blended face.
     * Use `null` for no verb overlay (matches bridge `POST /api/raw/verb/clear`).
     */
    setBlendVerbPreview(verb: Expression | null): void;
    blendVerbPreview(): Expression | null;
    setStaticOverride(partial: {
        params?: Partial<FaceParams>;
        blinkAmt?: number;
        gdx?: number;
        gdy?: number;
        expression?: string;
    }): void;
    staticOverride(): StaticOverrideState;
    /** Last values passed to `renderScene` (blink + gaze); meaningful when not in static mode. */
    liveRenderMod(): { blinkAmt: number; gdx: number; gdy: number };
    armOffsetDeg(): number;
}

export interface CreateFrameControllerOptions {
    /** Defaults to a mutable clone of `EMOTION_TRIANGULATION` (editor can move anchors). */
    triangulation?: EmotionTriangulationTable;
}

export function createFrameController(opts?: CreateFrameControllerOptions): FrameController {
    const liveTriangulation =
        opts?.triangulation ?? cloneMutableEmotionTriangulation(EMOTION_TRIANGULATION);
    const liveBaseTargets = cloneMutableBaseTargets();
    const liveVerbTimelines = cloneMutableVerbTimelines();
    const emotionBlend = createEmotionBlend({
        triangulation: liveTriangulation as EmotionTriangulationTable,
        baseTargets: liveBaseTargets,
    });

    const settings = createRobotSettings();
    const face = createFaceRenderer({ settings, tft });

    const animCfg = () => kFrameAnim;

    let sSmoothed: ParamI16[] = cloneFaceParamsIndexed(liveBaseTargets[0]!);
    let sLastRendered: ParamI16[] = cloneFaceParamsIndexed(liveBaseTargets[0]!);
    let sLastEmotionSmoothMs = 0;

    let sNextBlinkMs = 0;
    let sBlinkStartMs = 0;
    let sBlinkActive = false;

    let sLastTickMs = 0;
    let sMoodR = 0;
    let sMoodG = 0;
    let sMoodB = 0;
    let sLastMoodMs = 0;

    let sIdleGlanceDx = 0;
    let sIdleGlanceDy = 0;
    let sIdleGlanceFromDx = 0;
    let sIdleGlanceFromDy = 0;
    let sIdleGlanceStartMs = 0;
    let sNextIdleGlanceMs = 0;

    let sBodyBobPhaseRad = 0;
    let sBodyBobPhaseLastMs = 0;
    let sLastBobAmp = 0;
    let sFromBobAmp = 0;
    let sFromGdx = 0;
    let sFromGdy = 0;
    let sLastGdx = 0;
    let sLastGdy = 0;
    let sLastVerbForXfade = Expression.Count;

    let sGazePhaseRad = 0;
    let sGazePhaseLastMs = 0;
    let sEyeWavePhaseRad = 0;
    let sMouthWavePhaseRad = 0;
    let sWavePhaseLastMs = 0;

    let sLastExprIdx = -1;
    let sVerbEnteredMs = 0;
    let sLastSettingsVersion = 0;

    let sCurrentParams: FaceParams = faceParamsFromIndexed(sSmoothed);
    let sLiveBlinkAmt = 0;
    let sLiveGdx = 0;
    let sLiveGdy = 0;
    let sCurrentExpr = "Neutral";

    let sStaticMode = false;
    let sStaticOverride: StaticOverrideState = {
        params: { ...presetTargetForExpression("Neutral") },
        blinkAmt: 0,
        gdx: 0,
        gdy: 0,
        expression: "Neutral",
    };

    let sBlendMode = false;
    let sBlendV = 0;
    let sBlendA = 0.5;
    let sBlendLastTri: BlendTriangle | null = null;

    let sCurrentArmDeg = 0;
    let sArmLogicLastMs = 0;
    let sArmEmotionInOsc = true;
    let sArmEmotionOsc01 = 0;
    let sArmEmotionDwellS = 0;
    let sPrevArmDriverEmotion = false;

    let sVerbTimelinePreview: { verb: Expression; timeMs: number } | null = null;

    /** Base emotion (V/A) under verb-timeline preview — see `setVerbPreviewBaseVa`. */
    let sVerbPreviewBaseVa = { v: 0, a: 0.5 };
    /** Optional verb layered in blend-mode tick — see `setBlendVerbPreview`. */
    let sBlendVerbPreview: Expression | null = null;
    let sBlendVerbEnteredMs = 0;

    const resolveLiveVerbTable: VerbTimelineTableResolver = (verb: Expression) =>
        liveVerbTimelines.find(t => t.verb === verb);

    const listeners: Array<(name: string) => void> = [];
    let rafHandle: number | null = null;
    let sprite: TFTSprite | null = null;
    let outputCanvas: HTMLCanvasElement | null = null;

    function now(): number {
        return performance.now();
    }

    function tickDispatch(): void {
        if (sStaticMode) tickStatic();
        else if (sBlendMode) tickBlend();
        else tick();
        rafHandle = requestAnimationFrame(tickDispatch);
    }

    function breathPhase(t: number): number {
        const periodMs = animCfg().breath_period_ms || 4000;
        const u = (t % periodMs) / periodMs;
        return Math.sin(u * 2 * PI);
    }

    function scheduleNextBlink(idle: IdleAnimRow, from: number): void {
        if (idle.blink_period_min_ms === 0 && idle.blink_period_max_ms === 0) {
            sNextBlinkMs = 0;
            return;
        }
        if (idle.blink_period_max_ms < idle.blink_period_min_ms) {
            sNextBlinkMs = 0;
            return;
        }
        const p = randInt(idle.blink_period_min_ms, idle.blink_period_max_ms);
        sNextBlinkMs = from + p;
    }

    function currentBlinkAmount(t: number, idle: IdleAnimRow): number {
        if (!sBlinkActive) return 0;
        const closeMs = idle.blink_close_ms || animCfg().default_blink_close_ms;
        const openMs = idle.blink_open_ms || animCfg().default_blink_open_ms;
        const d = t - sBlinkStartMs;
        if (d < closeMs) return d / closeMs;
        const d2 = d - closeMs;
        if (d2 < openMs) return 1 - d2 / openMs;
        sBlinkActive = false;
        return 0;
    }

    function gazeFor(idle: IdleAnimRow, t: number): [number, number] {
        let gdx = 0;
        let gdy = 0;
        const per = idle.gaze_scan_period_ms;
        if (per !== 0) {
            const dtMs = sGazePhaseLastMs === 0 ? 0 : t - sGazePhaseLastMs;
            sGazePhaseRad += ((2 * PI) / per) * dtMs;
            sGazePhaseRad %= 2 * PI;
            if (sGazePhaseRad < 0) sGazePhaseRad += 2 * PI;
        }
        sGazePhaseLastMs = t;

        switch (idle.gaze_style) {
            case GazeStyle.IdleRandom: {
                const moveMs = idle.gaze_move_ms || animCfg().default_gaze_move_ms;
                if (sIdleGlanceStartMs !== 0) {
                    const u = smoothstep01((t - sIdleGlanceStartMs) / moveMs);
                    gdx = lerpi16(sIdleGlanceFromDx, sIdleGlanceDx, u);
                    gdy = lerpi16(sIdleGlanceFromDy, sIdleGlanceDy, u);
                } else {
                    gdx = sIdleGlanceDx;
                    gdy = sIdleGlanceDy;
                }
                if (sNextIdleGlanceMs === 0 || t >= sNextIdleGlanceMs) {
                    sIdleGlanceFromDx = gdx;
                    sIdleGlanceFromDy = gdy;
                    const sx = idle.gaze_rand_span_x > 0 ? idle.gaze_rand_span_x : 0;
                    const sy = idle.gaze_rand_span_y > 0 ? idle.gaze_rand_span_y : 0;
                    sIdleGlanceDx = randInt(-sx, sx);
                    sIdleGlanceDy = randInt(-sy, sy);
                    sIdleGlanceStartMs = t;
                    if (idle.gaze_reroll_max_ms >= idle.gaze_reroll_min_ms) {
                        sNextIdleGlanceMs =
                            t + randInt(idle.gaze_reroll_min_ms, idle.gaze_reroll_max_ms + 1);
                    } else {
                        sNextIdleGlanceMs = t + animCfg().invalid_gaze_reroll_fallback_ms;
                    }
                }
                break;
            }
            case GazeStyle.Orbit: {
                if (per === 0) break;
                gdx = Math.round(Math.sin(sGazePhaseRad) * idle.gaze_amp_x);
                gdy = Math.round(Math.cos(sGazePhaseRad) * idle.gaze_amp_y);
                break;
            }
            case GazeStyle.ScanX: {
                if (per === 0) break;
                gdx = Math.round(Math.sin(sGazePhaseRad) * idle.gaze_amp_x);
                break;
            }
            default:
                break;
        }
        return [gdx, gdy];
    }

    function bodyBobFor(
        exprIdx: number,
        idle: IdleAnimRow,
        t: number,
        blendMode: boolean,
        blendV: number,
        blendA: number
    ): number {
        const period = periodMsForContext(
            liveTriangulation,
            exprIdx,
            blendMode,
            blendV,
            blendA,
            emotionBlend
        );
        if (period === 0) {
            sBodyBobPhaseLastMs = t;
            sLastBobAmp = 0;
            return 0;
        }
        const liveAmp = bodyBobAmpFor(
            liveTriangulation,
            exprIdx,
            idle,
            blendMode,
            blendV,
            blendA,
            emotionBlend
        );
        const tt = verbTransitionT(t);
        const effAmpF = tt >= 1.0 ? liveAmp : sFromBobAmp + (liveAmp - sFromBobAmp) * tt;
        const amp = Math.round(effAmpF);
        sLastBobAmp = amp;
        const integrate = liveAmp !== 0 || sFromBobAmp !== 0;
        const twoPi = 2 * PI;
        if (integrate) {
            const dtMs = sBodyBobPhaseLastMs === 0 ? 0 : t - sBodyBobPhaseLastMs;
            sBodyBobPhaseLastMs = t;
            sBodyBobPhaseRad += (twoPi / period) * dtMs;
            sBodyBobPhaseRad %= twoPi;
            if (sBodyBobPhaseRad < 0) sBodyBobPhaseRad += twoPi;
        } else {
            sBodyBobPhaseLastMs = t;
        }
        if (amp === 0) return 0;
        return Math.round(-Math.sin(sBodyBobPhaseRad) * amp);
    }

    function onExpressionChange(newExprIdx: number, t: number): void {
        sLastExprIdx = newExprIdx;
        sBlinkActive = false;
        const idleNew = idleFor(
            liveTriangulation,
            newExprIdx,
            sBlendMode,
            sBlendV,
            sBlendA,
            emotionBlend
        );
        scheduleNextBlink(idleNew, t);

        if (idleNew.gaze_style === GazeStyle.IdleRandom) {
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

        if (isVerbExpression(newExprIdx as Expression)) {
            sVerbEnteredMs = t;
        }
    }

    function resetEmotionArmPhase(): void {
        sArmEmotionInOsc = true;
        sArmEmotionOsc01 = 0;
        sArmEmotionDwellS = 0;
        sArmLogicLastMs = 0;
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

    function verbArmOffset(exprIdx: number, t: number): number {
        const m = kMotion[exprIdx];
        if (!m) return 0;
        switch (exprIdx) {
            case Expression.VerbReading:
                return -8;
            case Expression.OverlayWaking:
                return 18;
            case Expression.VerbThinking: {
                const T = m.period_ms || 2000;
                const u = (t % T) / T;
                return -15 + m.amplitude * Math.sin(u * 2 * PI);
            }
            case Expression.VerbWriting: {
                const T = m.period_ms || 840;
                return m.center + m.amplitude * Math.sin(((t % T) / T) * 2 * PI);
            }
            case Expression.VerbExecuting: {
                const T = m.period_ms || 1000;
                return m.center + m.amplitude * Math.sin(((t % T) / T) * 2 * PI);
            }
            case Expression.VerbStraining: {
                const T = m.period_ms || 750;
                return m.amplitude * Math.sin(((t % T) / T) * 2 * PI);
            }
            case Expression.VerbSleeping: {
                const T = m.period_ms || 8000;
                return m.center + m.amplitude * Math.sin(((t % T) / T) * 2 * PI);
            }
            case Expression.OverlayAttention: {
                const T = m.period_ms || 900;
                return m.amplitude * Math.sin(((t % T) / T) * 2 * PI);
            }
            default:
                return 0;
        }
    }

    function updateArmOffset(t: number, exprIdx: number): void {
        const dt = sArmLogicLastMs === 0 ? 0 : Math.min(0.5, (t - sArmLogicLastMs) / 1000);
        sArmLogicLastMs = t;

        const armDriverEmotion = sBlendMode || isEmotionExpressionIndex(exprIdx);
        if (armDriverEmotion && !sPrevArmDriverEmotion) {
            resetEmotionArmPhase();
        }
        sPrevArmDriverEmotion = armDriverEmotion;

        if (sBlendMode && emotionBlend.ready()) {
            const arm = emotionBlend.blendedEmotionArmMotion(sBlendV, sBlendA);
            sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
            return;
        }

        if (isEmotionExpressionIndex(exprIdx) && emotionBlend.ready()) {
            const an = liveTriangulation.anchors.find(
                x => expressionIndexFromName(x.emotion) === exprIdx
            );
            const arm = an ? emotionBlend.blendedEmotionArmMotion(an.v, an.a) : null;
            sCurrentArmDeg = arm ? tickEmotionArm(dt, arm) : 0;
            return;
        }

        resetEmotionArmPhase();
        sCurrentArmDeg = verbArmOffset(exprIdx, t);
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
            outputCanvas.height
        );
        if (!sStaticMode) {
            drawArmOverlay(octx, outputCanvas.width, outputCanvas.height, sCurrentArmDeg);
        }
    }

    function drawArmOverlay(
        octx: CanvasRenderingContext2D,
        w: number,
        h: number,
        offsetDeg: number
    ): void {
        const cx = w * 0.5;
        const cy = h * 0.5;
        const R = Math.min(w, h) * 0.5 - 14;
        const offsetRad = (offsetDeg * PI) / 180;
        const thetaRight = -offsetRad;
        const thetaLeft = PI - thetaRight;

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

    function notifyExpression(): void {
        for (const fn of listeners) fn(sCurrentExpr);
    }

    function tick(): void {
        const t = now();
        const exprIdx = expressionIndexFromName(sCurrentExpr);
        const exprEnum = exprIdx as Expression;

        const settingsVersion = settings.version();
        if (settingsVersion !== sLastSettingsVersion) {
            sLastSettingsVersion = settingsVersion;
            const tgt = liveBaseTargets[exprIdx]!;
            if (tgt) smoothFaceValuesToward(sSmoothed, tgt, 1.0);
            const flat = faceParamsFromIndexed(sSmoothed);
            sMoodR = flat.ring_r;
            sMoodG = flat.ring_g;
            sMoodB = flat.ring_b;
            sLastMoodMs = t;
        }

        const tickInterval = animCfg().tick_interval_ms;
        if (t - sLastTickMs < tickInterval) {
            return;
        }
        sLastTickMs = t;

        if (exprIdx !== sLastExprIdx) {
            onExpressionChange(exprIdx, t);
        }

        const idle = idleFor(liveTriangulation, exprIdx, false, sBlendV, sBlendA, emotionBlend);

        const emoDt = sLastEmotionSmoothMs === 0 ? tickInterval : t - sLastEmotionSmoothMs;
        sLastEmotionSmoothMs = t;
        const emoAlpha = 1.0 - Math.exp(-emoDt / animCfg().emotion_geometry_smooth_tau_ms);

        let targetRow: ParamI16[] = [...liveBaseTargets[exprIdx]!];
        if (
            sVerbTimelinePreview !== null &&
            expressionUsesVerbTimeline(exprEnum) &&
            emotionBlend.ready()
        ) {
            const blended = emotionBlend.blendedFaceParamsIndexed(
                sVerbPreviewBaseVa.v,
                sVerbPreviewBaseVa.a
            );
            if (blended) targetRow = [...blended];
        }
        smoothFaceValuesToward(sSmoothed, targetRow, emoAlpha);

        if (exprEnum !== sLastVerbForXfade) {
            sFromBobAmp = sLastBobAmp;
            sFromGdx = sLastGdx;
            sFromGdy = sLastGdy;
            sLastVerbForXfade = exprEnum;
        }

        let verbTime = 0;
        if (isVerbExpression(exprEnum)) {
            if (sVerbTimelinePreview !== null && sVerbTimelinePreview.verb === exprEnum) {
                verbTime = sVerbTimelinePreview.timeMs;
            } else {
                verbTime = Math.max(0, t - sVerbEnteredMs);
            }
        }
        const verbHas: boolean[] = new Array(FieldIndex.Count).fill(false);
        const verbVals: ParamI16[] = Array.from({ length: FieldIndex.Count }, () => ({
            value: 0,
            strength: 0,
        }));
        sampleEffectiveVerb(exprEnum, t, verbTime, verbHas, verbVals, resolveLiveVerbTable);

        let combined = combineEmotionVerbFace(sSmoothed, verbHas, verbVals);
        let pFlat = faceParamsFromIndexed(combined);

        const moodDt = sLastMoodMs === 0 ? 0 : t - sLastMoodMs;
        const moodAlpha = 1.0 - Math.exp(-moodDt / animCfg().mood_ring_tau_ms);
        sMoodR += (pFlat.ring_r - sMoodR) * moodAlpha;
        sMoodG += (pFlat.ring_g - sMoodG) * moodAlpha;
        sMoodB += (pFlat.ring_b - sMoodB) * moodAlpha;
        sLastMoodMs = t;

        const exN = EXPRESSIONS[exprIdx]!;
        if (
            !expressionUsesVerbTimeline(exprEnum) &&
            exN !== "Joyful" &&
            exN !== "Gleeful" &&
            exN !== "VerbSleeping"
        ) {
            const b = breathPhase(t) * animCfg().breath_eye_amp_px;
            pFlat = {
                ...pFlat,
                eye_dy: Math.round(pFlat.eye_dy + b),
                mouth_dy: Math.round(pFlat.mouth_dy + b * animCfg().breath_mouth_scale),
            };
        }

        pFlat = {
            ...pFlat,
            face_y: Math.round(
                pFlat.face_y + bodyBobFor(exprIdx, idle, t, false, sBlendV, sBlendA)
            ),
        };

        if (!sBlinkActive) {
            if (sNextBlinkMs === 0) scheduleNextBlink(idle, t);
            else if (t >= sNextBlinkMs) {
                sBlinkActive = true;
                sBlinkStartMs = t;
                sNextBlinkMs = 0;
            }
        }
        const blinkAmt = currentBlinkAmount(t, idle);
        if (!sBlinkActive && sNextBlinkMs === 0) scheduleNextBlink(idle, t);

        let liveGdx = 0;
        let liveGdy = 0;
        if (!expressionUsesVerbTimeline(exprEnum)) {
            const gaze = gazeFor(idle, t);
            liveGdx = gaze[0]!;
            liveGdy = gaze[1]!;
        }
        const ttGaze = verbTransitionT(t);
        let gdx: number;
        let gdy: number;
        if (ttGaze >= 1.0) {
            gdx = liveGdx;
            gdy = liveGdy;
        } else {
            gdx = Math.round(sFromGdx + (liveGdx - sFromGdx) * ttGaze);
            gdy = Math.round(sFromGdy + (liveGdy - sFromGdy) * ttGaze);
        }
        sLastGdx = gdx;
        sLastGdy = gdy;

        const waveDtMs = sWavePhaseLastMs === 0 ? 0 : t - sWavePhaseLastMs;
        sWavePhaseLastMs = t;
        const kPiOver180000 = PI / 180000.0;
        sEyeWavePhaseRad += pFlat.eye_wave_speed * waveDtMs * kPiOver180000;
        sMouthWavePhaseRad += pFlat.mouth_wave_speed * waveDtMs * kPiOver180000;
        const kTwoPi = 2 * PI;
        sEyeWavePhaseRad %= kTwoPi;
        if (sEyeWavePhaseRad < 0) sEyeWavePhaseRad += kTwoPi;
        sMouthWavePhaseRad %= kTwoPi;
        if (sMouthWavePhaseRad < 0) sMouthWavePhaseRad += kTwoPi;

        updateArmOffset(t, exprIdx);

        combined = indexedFromFaceParams(pFlat);
        sLastRendered = [...combined];

        sLiveBlinkAmt = blinkAmt;
        sLiveGdx = gdx;
        sLiveGdy = gdy;

        if (sprite) {
            face.renderScene(sprite, pFlat, blinkAmt, gdx, gdy, t);
        }
        sCurrentParams = { ...pFlat };
        pushSpriteToCanvas();
    }

    function tickStatic(): void {
        const t = now();
        const tickInterval = animCfg().tick_interval_ms;
        if (t - sLastTickMs < tickInterval) {
            return;
        }
        sLastTickMs = t;
        const p = { ...sStaticOverride.params };
        sLiveBlinkAmt = sStaticOverride.blinkAmt;
        sLiveGdx = sStaticOverride.gdx;
        sLiveGdy = sStaticOverride.gdy;
        if (sprite) {
            face.renderScene(
                sprite,
                p,
                sStaticOverride.blinkAmt,
                sStaticOverride.gdx,
                sStaticOverride.gdy,
                t
            );
        }
        sCurrentParams = { ...p };
        sLastRendered = indexedFromFaceParams(p);
        pushSpriteToCanvas();
    }

    function tickBlend(): void {
        const t = now();
        if (t - sLastTickMs < animCfg().tick_interval_ms) {
            return;
        }
        sLastTickMs = t;
        const exprIdx = expressionIndexFromName(sCurrentExpr);
        const idle = idleFor(liveTriangulation, exprIdx, true, sBlendV, sBlendA, emotionBlend);

        const blended = emotionBlend.blendedFaceParamsIndexed(sBlendV, sBlendA);
        if (!blended) {
            return;
        }
        smoothFaceValuesToward(sSmoothed, blended, 1.0);

        const verbHas: boolean[] = new Array(FieldIndex.Count).fill(false);
        const verbVals: ParamI16[] = Array.from({ length: FieldIndex.Count }, () => ({
            value: 0,
            strength: 0,
        }));
        let blendVerbPhaseExpr: Expression = Expression.Count;
        let blendVerbTimeMs = 0;
        if (sBlendVerbPreview !== null && expressionUsesVerbTimeline(sBlendVerbPreview)) {
            blendVerbPhaseExpr = sBlendVerbPreview;
            blendVerbTimeMs = Math.max(0, t - sBlendVerbEnteredMs);
        }
        sampleEffectiveVerb(
            blendVerbPhaseExpr,
            t,
            blendVerbTimeMs,
            verbHas,
            verbVals,
            resolveLiveVerbTable
        );

        let combined = combineEmotionVerbFace(sSmoothed, verbHas, verbVals);
        let pFlat = faceParamsFromIndexed(combined);

        const moodDt = sLastMoodMs === 0 ? 0 : t - sLastMoodMs;
        const moodAlpha = 1.0 - Math.exp(-moodDt / animCfg().mood_ring_tau_ms);
        sMoodR += (pFlat.ring_r - sMoodR) * moodAlpha;
        sMoodG += (pFlat.ring_g - sMoodG) * moodAlpha;
        sMoodB += (pFlat.ring_b - sMoodB) * moodAlpha;
        sLastMoodMs = t;

        if (
            sCurrentExpr !== "Joyful" &&
            sCurrentExpr !== "Gleeful" &&
            sCurrentExpr !== "VerbSleeping"
        ) {
            const b = breathPhase(t) * animCfg().breath_eye_amp_px;
            pFlat = {
                ...pFlat,
                eye_dy: Math.round(pFlat.eye_dy + b),
                mouth_dy: Math.round(pFlat.mouth_dy + b * animCfg().breath_mouth_scale),
            };
        }

        pFlat = {
            ...pFlat,
            face_y: Math.round(pFlat.face_y + bodyBobFor(exprIdx, idle, t, true, sBlendV, sBlendA)),
        };

        updateArmOffset(t, exprIdx);

        if (emotionBlend.ready()) {
            sBlendLastTri = emotionBlend.findTriangle(sBlendV, sBlendA);
        }

        sLiveBlinkAmt = 0;
        sLiveGdx = 0;
        sLiveGdy = 0;
        if (sprite) {
            face.renderScene(sprite, pFlat, 0, 0, 0, t);
        }
        sCurrentParams = { ...pFlat };
        pushSpriteToCanvas();
    }

    const knownExpressions = new Set(expressionsList());

    return {
        start(canvas: HTMLCanvasElement): void {
            if (rafHandle) return;
            sprite = new TFTSprite(240, 240);
            outputCanvas = canvas;
            resetVerbTransition();
            sSmoothed = cloneFaceParamsIndexed(liveBaseTargets[0]!);
            sLastRendered = cloneFaceParamsIndexed(liveBaseTargets[0]!);
            sLastExprIdx = -1;
            sLastTickMs = 0;
            sLastEmotionSmoothMs = 0;
            sLastSettingsVersion = settings.version();
            const flat = faceParamsFromIndexed(sSmoothed);
            sMoodR = flat.ring_r;
            sMoodG = flat.ring_g;
            sMoodB = flat.ring_b;
            sLastMoodMs = 0;
            sPrevArmDriverEmotion = isEmotionExpression(sCurrentExpr);
            resetEmotionArmPhase();
            sCurrentArmDeg = 0;
            rafHandle = requestAnimationFrame(tickDispatch);
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
            return { ...sCurrentParams };
        },

        paramFields(): ParamField[] {
            return [...paramFieldsList()];
        },

        baseTargetForExpression(name: string): FaceParams {
            return presetTargetForExpression(name);
        },

        liveBaseFaceParams(name: string): FaceParams {
            const i = expressionIndexFromName(name);
            const row = liveBaseTargets[i];
            if (!row) return { ...faceParamsFromIndexed(liveBaseTargets[0]!) };
            return { ...faceParamsFromIndexed(row) };
        },

        liveBaseFaceStrengths(name: string): FaceParams {
            const i = expressionIndexFromName(name);
            const row = liveBaseTargets[i];
            if (!row) return { ...faceStrengthsFromIndexed(liveBaseTargets[0]!) };
            return { ...faceStrengthsFromIndexed(row) };
        },

        patchLiveBaseFaceParams(name: string, partial: Partial<FaceParams>): void {
            const i = expressionIndexFromName(name);
            const row = liveBaseTargets[i];
            if (!row) return;
            mergeValuesIntoIndexedRow(row, partial);
        },

        patchLiveBaseFaceStrengths(name: string, partial: Partial<FaceParams>): void {
            const i = expressionIndexFromName(name);
            const row = liveBaseTargets[i];
            if (!row) return;
            mergeStrengthsIntoIndexedRow(row, partial);
        },

        setStaticMode(on: boolean): void {
            sStaticMode = !!on;
            if (sStaticMode) {
                sBlendMode = false;
                sStaticOverride.params = { ...sCurrentParams };
                sLastTickMs = 0;
            } else {
                sLastExprIdx = -1;
                sLastTickMs = 0;
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
                sLastTickMs = 0;
            } else {
                sPrevArmDriverEmotion = isEmotionExpression(sCurrentExpr);
                sLastExprIdx = -1;
                sLastTickMs = 0;
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

        emotionBlendApi(): EmotionBlendApi {
            return emotionBlend;
        },

        emotionTriangulation(): EmotionTriangulationTable {
            return liveTriangulation as EmotionTriangulationTable;
        },

        verbTimelines(): MutableVerbTimeline[] {
            return liveVerbTimelines;
        },

        setVerbTimelinePreview(p: { verb: Expression; timeMs: number } | null): void {
            sVerbTimelinePreview = p;
        },

        setVerbPreviewBaseVa(v: number, a: number): void {
            if (typeof v === "number") sVerbPreviewBaseVa.v = Math.max(-1, Math.min(1, v));
            if (typeof a === "number") sVerbPreviewBaseVa.a = Math.max(0, Math.min(1, a));
        },

        verbPreviewBaseVa(): { v: number; a: number } {
            return { ...sVerbPreviewBaseVa };
        },

        setBlendVerbPreview(verb: Expression | null): void {
            if (verb !== null && !expressionUsesVerbTimeline(verb)) return;
            if (verb === sBlendVerbPreview) return;
            sBlendVerbPreview = verb;
            sBlendVerbEnteredMs = now();
            // Do not resetVerbTransition() — tickBlend passes the new target to
            // sampleEffectiveVerb each frame; it snapshots in-flight output and
            // cross-fades over kVerbTransitionDurMs (matches firmware VerbTimeline).
        },

        blendVerbPreview(): Expression | null {
            return sBlendVerbPreview;
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
            if (typeof partial.blinkAmt === "number") sStaticOverride.blinkAmt = partial.blinkAmt;
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

        liveRenderMod(): { blinkAmt: number; gdx: number; gdy: number } {
            return {
                blinkAmt: sLiveBlinkAmt,
                gdx: sLiveGdx,
                gdy: sLiveGdy,
            };
        },

        armOffsetDeg(): number {
            return sCurrentArmDeg;
        },
    };
}
