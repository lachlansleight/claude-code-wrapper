import type { FrameController } from "../face-engine/frameController";
import type { EmotionBlendApi } from "../face-engine/emotionBlend";
import { EMOTION_TRIANGULATION } from "../face-engine/emotionTriangulation";
import { vaToCanvas } from "./blendCanvasMath";
import { EMOTION_COLOR } from "./simulatorLayout";

export function drawBlendDiagram(
  ctx: CanvasRenderingContext2D,
  fc: FrameController,
  blendApi: EmotionBlendApi,
  w: number,
  h: number,
): void {
  ctx.fillStyle = "#0b0d12";
  ctx.fillRect(0, 0, w, h);

  const data = EMOTION_TRIANGULATION;
  if (!data) {
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
  const tri = blendApi.ready() ? blendApi.findTriangle(va.v, va.a) : null;
  if (tri) {
    const [i0, i1, i2] = tri.indices;
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

  for (const an of data.anchors) {
    const [x, y] = vaToCanvas(an.v, an.a, w, h);
    ctx.fillStyle = EMOTION_COLOR[an.emotion] || "#e6e8ee";
    ctx.beginPath();
    ctx.arc(x, y, 3.5, 0, Math.PI * 2);
    ctx.fill();
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
  ctx.fillText("v=-1", 4, h - 4);
  ctx.fillText("v=+1", w - 32, h - 4);
  ctx.fillText("a=1", w - 32, 12);
  ctx.fillText("a=0", w - 32, h - 16);
}

export function computeBlendMetaHtml(
  fc: FrameController,
  blendApi: EmotionBlendApi,
): string {
  const va = fc.blendVA();
  const tri = blendApi.ready() ? blendApi.findTriangle(va.v, va.a) : null;
  let armPlain = "";
  let armHtml = "";
  if (blendApi.ready()) {
    const m = blendApi.blendedEmotionArmMotion(va.v, va.a);
    if (m) {
      armPlain =
        ` · arm blend: [${m.min_offset_deg}°, ${m.max_offset_deg}°] ` +
        `P=${m.waggle_period_s.toFixed(2)}s I=${m.waggle_interval_s.toFixed(2)}s`;
      armHtml =
        ` &nbsp;·&nbsp; arm blend: [${m.min_offset_deg}°, ${m.max_offset_deg}°] ` +
        `P=${m.waggle_period_s.toFixed(2)}s I=${m.waggle_interval_s.toFixed(2)}s`;
    }
  }
  if (!tri) {
    return `triangle: — · weights: —${armPlain}`;
  }
  const t = EMOTION_TRIANGULATION;
  const [i0, i1, i2] = tri.indices;
  const [l0, l1, l2] = tri.weights;
  const e0 = t.anchors[i0]!.emotion;
  const e1 = t.anchors[i1]!.emotion;
  const e2 = t.anchors[i2]!.emotion;
  return (
    `triangle: <b>${e0}</b> · <b>${e1}</b> · <b>${e2}</b> ` +
    `&nbsp;·&nbsp; weights: ${l0.toFixed(2)} / ${l1.toFixed(2)} / ${l2.toFixed(2)}` +
    armHtml
  );
}
