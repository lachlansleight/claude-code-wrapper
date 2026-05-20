import type { FrameController } from "../face-engine/frameController";
import type { EmotionBlendApi } from "../face-engine/emotionBlend";
import { FieldIndex } from "../face-engine/faceConfigTypes";
import type { EmotionTriangulationTable } from "../face-engine/types";
import { vaToCanvas } from "./blendCanvasMath";
import { EMOTION_COLOR } from "./simulatorLayout";

/** Stroke radius for anchor rings (px). */
export const BLEND_ANCHOR_RING_R = 6.5;
/** Hit-test radius around anchor centre (px). */
export const BLEND_ANCHOR_HIT_R = 14;

export function pickBlendAnchorIndexAtCanvas(
    px: number,
    py: number,
    tri: EmotionTriangulationTable,
    w: number,
    h: number
): number | null {
    const hitR2 = BLEND_ANCHOR_HIT_R * BLEND_ANCHOR_HIT_R;
    let best: number | null = null;
    let bestD = hitR2;
    for (let i = 0; i < tri.anchors.length; i++) {
        const an = tri.anchors[i]!;
        const [x, y] = vaToCanvas(an.v, an.a, w, h);
        const dx = px - x;
        const dy = py - y;
        const d2 = dx * dx + dy * dy;
        if (d2 <= hitR2 && d2 <= bestD) {
            best = i;
            bestD = d2;
        }
    }
    return best;
}

export function drawBlendDiagram(
    ctx: CanvasRenderingContext2D,
    fc: FrameController,
    blendApi: EmotionBlendApi,
    tri: EmotionTriangulationTable,
    w: number,
    h: number
): void {
    ctx.fillStyle = "#0b0d12";
    ctx.fillRect(0, 0, w, h);

    const data = tri;
    if (!data || !data.anchors?.length) {
        ctx.fillStyle = "#ff7b7b";
        ctx.font = "13px ui-monospace, monospace";
        ctx.fillText("emotion triangulation missing", 14, 22);
        return;
    }

    ctx.strokeStyle = "#1d222c";
    ctx.lineWidth = 1;
    const [zx] = vaToCanvas(0, 0, w, h);
    ctx.beginPath();
    ctx.moveTo(zx, 0);
    ctx.lineTo(zx, h);
    ctx.stroke();
    const [, zy] = vaToCanvas(0, 0.5, w, h);
    ctx.beginPath();
    ctx.moveTo(0, zy);
    ctx.lineTo(w, zy);
    ctx.stroke();

    ctx.strokeStyle = "#2a3140";
    ctx.lineWidth = 1;
    for (const [i0, i1, i2] of data.triangles) {
        const a0 = data.anchors[i0]!;
        const a1 = data.anchors[i1]!;
        const a2 = data.anchors[i2]!;
        const p0 = vaToCanvas(a0.v, a0.a, w, h);
        const p1 = vaToCanvas(a1.v, a1.a, w, h);
        const p2 = vaToCanvas(a2.v, a2.a, w, h);
        ctx.beginPath();
        ctx.moveTo(p0[0], p0[1]);
        ctx.lineTo(p1[0], p1[1]);
        ctx.lineTo(p2[0], p2[1]);
        ctx.closePath();
        ctx.stroke();
    }

    const va = fc.blendVA();
    const highlightTri = blendApi.ready() ? blendApi.findTriangle(va.v, va.a) : null;
    if (highlightTri) {
        const [i0, i1, i2] = highlightTri.indices;
        const a0 = data.anchors[i0]!;
        const a1 = data.anchors[i1]!;
        const a2 = data.anchors[i2]!;
        const p0 = vaToCanvas(a0.v, a0.a, w, h);
        const p1 = vaToCanvas(a1.v, a1.a, w, h);
        const p2 = vaToCanvas(a2.v, a2.a, w, h);
        ctx.fillStyle = "rgba(110, 168, 255, 0.18)";
        ctx.beginPath();
        ctx.moveTo(p0[0], p0[1]);
        ctx.lineTo(p1[0], p1[1]);
        ctx.lineTo(p2[0], p2[1]);
        ctx.closePath();
        ctx.fill();
    }

    const labelLift = BLEND_ANCHOR_RING_R + 8;
    const labelPadX = 4;
    for (const an of data.anchors) {
        const [x, y] = vaToCanvas(an.v, an.a, w, h);
        const col = EMOTION_COLOR[an.emotion] || "#e6e8ee";
        ctx.strokeStyle = col;
        ctx.lineWidth = 2.25;
        ctx.beginPath();
        ctx.arc(x, y, BLEND_ANCHOR_RING_R, 0, Math.PI * 2);
        ctx.stroke();
        ctx.fillStyle = col;
        ctx.font = "10px ui-monospace, monospace";
        const inBottomHalf = y > h * 0.5;
        const inRightHalf = x > w * 0.5;
        ctx.textAlign = inRightHalf ? "right" : "left";
        if (inBottomHalf) {
            ctx.textBaseline = "bottom";
            ctx.fillText(an.emotion, inRightHalf ? x - labelPadX : x + labelPadX, y - labelLift);
        } else {
            ctx.textBaseline = "top";
            ctx.fillText(an.emotion, inRightHalf ? x - labelPadX : x + labelPadX, y + labelLift);
        }
    }

    const [px, py] = vaToCanvas(va.v, va.a, w, h);
    ctx.fillStyle = "#fff";
    ctx.strokeStyle = "#0b0d12";
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(px, py, 5.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = "#8b93a7";
    ctx.font = "10px ui-monospace, monospace";
    ctx.textAlign = "left";
    ctx.textBaseline = "alphabetic";
    ctx.fillText("v=-1", 4, h - 4);
    ctx.fillText("v=+1", w - 32, h - 4);
    ctx.fillText("a=1", w - 32, 12);
    ctx.fillText("a=0", w - 32, h - 16);
}

export function computeBlendMetaHtml(
    fc: FrameController,
    blendApi: EmotionBlendApi,
    tri: EmotionTriangulationTable
): string {
    const va = fc.blendVA();
    const foundTri = blendApi.ready() ? blendApi.findTriangle(va.v, va.a) : null;
    let armPlain = "";
    let armHtml = "";
    if (blendApi.ready()) {
        const row = blendApi.blendedFaceParamsIndexed(va.v, va.a);
        if (row) {
            const lo = row[FieldIndex.ArmMinDeg]!.value;
            const hi = row[FieldIndex.ArmMaxDeg]!.value;
            const pMs = row[FieldIndex.ArmPeriodMs]!.value;
            const iMs = row[FieldIndex.ArmIntervalMs]!.value;
            armPlain = ` · arm blend: [${lo}°, ${hi}°] P=${pMs}ms I=${iMs}ms`;
            armHtml = ` &nbsp;·&nbsp; arm blend: [${lo}°, ${hi}°] P=${pMs}ms I=${iMs}ms`;
        }
    }
    if (!foundTri) {
        return `triangle: — · weights: —${armPlain}`;
    }
    const [i0, i1, i2] = foundTri.indices;
    const [l0, l1, l2] = foundTri.weights;
    const e0 = tri.anchors[i0]!.emotion;
    const e1 = tri.anchors[i1]!.emotion;
    const e2 = tri.anchors[i2]!.emotion;
    return (
        `triangle: <b>${e0}</b> · <b>${e1}</b> · <b>${e2}</b> ` +
        `&nbsp;·&nbsp; weights: ${l0.toFixed(2)} / ${l1.toFixed(2)} / ${l2.toFixed(2)}` +
        armHtml
    );
}
