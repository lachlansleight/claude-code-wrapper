/**
 * Port of `robot_v3/src/face/EffectsRenderer.cpp` — procedural read/write code streams.
 */

import { rgb888To565, type TFTSprite } from "./tftSprite";

function clamp01(t: number): number {
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

function mixBits(x: number): number {
    x = x >>> 0;
    x ^= x >>> 16;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x ^= x >>> 15;
    x = Math.imul(x, 0x846ca68b) >>> 0;
    x ^= x >>> 16;
    return x >>> 0;
}

function alphaScale8(c: number, a: number): number {
    if (a <= 0) return 0;
    if (a >= 1) return c;
    return (c * a) | 0;
}

function tokenRgb(tok: number): [number, number, number] {
    const cPick = tok % 6;
    let r = 120;
    let g = 160;
    let b = 230;
    if (cPick === 0) {
        r = 120;
        g = 220;
        b = 255;
    } else if (cPick === 1) {
        r = 180;
        g = 135;
        b = 255;
    } else if (cPick === 2) {
        r = 255;
        g = 190;
        b = 90;
    } else if (cPick === 3) {
        r = 130;
        g = 235;
        b = 160;
    } else if (cPick === 4) {
        r = 255;
        g = 120;
        b = 170;
    }
    return [r, g, b];
}

const kScreenW = 240;

function drawReadStreamEffect(s: TFTSprite, now: number, alpha: number): void {
    if (alpha <= 0.01) return;

    const vis = 0.5 * clamp01(alpha);
    const kTop = 14;
    const kBottom = 226;
    const xBandMin = 0;
    const xBandMax = (kScreenW / 2) | 0;
    const lineHeight = 2;
    const animateSpeed = 200.0;
    const kVirtualLinesPerParagraph = 14;

    const scrollPx = now * (animateSpeed / 1000.0);

    for (let y = kTop; y + lineHeight <= kBottom; y = (y + lineHeight) | 0) {
        const row = ((y - kTop) / lineHeight) | 0;
        const lineIdx = Math.floor(row + scrollPx / lineHeight + 1.0e6);
        const p0 = Math.floor(lineIdx / kVirtualLinesPerParagraph) * kVirtualLinesPerParagraph;
        const rel = lineIdx - p0;
        const ph = mixBits((p0 ^ 0xdeadbeef) >>> 0);
        const logicalSpan = 5 + (ph % 46);
        if ((ph + rel * 17) % logicalSpan === 0) continue;

        const indentSteps =
            (ph % 7) +
            (rel > kVirtualLinesPerParagraph / 2 ? ((ph >> 3) % 4) | 0 : 0);
        const indentPx = 4 + indentSteps * 4;

        let x = (xBandMin + indentPx) | 0;
        const xMax = xBandMax;
        const avail = (xMax - x) | 0;
        if (avail < 10) continue;

        const wLine = mixBits((lineIdx ^ ph ^ 0x51edc3ba) >>> 0);
        let lineEndX: number;
        if (wLine % 8 === 0) {
            const lo = x + ((avail * 62) / 100) | 0;
            const hi = xMax;
            const span = hi - lo;
            lineEndX = span < 8 ? xMax : (lo + 8 + ((wLine >> 5) % (span - 7))) | 0;
        } else {
            const pct = (16 + (wLine % 34)) | 0;
            lineEndX = (x + ((avail * pct) / 100)) | 0;
            const minEnd = (x + 12) | 0;
            const cap = (x + ((avail * 52) / 100)) | 0;
            if (lineEndX < minEnd) lineEndX = minEnd;
            if (lineEndX > cap) lineEndX = cap;
        }
        if (lineEndX > xMax) lineEndX = xMax;

        let tok = mixBits((lineIdx ^ (Math.imul(p0, 0x27d4eb2d) >>> 0)) >>> 0);
        while (x < lineEndX) {
            const runW = (2 + (tok % 3) * 2) | 0;
            const [r, g, b] = tokenRgb(tok);
            const tokenColor = rgb888To565(
                alphaScale8(r, vis),
                alphaScale8(g, vis),
                alphaScale8(b, vis)
            );
            if (x + runW > xBandMin && x < xBandMax) {
                const x0 = x < xBandMin ? xBandMin : x;
                const x1 = (x + runW) | 0;
                const clipR = x1 > xBandMax ? xBandMax : x1;
                const wClip = (clipR - x0) | 0;
                if (wClip > 0) s.fillRect(x0, y, wClip, lineHeight, tokenColor);
            }
            x = (x + runW) | 0;
            const gap = (1 + ((tok >> 8) % 3)) | 0;
            x = (x + gap) | 0;
            tok = mixBits((tok + 0x6d2b79f5 + x) >>> 0);
        }
    }
}

function drawWriteStreamEffect(s: TFTSprite, now: number, alpha: number): void {
    if (alpha <= 0.01) return;

    const vis = 0.5 * clamp01(alpha);
    const kTop = 14;
    const kCy = 120;
    const xBandMin = (kScreenW / 2) | 0;
    const xBandMax = kScreenW;
    const lineHeight = 4;
    const animateSpeed = 100.0;
    const kVirtualLinesPerParagraph = 18;

    const scrollPx = now * (animateSpeed / 1000.0);

    for (let y = kTop; y + lineHeight <= kCy; y = (y + lineHeight) | 0) {
        const row = ((y - kTop) / lineHeight) | 0;
        const lineIdx = Math.floor(row + scrollPx / lineHeight + 1.0e6);
        const p0 = Math.floor(lineIdx / kVirtualLinesPerParagraph) * kVirtualLinesPerParagraph;
        const rel = lineIdx - p0;
        const ph = mixBits((p0 ^ 0x5a5a5a5a) >>> 0);
        const logicalSpan = 6 + (ph % 40);
        if ((ph + rel * 17) % logicalSpan === 0) continue;

        const indentSteps =
            (ph % 7) +
            (rel > kVirtualLinesPerParagraph / 2 ? ((ph >> 3) % 4) | 0 : 0);
        const indentPx = 4 + indentSteps * 4;

        let x = (xBandMin + indentPx) | 0;
        const xMax = xBandMax;
        const avail = (xMax - x) | 0;
        if (avail < 10) continue;

        const wLine = mixBits((lineIdx ^ ph ^ 0x51edc3ba) >>> 0);
        let lineEndX: number;
        if (wLine % 8 === 0) {
            const lo = x + ((avail * 62) / 100) | 0;
            const hi = xMax;
            const span = hi - lo;
            lineEndX = span < 8 ? xMax : (lo + 8 + ((wLine >> 5) % (span - 7))) | 0;
        } else {
            const pct = (16 + (wLine % 34)) | 0;
            lineEndX = (x + ((avail * pct) / 100)) | 0;
            const minEnd = (x + 12) | 0;
            const cap = (x + ((avail * 52) / 100)) | 0;
            if (lineEndX < minEnd) lineEndX = minEnd;
            if (lineEndX > cap) lineEndX = cap;
        }
        if (lineEndX > xMax) lineEndX = xMax;

        let tok = mixBits((lineIdx ^ (Math.imul(p0, 0x27d4eb2d) >>> 0)) >>> 0);
        while (x < lineEndX) {
            const runW = (2 + (tok % 3) * 2) | 0;
            const [r, g, b] = tokenRgb(tok);
            const tokenColor = rgb888To565(
                alphaScale8(r, vis),
                alphaScale8(g, vis),
                alphaScale8(b, vis)
            );
            if (x + runW > xBandMin && x < xBandMax) {
                const x0 = x < xBandMin ? xBandMin : x;
                const x1 = (x + runW) | 0;
                const clipR = x1 > xBandMax ? xBandMax : x1;
                const wClip = (clipR - x0) | 0;
                if (wClip > 0) s.fillRect(x0, y, wClip, lineHeight, tokenColor);
            }
            x = (x + runW) | 0;
            const gap = (1 + ((tok >> 8) % 3)) | 0;
            x = (x + gap) | 0;
            tok = mixBits((tok + 0x6d2b79f5 + x) >>> 0);
        }
    }
}

/** Paint read (left) and write (right) stream effects; call after background fill, before face. */
export function drawEffects(
    s: TFTSprite,
    nowMs: number,
    readAlpha: number,
    writeAlpha: number
): void {
    drawReadStreamEffect(s, nowMs, readAlpha);
    drawWriteStreamEffect(s, nowMs, writeAlpha);
}
