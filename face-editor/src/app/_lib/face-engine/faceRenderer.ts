/**
 * Port of robot_v3/src/face/FaceRenderer.cpp — keep math 1:1 with firmware.
 * Ported from control/scripts/face-v3.js
 */

import type { FaceParams } from "./faceParams";
import type { RobotSettings } from "./robotSettings";
import type { TFTSprite } from "./tftSprite";
import type { TftApi } from "./tftSprite";

export interface FaceRendererApi {
    renderScene(
        s: TFTSprite,
        p: FaceParams,
        blinkAmt: number,
        gdx: number,
        gdy: number,
        nowMs: number
    ): void;
    smoothstep01(t: number): number;
    clamp01(t: number): number;
    geometry: {
        kCx: number;
        kCy: number;
        kEyeY: number;
        kEyeLX: number;
        kEyeRX: number;
        kMouthY: number;
        kPivotY: number;
    };
}

export function createFaceRenderer(deps: {
    settings: RobotSettings;
    tft: TftApi;
}): FaceRendererApi {
    const { settings, tft } = deps;

    const kCx = 120;
    const kCy = 120;
    const kEyeY = 95;
    const kEyeLX = 85;
    const kEyeRX = 155;
    const kMouthY = 165;
    const kPivotY = 130;

    const kDisplayHalf = 120;
    const kMoodRingInsetPx = 15;
    const kMoodRingThickPx = 10;

    function clamp01(t: number): number {
        return t < 0 ? 0 : t > 1 ? 1 : t;
    }

    function smoothstep01(t: number): number {
        t = clamp01(t);
        return t * t * (3 - 2 * t);
    }

    function fg565(): number {
        const [r, g, b] = settings.rgb("foreground");
        return tft.color565(r, g, b);
    }

    function bg565(): number {
        const [r, g, b] = settings.rgb("background");
        return tft.color565(r, g, b);
    }

    function curveAt(apex: number, corner: number, n: number): number {
        const r = Math.sqrt(Math.max(0, 1 - n * n));
        return corner + (apex - corner) * r;
    }

    const ARC_CURL_FLOOR_K = 20;

    function arcParams(openAmt: number, arcAmtScaled: number) {
        const O = openAmt;
        const A = arcAmtScaled / 100;
        const S = O > ARC_CURL_FLOOR_K ? O : ARC_CURL_FLOOR_K;
        let topApex: number;
        let botApex: number;
        let corner: number;
        if (A >= 0) {
            if (A <= 1) {
                topApex = -O;
                botApex = +O;
                corner = -S * A;
            } else {
                topApex = -O + (A - 1) * S;
                botApex = +O;
                corner = -S;
            }
        } else {
            const a = -A;
            if (a <= 1) {
                topApex = -O;
                botApex = +O;
                corner = +S * a;
            } else {
                topApex = -O;
                botApex = +O - (a - 1) * S;
                corner = +S;
            }
        }
        return { topApex, botApex, corner };
    }

    function wavePhaseRad(speedDegPerSec: number, nowMs: number): number {
        return speedDegPerSec * nowMs * (Math.PI / 180000);
    }

    function paintLocalSpan(
        s: TFTSprite,
        cx: number,
        cy: number,
        fx: number,
        ly0: number,
        ly1: number,
        cosA: number,
        sinA: number,
        color: number
    ): void {
        const ax = fx * cosA;
        const ay = fx * sinA;
        const x0 = cx + Math.round(ax - ly0 * sinA);
        const y0 = cy + Math.round(ay + ly0 * cosA);
        const x1 = cx + Math.round(ax - ly1 * sinA);
        const y1 = cy + Math.round(ay + ly1 * cosA);
        s.drawLine(x0, y0, x1, y1, color);

        const rotMix = Math.abs(sinA * cosA);
        if (rotMix > 0.08) {
            const dx = x1 - x0;
            const dy = y1 - y0;
            if (Math.abs(dx) >= Math.abs(dy)) {
                s.drawLine(x0, y0 + 1, x1, y1 + 1, color);
            } else {
                s.drawLine(x0 + 1, y0, x1 + 1, y1, color);
            }
        }
    }

    function localToScreen(
        lx: number,
        ly: number,
        cx: number,
        cy: number,
        cosA: number,
        sinA: number
    ): [number, number] {
        return [cx + Math.round(lx * cosA - ly * sinA), cy + Math.round(lx * sinA + ly * cosA)];
    }

    function drawEdgeStroke(
        s: TFTSprite,
        cx: number,
        cy: number,
        halfw: number,
        apex: number,
        corner: number,
        blinkScale: number,
        thick: number,
        outwardSign: number,
        waveAmp: number,
        waveFreq: number,
        wavePhase: number,
        cosA: number,
        sinA: number,
        color: number
    ): void {
        if (halfw < 1 || thick < 1) return;
        for (let k = 0; k < thick; k++) {
            const rxk = halfw + k;
            const apexK = apex + outwardSign * k;
            let prevPx = 0;
            let prevPy = 0;
            let havePrev = false;
            for (let lx = -rxk; lx <= rxk; lx++) {
                const n = lx / rxk;
                const r = Math.sqrt(Math.max(0, 1 - n * n));
                let ly = (corner + (apexK - corner) * r) * blinkScale;
                if (waveAmp !== 0) {
                    ly += waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
                }
                const px = cx + Math.round(lx * cosA - ly * sinA);
                const py = cy + Math.round(lx * sinA + ly * cosA);
                if (havePrev) s.drawLine(prevPx, prevPy, px, py, color);
                prevPx = px;
                prevPy = py;
                havePrev = true;
            }
        }
    }

    function drawMouth(
        s: TFTSprite,
        p: FaceParams,
        cx: number,
        cy: number,
        nowMs: number,
        cosA: number,
        sinA: number,
        fg: number
    ): void {
        const halfw = p.mouth_rx | 0;
        if (halfw < 1) return;
        const wavePhase = wavePhaseRad(p.mouth_wave_speed, nowMs);
        const waveFreq = p.mouth_wave_freq * 0.02;
        const waveAmp = p.mouth_wave_amp;
        const minThick = p.mouth_thick;

        const arc = arcParams(p.mouth_open_amt, p.mouth_arc_amt);

        for (let lx = -halfw; lx <= halfw; lx++) {
            const n = lx / halfw;
            let yt = curveAt(arc.topApex, arc.corner, n);
            let yb = curveAt(arc.botApex, arc.corner, n);
            if (waveAmp !== 0) {
                const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
                yt += w;
                yb += w;
            }
            if (yb < yt) {
                const tmp = yt;
                yt = yb;
                yb = tmp;
            }
            if (yb - yt < minThick) {
                const mid = 0.5 * (yt + yb);
                yt = mid - 0.5 * minThick;
                yb = mid + 0.5 * minThick;
            }
            paintLocalSpan(s, cx, cy, lx, yt, yb, cosA, sinA, fg);
        }
    }

    function drawEye(
        s: TFTSprite,
        p: FaceParams,
        cx: number,
        cy: number,
        blinkAmt: number,
        gdx: number,
        gdy: number,
        nowMs: number,
        cosA: number,
        sinA: number,
        fg: number,
        bg: number
    ): void {
        const halfw = p.eye_rx | 0;
        if (halfw < 1) return;

        const blink = clamp01(blinkAmt);
        const blinkScale = 1 - blink;

        const wavePhase = wavePhaseRad(p.eye_wave_speed, nowMs);
        const waveFreq = p.eye_wave_freq * 0.02;
        const waveAmp = p.eye_wave_amp;

        const pupilLx = p.pupil_dx + gdx;
        const pupilLy = p.pupil_dy + gdy;
        const pupilR = p.pupil_r;
        const pupilR2 = pupilR * pupilR;
        const maskPupilR = pupilR + 2;
        const maskPupilR2 = maskPupilR * maskPupilR;
        const drawPupil = pupilR > 0 && blink < 0.6;
        const pupilMinX = Math.floor(pupilLx - maskPupilR) - 1;
        const pupilMaxX = Math.ceil(pupilLx + maskPupilR) + 1;

        if (drawPupil) {
            const [px, py] = localToScreen(pupilLx, pupilLy, cx, cy, cosA, sinA);
            s.fillSmoothCircle(px, py, Math.round(pupilR), fg, bg);
        }

        const arc = arcParams(p.eye_open_amt, p.eye_arc_amt);

        for (let lx = -halfw; lx <= halfw; lx++) {
            const n = lx / halfw;
            let yt = curveAt(arc.topApex, arc.corner, n) * blinkScale;
            let yb = curveAt(arc.botApex, arc.corner, n) * blinkScale;
            if (waveAmp !== 0) {
                const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
                yt += w;
                yb += w;
            }
            if (yb < yt) {
                const tmp = yt;
                yt = yb;
                yb = tmp;
            }
            const clipTopBound = yt;
            const clipBotBound = yb > yt ? yb : yt;

            if (drawPupil && lx >= pupilMinX && lx <= pupilMaxX) {
                const dx = lx - pupilLx;
                if (dx * dx <= maskPupilR2) {
                    const dyMag = Math.sqrt(maskPupilR2 - dx * dx);
                    const pupilTop = pupilLy - dyMag;
                    const pupilBot = pupilLy + dyMag;

                    if (pupilTop < clipTopBound) {
                        const maskBot = pupilBot < clipTopBound ? pupilBot : clipTopBound;
                        if (maskBot > pupilTop) {
                            paintLocalSpan(s, cx, cy, lx, pupilTop, maskBot, cosA, sinA, bg);
                        }
                    }
                    if (pupilBot > clipBotBound) {
                        const maskTop = pupilTop > clipBotBound ? pupilTop : clipBotBound;
                        if (pupilBot > maskTop) {
                            paintLocalSpan(s, cx, cy, lx, maskTop, pupilBot, cosA, sinA, bg);
                        }
                    }
                }
            }
        }

        if (drawPupil) {
            const sideTop = pupilLy - maskPupilR;
            const sideBot = pupilLy + maskPupilR;

            if (pupilMinX < -halfw) {
                const leftEnd = Math.min(pupilMaxX, -halfw - 1);
                for (let lx = pupilMinX; lx <= leftEnd; lx++) {
                    paintLocalSpan(s, cx, cy, lx, sideTop, sideBot, cosA, sinA, bg);
                }
            }
            if (pupilMaxX > halfw) {
                const rightStart = Math.max(pupilMinX, halfw + 1);
                for (let lx = rightStart; lx <= pupilMaxX; lx++) {
                    paintLocalSpan(s, cx, cy, lx, sideTop, sideBot, cosA, sinA, bg);
                }
            }
        }

        const thick = p.eye_thick > 0 ? p.eye_thick : 1;
        drawEdgeStroke(
            s,
            cx,
            cy,
            halfw,
            arc.topApex,
            arc.corner,
            blinkScale,
            thick,
            -1,
            waveAmp,
            waveFreq,
            wavePhase,
            cosA,
            sinA,
            fg
        );
        drawEdgeStroke(
            s,
            cx,
            cy,
            halfw,
            arc.botApex,
            arc.corner,
            blinkScale,
            thick,
            +1,
            waveAmp,
            waveFreq,
            wavePhase,
            cosA,
            sinA,
            fg
        );
    }

    function drawFace(
        s: TFTSprite,
        p: FaceParams,
        blinkAmt: number,
        gdx: number,
        gdy: number,
        nowMs: number
    ): void {
        const fg = fg565();
        const bg = bg565();
        const angleRad = (p.face_rot * Math.PI) / 180;
        const cosA = Math.cos(angleRad);
        const sinA = Math.sin(angleRad);

        const shorten = (Math.abs(p.face_y) / 2) | 0;
        const compress = (fy: number) => {
            const dy = fy - kPivotY;
            if (dy > 0) {
                const nd = dy - shorten;
                return kPivotY + (nd > 0 ? nd : 0);
            }
            if (dy < 0) {
                const nd = -dy - shorten;
                return kPivotY - (nd > 0 ? nd : 0);
            }
            return fy;
        };
        const rotated = (fx: number, fy: number): [number, number] => {
            const dx = fx - kCx;
            const dy = fy - kPivotY;
            return [
                kCx + ((dx * cosA - dy * sinA) | 0),
                kPivotY + ((dx * sinA + dy * cosA) | 0) + p.face_y,
            ];
        };

        const [lex, ley] = rotated(kEyeLX, compress(kEyeY + p.eye_dy));
        const [rex, rey] = rotated(kEyeRX, compress(kEyeY + p.eye_dy));
        const [mx, my] = rotated(kCx, compress(kMouthY + p.mouth_dy));

        drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg, bg);
        drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg, bg);
        drawMouth(s, p, mx, my, nowMs, cosA, sinA, fg);
    }

    function drawMoodRing(s: TFTSprite, ringR: number, ringG: number, ringB: number): void {
        let r = Number(ringR) | 0;
        let g = Number(ringG) | 0;
        let b = Number(ringB) | 0;
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        if (r === 0 && g === 0 && b === 0) return;

        const outerR = kDisplayHalf - kMoodRingInsetPx;
        const innerR = outerR - kMoodRingThickPx;
        if (innerR < 1 || outerR <= innerR) return;

        const color565 = tft.color565(r, g, b);
        for (let rad = innerR + 1; rad <= outerR; rad++) {
            s.drawCircle(kCx, kCy, rad, color565);
        }
    }

    function renderScene(
        s: TFTSprite,
        p: FaceParams,
        blinkAmt: number,
        gdx: number,
        gdy: number,
        nowMs: number
    ): void {
        s.fillSprite(bg565());
        drawFace(s, p, blinkAmt, gdx, gdy, nowMs);
        drawMoodRing(s, p.ring_r, p.ring_g, p.ring_b);
    }

    return {
        renderScene,
        smoothstep01,
        clamp01,
        geometry: { kCx, kCy, kEyeY, kEyeLX, kEyeRX, kMouthY, kPivotY },
    };
}
