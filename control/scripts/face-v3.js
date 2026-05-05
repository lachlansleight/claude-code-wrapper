// Port of robot_v3/src/face/FaceRenderer.cpp. Keep math 1:1 with the
// firmware so visual changes can be tuned here and pasted across.
//
// Renders into a TFT.Sprite (see tft.js). Uses RobotSettings for fg/bg.

(function () {
  // Geometry constants — must match SceneTypes.h.
  const kCx = 120;
  const kCy = 120;
  const kEyeY = 95;
  const kEyeLX = 85;
  const kEyeRX = 155;
  const kMouthY = 165;
  const kPivotY = 130;

  function clamp01(t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
  function smoothstep01(t) { t = clamp01(t); return t * t * (3 - 2 * t); }

  function fg565() {
    const [r, g, b] = window.RobotSettings.rgb("foreground");
    return TFT.color565(r, g, b);
  }
  function bg565() {
    const [r, g, b] = window.RobotSettings.rgb("background");
    return TFT.color565(r, g, b);
  }

  // Semicircular interp between apex (n=0) and corner (|n|=1):
  //   y(n) = corner + (apex - corner) * sqrt(1 - n^2)
  // Top + bottom edges with mirrored apexes about y=0 trace a perfect ellipse.
  function curveAt(apex, corner, n) {
    const r = Math.sqrt(Math.max(0, 1 - n * n));
    return corner + (apex - corner) * r;
  }

  function wavePhaseRad(speedDegPerSec, nowMs) {
    return speedDegPerSec * nowMs * (Math.PI / 180000);
  }

  // Paint one local-coord vertical span [ly0, ly1] as a single rotated line.
  function paintLocalSpan(s, cx, cy, fx, ly0, ly1, cosA, sinA, color) {
    const ax = fx * cosA;
    const ay = fx * sinA;
    const x0 = cx + Math.round(ax - ly0 * sinA);
    const y0 = cy + Math.round(ay + ly0 * cosA);
    const x1 = cx + Math.round(ax - ly1 * sinA);
    const y1 = cy + Math.round(ay + ly1 * cosA);
    s.drawLine(x0, y0, x1, y1, color);
  }

  function localToScreenPoint(cx, cy, lx, ly, cosA, sinA) {
    return [cx + lx * cosA - ly * sinA, cy + lx * sinA + ly * cosA];
  }

  function drawMouth(s, p, cx, cy, nowMs, cosA, sinA, fg) {
    const halfw = p.mouth_rx | 0;
    if (halfw < 1) return;
    const wavePhase = wavePhaseRad(p.mouth_wave_speed, nowMs);
    // Slider value / 50 → ~1 full cycle across the shape at value=50.
    const waveFreq = p.mouth_wave_freq * 0.02;
    const waveAmp = p.mouth_wave_amp;
    const mouthW = 2 * p.mouth_thick;
    let hasPrevTop = false, hasPrevBot = false;
    let prevTopX = 0, prevTopY = 0, prevBotX = 0, prevBotY = 0;

    for (let lx = -halfw; lx <= halfw; lx++) {
      const n = lx / halfw;
      let yt = curveAt(p.mouth_top_apex, p.mouth_top_corner, n);
      let yb = curveAt(p.mouth_bot_apex, p.mouth_bot_corner, n);
      if (waveAmp !== 0) {
        const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
        yt += w; yb += w;
      }
      if (yb < yt) { const tmp = yt; yt = yb; yb = tmp; }
      const [topX, topY] = localToScreenPoint(cx, cy, lx, yt, cosA, sinA);
      const [botX, botY] = localToScreenPoint(cx, cy, lx, yb, cosA, sinA);
      if (hasPrevTop) {
        s.drawWedgeLine(prevTopX, prevTopY, topX, topY, mouthW, mouthW, fg, fg);
      }
      if (hasPrevBot) {
        s.drawWedgeLine(prevBotX, prevBotY, botX, botY, mouthW, mouthW, fg, fg);
      }
      if (yb >= yt) paintLocalSpan(s, cx, cy, lx, yt, yb, cosA, sinA, fg);
      prevTopX = topX;
      prevTopY = topY;
      prevBotX = botX;
      prevBotY = botY;
      hasPrevTop = true;
      hasPrevBot = true;
    }
  }

  function drawEye(s, p, cx, cy, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg, bg) {
    const halfw = p.eye_rx | 0;
    if (halfw < 1) return;

    const blink = clamp01(blinkAmt);
    const blinkScale = 1 - blink;

    const wavePhase = wavePhaseRad(p.eye_wave_speed, nowMs);
    const waveFreq = p.eye_wave_freq * 0.02;
    const waveAmp = p.eye_wave_amp;
    const thickF = p.eye_thick > 0 ? p.eye_thick : 1;

    const pupilLx = p.pupil_dx + gdx;
    const pupilLy = p.pupil_dy + gdy;
    const pupilR = p.pupil_r;
    const pupilR2 = pupilR * pupilR;
    const drawPupil = pupilR > 0 && blink < 0.6;
    const strokeW = 2 * thickF; // centered wedge => outward half-width == thickF.
    let hasPrev = false;
    let prevTopX = 0, prevTopY = 0, prevBotX = 0, prevBotY = 0;

    for (let lx = -halfw; lx <= halfw; lx++) {
      const n = lx / halfw;
      let yt = curveAt(p.eye_top_apex, p.eye_top_corner, n) * blinkScale;
      let yb = curveAt(p.eye_bot_apex, p.eye_bot_corner, n) * blinkScale;
      if (waveAmp !== 0) {
        const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
        yt += w; yb += w;
      }
      if (yb < yt) { const tmp = yt; yt = yb; yb = tmp; }

      const [topX, topY] = localToScreenPoint(cx, cy, lx, yt, cosA, sinA);
      const [botX, botY] = localToScreenPoint(cx, cy, lx, yb, cosA, sinA);
      if (hasPrev) {
        s.drawWedgeLine(prevTopX, prevTopY, topX, topY, strokeW, strokeW, fg, bg);
        s.drawWedgeLine(prevBotX, prevBotY, botX, botY, strokeW, strokeW, fg, bg);
      }
      prevTopX = topX;
      prevTopY = topY;
      prevBotX = botX;
      prevBotY = botY;
      hasPrev = true;

      if (yb <= yt) continue;  // collapsed envelope: no interior to fill.

      const interiorTop = yt;
      const interiorBot = yb;
      if (drawPupil) {
        const dx = lx - pupilLx;
        if (dx * dx <= pupilR2) {
          const dyMag = Math.sqrt(pupilR2 - dx * dx);
          const pupilTop = pupilLy - dyMag;
          const pupilBot = pupilLy + dyMag;
          const clipTop = pupilTop > interiorTop ? pupilTop : interiorTop;
          const clipBot = pupilBot < interiorBot ? pupilBot : interiorBot;
          if (clipBot >= clipTop) {
            if (clipTop > interiorTop) paintLocalSpan(s, cx, cy, lx, interiorTop, clipTop, cosA, sinA, bg);
            paintLocalSpan(s, cx, cy, lx, clipTop, clipBot, cosA, sinA, fg);
            if (clipBot < interiorBot) paintLocalSpan(s, cx, cy, lx, clipBot, interiorBot, cosA, sinA, bg);
          } else {
            paintLocalSpan(s, cx, cy, lx, interiorTop, interiorBot, cosA, sinA, bg);
          }
        } else {
          paintLocalSpan(s, cx, cy, lx, interiorTop, interiorBot, cosA, sinA, bg);
        }
      } else {
        paintLocalSpan(s, cx, cy, lx, interiorTop, interiorBot, cosA, sinA, bg);
      }
    }
  }

  function drawFace(s, p, blinkAmt, gdx, gdy, nowMs) {
    const fg = fg565();
    const bg = bg565();
    const angleRad = (p.face_rot * Math.PI) / 180;
    const cosA = Math.cos(angleRad);
    const sinA = Math.sin(angleRad);

    const shorten = (Math.abs(p.face_y) / 2) | 0;
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

    drawEye(s, p, lex, ley, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg, bg);
    drawEye(s, p, rex, rey, blinkAmt, gdx, gdy, nowMs, cosA, sinA, fg, bg);
    drawMouth(s, p, mx, my, nowMs, cosA, sinA, fg);
  }

  function renderScene(s, p, blinkAmt, gdx, gdy, nowMs) {
    s.fillSprite(bg565());
    drawFace(s, p, blinkAmt, gdx, gdy, nowMs);
  }

  window.RobotFaceV3 = {
    drawFace,
    renderScene,
    geometry: { kCx, kCy, kEyeY, kEyeLX, kEyeRX, kMouthY, kPivotY },
    smoothstep01,
    clamp01,
  };
})();
