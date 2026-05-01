// Port of robot_v2/FaceRenderer.cpp + Scene.cpp face path. Keep method
// names and math 1:1 with the firmware so changes can be ported either
// direction without translation overhead.

(function () {
  // Geometry constants — must match SceneTypes.h.
  const kCx = 120;
  const kCy = 120;
  const kEyeY = 95;
  const kEyeLX = 85;
  const kEyeRX = 155;
  const kMouthY = 165;
  const kPivotY = 130;

  function clamp01(t) {
    return t < 0 ? 0 : t > 1 ? 1 : t;
  }

  function smoothstep01(t) {
    t = clamp01(t);
    return t * t * (3 - 2 * t);
  }

  function kFg() {
    const [r, g, b] = window.RobotSettings.rgb("foreground");
    return TFT.color565(r, g, b);
  }
  function kBg() {
    const [r, g, b] = window.RobotSettings.rgb("background");
    return TFT.color565(r, g, b);
  }

  function abs(n) { return n < 0 ? -n : n; }

  function drawParabola(s, cx, cy, w, bend, thick, cosA, sinA) {
    if (w < 2) return;
    if (thick < 1) thick = 1;
    const halfw = (w / 2) | 0;
    for (let lx = -halfw; lx <= halfw; lx++) {
      const norm = lx / halfw;
      const ly = -bend * (1 - norm * norm);
      const rx = lx * cosA - ly * sinA;
      const ry = lx * sinA + ly * cosA;
      const px = cx + (rx | 0);
      const py = cy + (ry | 0);
      s.fillRect(px, py - ((thick / 2) | 0), 1, thick, kFg());
    }
  }

  function drawEye(s, p, cx, cy, blinkAmt, gdx, gdy, cosA, sinA) {
    let ry = p.eye_ry;
    if (blinkAmt > 0.01) {
      const k = clamp01(blinkAmt);
      ry = (ry * (1 - k) + 2 * k) | 0;
      if (ry < 2) ry = 2;
    }
    const rx = p.eye_rx;

    if (p.eye_curve !== 0 || ry < 5) {
      drawParabola(s, cx, cy, rx * 2, p.eye_curve, p.eye_stroke * 2, cosA, sinA);
      return;
    }

    s.fillEllipse(cx, cy, rx, ry, kFg());
    const irx = rx - p.eye_stroke;
    const iry = ry - p.eye_stroke;
    if (irx > 0 && iry > 0) {
      s.fillEllipse(cx, cy, irx, iry, kBg());
    }
    if (p.pupil_r > 0 && blinkAmt < 0.6) {
      const ldx = p.pupil_dx + gdx;
      const ldy = p.pupil_dy + gdy;
      const rdx = ldx * cosA - ldy * sinA;
      const rdy = ldx * sinA + ldy * cosA;

      const slackX = irx - Math.abs(rdx);
      const slackY = iry - Math.abs(rdy);
      let maxR = Math.min(slackX, slackY) | 0;
      if (maxR < 1) return;
      let effR = p.pupil_r;
      if (effR > maxR) effR = maxR;

      s.fillSmoothCircle(cx + (rdx | 0), cy + (rdy | 0), effR, kFg(), kBg());
    }
  }

  function drawHalfEllipse(s, cx, cy, rx, ry, cosA, sinA) {
    if (rx < 1 || ry < 1) return;
    for (let lx = -rx; lx <= rx; lx++) {
      const norm = lx / rx;
      const h = ry * Math.sqrt(Math.max(0, 1 - norm * norm));
      if (h < 0.5) continue;
      const top_rx = lx * cosA;
      const top_ry = lx * sinA;
      const bot_rx = lx * cosA - h * sinA;
      const bot_ry = lx * sinA + h * cosA;
      s.drawLine(
        cx + (top_rx | 0),
        cy + (top_ry | 0),
        cx + (bot_rx | 0),
        cy + (bot_ry | 0),
        kFg(),
      );
    }
  }

  function drawZigZagMouth(s, cx, cy, width, amp, thick, cosA, sinA) {
    if (width < 8) return;
    if (thick < 1) thick = 1;
    const half = (width / 2) | 0;
    const segments = 6;
    const step = width / segments;
    let lx0 = -half;
    let ly0 = 0;
    for (let i = 1; i <= segments; i++) {
      const lx1 = -half + step * i;
      const ly1 = i % 2 === 0 ? -amp : amp;
      const rx0 = lx0 * cosA - ly0 * sinA;
      const ry0 = lx0 * sinA + ly0 * cosA;
      const rx1 = lx1 * cosA - ly1 * sinA;
      const ry1 = lx1 * sinA + ly1 * cosA;
      const tHalf = (thick / 2) | 0;
      for (let o = -tHalf; o <= tHalf; o++) {
        s.drawLine(
          cx + (rx0 | 0),
          cy + (ry0 | 0) + o,
          cx + (rx1 | 0),
          cy + (ry1 | 0) + o,
          kFg(),
        );
      }
      lx0 = lx1;
      ly0 = ly1;
    }
  }

  function drawMouth(s, p, cx, cy, state, cosA, sinA) {
    if (state === "executing_long") {
      drawZigZagMouth(s, cx, cy, p.mouth_w * 2, 4, p.mouth_thick, cosA, sinA);
      return;
    }
    if (p.mouth_open_h > 0) {
      if (p.mouth_curve < 0) {
        drawHalfEllipse(s, cx, cy, (p.mouth_w / 2) | 0, p.mouth_open_h, cosA, sinA);
      } else {
        s.fillEllipse(cx, cy, (p.mouth_w / 2) | 0, p.mouth_open_h, kFg());
      }
      return;
    }
    drawParabola(s, cx, cy, p.mouth_w, p.mouth_curve, p.mouth_thick, cosA, sinA);
  }

  function drawFace(s, p, blinkAmt, gdx, gdy, state) {
    const angleRad = (p.face_rot * Math.PI) / 180;
    const cosA = Math.cos(angleRad);
    const sinA = Math.sin(angleRad);

    const shorten = (abs(p.face_y) / 2) | 0;
    const compress = (fy) => {
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

    const rotated = (fx, fy) => {
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

    drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, cosA, sinA);
    drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, cosA, sinA);
    drawMouth(s, p, mx, my, state, cosA, sinA);
  }

  // Equivalent of Scene.cpp::renderScene — face path only for now. Mood-ring,
  // activity-dots and effects come in a future pass.
  function renderScene(s, p, blinkAmt, gdx, gdy, state) {
    s.fillSprite(kBg());
    drawFace(s, p, blinkAmt, gdx, gdy, state);
  }

  window.RobotFace = {
    drawFace,
    renderScene,
    geometry: { kCx, kCy, kEyeY, kEyeLX, kEyeRX, kMouthY, kPivotY },
    smoothstep01,
    clamp01,
  };
})();
