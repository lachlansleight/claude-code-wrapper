// Browser shim that mimics the slice of TFT_eSPI / TFT_eSprite our face
// renderer uses. The aim is pixel-similar output to the on-device sprite,
// not bit-exact — the GC9A01 panel is also doing some gamma/colour shifting
// the canvas can't replicate.
//
// Coordinate system, colour format (RGB565), and method names match the
// firmware so face/effects code can be pasted across with minimal edits.
//
// SUBSET v1 — covers FaceRenderer.cpp. Calls used elsewhere in the firmware
// (drawString, pushImage, drawCircle, drawSmoothLine, sprite-to-sprite
// blits, fonts) intentionally throw so missing coverage is loud rather
// than silent. Add as needed.

(function () {
  // RGB565 → CSS rgb()
  function rgb565ToCss(c) {
    const r = (c >> 11) & 0x1f;
    const g = (c >> 5) & 0x3f;
    const b = c & 0x1f;
    // expand to 8 bits the way TFT_eSPI does (replicate high bits)
    const r8 = (r << 3) | (r >> 2);
    const g8 = (g << 2) | (g >> 4);
    const b8 = (b << 3) | (b >> 2);
    return `rgb(${r8}, ${g8}, ${b8})`;
  }

  function rgb888To565(r, g, b) {
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3);
  }

  class TFTSprite {
    constructor(width, height) {
      this.width = width;
      this.height = height;
      this.canvas = document.createElement("canvas");
      this.canvas.width = width;
      this.canvas.height = height;
      this.ctx = this.canvas.getContext("2d");
      // Match TFT pixel feel — disable smoothing for nearest-neighbor pushes
      // when we draw to the real canvas. Internal AA on shapes stays default.
      this.ctx.imageSmoothingEnabled = true;
    }

    // ---- TFT_eSPI subset ----------------------------------------------------

    fillSprite(color) {
      this.ctx.fillStyle = rgb565ToCss(color);
      this.ctx.fillRect(0, 0, this.width, this.height);
    }

    fillScreen(color) {
      this.fillSprite(color);
    }

    fillRect(x, y, w, h, color) {
      this.ctx.fillStyle = rgb565ToCss(color);
      this.ctx.fillRect(x | 0, y | 0, w | 0, h | 0);
    }

    drawLine(x0, y0, x1, y1, color) {
      // Mimic TFT_eSPI's 1px line. Half-pixel offset + butt caps so
      // single-pixel rows render as expected.
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

    drawWideLine(ax, ay, bx, by, wd, color, bgColor) {
      this.drawWedgeLine(ax, ay, bx, by, wd, wd, color, bgColor);
    }

    drawWedgeLine(ax, ay, bx, by, aw, bw, color, _bgColor) {
      const dx = bx - ax;
      const dy = by - ay;
      const len = Math.hypot(dx, dy);
      const ra = Math.max(0, aw * 0.5);
      const rb = Math.max(0, bw * 0.5);

      this.ctx.save();
      this.ctx.fillStyle = rgb565ToCss(color);

      if (len < 1e-6) {
        const r = Math.max(ra, rb);
        if (r > 0) {
          this.ctx.beginPath();
          this.ctx.arc(ax, ay, r, 0, Math.PI * 2);
          this.ctx.fill();
        }
        this.ctx.restore();
        return;
      }

      const nx = -dy / len;
      const ny = dx / len;

      // Fill the wedge body as a convex quad.
      this.ctx.beginPath();
      this.ctx.moveTo(ax + nx * ra, ay + ny * ra);
      this.ctx.lineTo(bx + nx * rb, by + ny * rb);
      this.ctx.lineTo(bx - nx * rb, by - ny * rb);
      this.ctx.lineTo(ax - nx * ra, ay - ny * ra);
      this.ctx.closePath();
      this.ctx.fill();

      // Rounded caps like TFT_eSPI.
      if (ra > 0) {
        this.ctx.beginPath();
        this.ctx.arc(ax, ay, ra, 0, Math.PI * 2);
        this.ctx.fill();
      }
      if (rb > 0) {
        this.ctx.beginPath();
        this.ctx.arc(bx, by, rb, 0, Math.PI * 2);
        this.ctx.fill();
      }

      this.ctx.restore();
    }

    fillEllipse(cx, cy, rx, ry, color) {
      if (rx < 1 || ry < 1) return;
      this.ctx.fillStyle = rgb565ToCss(color);
      this.ctx.beginPath();
      this.ctx.ellipse(cx | 0, cy | 0, rx | 0, ry | 0, 0, 0, Math.PI * 2);
      this.ctx.fill();
    }

    drawEllipse(cx, cy, rx, ry, color) {
      if (rx < 1 || ry < 1) return;
      this.ctx.save();
      this.ctx.strokeStyle = rgb565ToCss(color);
      this.ctx.lineWidth = 1;
      this.ctx.beginPath();
      this.ctx.ellipse(cx | 0, cy | 0, rx | 0, ry | 0, 0, 0, Math.PI * 2);
      this.ctx.stroke();
      this.ctx.restore();
    }

    fillCircle(cx, cy, r, color) {
      this.fillEllipse(cx, cy, r, r, color);
    }

    drawCircle(cx, cy, r, color) {
      this.drawEllipse(cx, cy, r, r, color);
    }

    // Anti-aliased filled circle. TFT_eSPI's version blends the edge against
    // bgColor — canvas already AAs against whatever's there, but we still
    // accept bgColor for signature compatibility.
    fillSmoothCircle(cx, cy, r, color, _bgColor) {
      this.ctx.save();
      this.ctx.fillStyle = rgb565ToCss(color);
      this.ctx.beginPath();
      this.ctx.arc(cx, cy, r, 0, Math.PI * 2);
      this.ctx.fill();
      this.ctx.restore();
    }

    // Convenience for callers that have an HTMLCanvasElement to flush to.
    pushTo(targetCtx, dx = 0, dy = 0) {
      targetCtx.drawImage(this.canvas, dx, dy);
    }
  }

  // Static colour helpers, mirroring the firmware globals.
  const TFT = {
    Sprite: TFTSprite,
    color565: rgb888To565,
    rgb565ToCss,
  };

  window.TFT = TFT;
})();
