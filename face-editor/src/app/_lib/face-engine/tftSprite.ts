/**
 * Browser TFT_eSPI / TFT_eSprite subset for the face renderer.
 * Ported from control/scripts/tft.js
 */

export function rgb565ToCss(c: number): string {
  const r = (c >> 11) & 0x1f;
  const g = (c >> 5) & 0x3f;
  const b = c & 0x1f;
  const r8 = (r << 3) | (r >> 2);
  const g8 = (g << 2) | (g >> 4);
  const b8 = (b << 3) | (b >> 2);
  return `rgb(${r8}, ${g8}, ${b8})`;
}

export function rgb888To565(r: number, g: number, b: number): number {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3);
}

export class TFTSprite {
  readonly width: number;
  readonly height: number;
  readonly canvas: HTMLCanvasElement;
  readonly ctx: CanvasRenderingContext2D;

  constructor(width: number, height: number) {
    this.width = width;
    this.height = height;
    this.canvas = document.createElement("canvas");
    this.canvas.width = width;
    this.canvas.height = height;
    const ctx = this.canvas.getContext("2d");
    if (!ctx) throw new Error("TFTSprite: 2d context unavailable");
    this.ctx = ctx;
    this.ctx.imageSmoothingEnabled = true;
  }

  fillSprite(color: number): void {
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.fillRect(0, 0, this.width, this.height);
  }

  fillScreen(color: number): void {
    this.fillSprite(color);
  }

  fillRect(x: number, y: number, w: number, h: number, color: number): void {
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.fillRect(x | 0, y | 0, w | 0, h | 0);
  }

  drawLine(
    x0: number,
    y0: number,
    x1: number,
    y1: number,
    color: number,
  ): void {
    this.ctx.save();
    this.ctx.strokeStyle = rgb565ToCss(color);
    this.ctx.lineWidth = 1;
    this.ctx.lineCap = "butt";
    this.ctx.beginPath();
    this.ctx.moveTo((x0 | 0) + 0.5, (y0 | 0) + 0.5);
    this.ctx.lineTo((x1 | 0) + 0.5, (y1 | 0) + 0.5);
    this.ctx.stroke();
    this.ctx.restore();
  }

  fillEllipse(
    cx: number,
    cy: number,
    rx: number,
    ry: number,
    color: number,
  ): void {
    if (rx < 1 || ry < 1) return;
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.beginPath();
    this.ctx.ellipse(cx | 0, cy | 0, rx | 0, ry | 0, 0, 0, Math.PI * 2);
    this.ctx.fill();
  }

  drawEllipse(
    cx: number,
    cy: number,
    rx: number,
    ry: number,
    color: number,
  ): void {
    if (rx < 1 || ry < 1) return;
    this.ctx.save();
    this.ctx.strokeStyle = rgb565ToCss(color);
    this.ctx.lineWidth = 1;
    this.ctx.beginPath();
    this.ctx.ellipse(cx | 0, cy | 0, rx | 0, ry | 0, 0, 0, Math.PI * 2);
    this.ctx.stroke();
    this.ctx.restore();
  }

  fillCircle(cx: number, cy: number, r: number, color: number): void {
    this.fillEllipse(cx, cy, r, r, color);
  }

  drawCircle(cx: number, cy: number, r: number, color: number): void {
    this.drawEllipse(cx, cy, r, r, color);
  }

  fillSmoothCircle(
    cx: number,
    cy: number,
    r: number,
    color: number,
    _bgColor: number,
  ): void {
    this.ctx.save();
    this.ctx.fillStyle = rgb565ToCss(color);
    this.ctx.beginPath();
    this.ctx.arc(cx, cy, r, 0, Math.PI * 2);
    this.ctx.fill();
    this.ctx.restore();
  }

  pushTo(targetCtx: CanvasRenderingContext2D, dx = 0, dy = 0): void {
    targetCtx.drawImage(this.canvas, dx, dy);
  }
}

export const tft = {
  Sprite: TFTSprite,
  color565: rgb888To565,
  rgb565ToCss,
} as const;

export type TftApi = typeof tft;
