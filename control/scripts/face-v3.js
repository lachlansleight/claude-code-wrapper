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

    // Match firmware seam fill for rotated spans to reduce 1px cracks.
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

  function localToScreen(lx, ly, cx, cy, cosA, sinA) {
    return [
      cx + Math.round(lx * cosA - ly * sinA),
      cy + Math.round(lx * sinA + ly * cosA),
    ];
  }

  // Trace an elliptical arc as a polyline, then expand outward by 1px and
  // re-trace, repeating `thick` times so the resulting band has uniform
  // thickness perpendicular to the arc — including at corners where the
  // tangent goes vertical (column-major strokes can't do that because
  // their pixels run parallel to the tangent there).
  function drawEdgeStroke(s, cx, cy, halfw, apex, corner, blinkScale, thick,
                          outwardSign, waveAmp, waveFreq, wavePhase, cosA, sinA, color) {
    if (halfw < 1 || thick < 1) return;
    for (let k = 0; k < thick; k++) {
      const rxk = halfw + k;
      const apexK = apex + outwardSign * k;
      let prevPx = 0, prevPy = 0;
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

  function drawMouth(s, p, cx, cy, nowMs, cosA, sinA, fg) {
    const halfw = p.mouth_rx | 0;
    if (halfw < 1) return;
    const wavePhase = wavePhaseRad(p.mouth_wave_speed, nowMs);
    // Slider value / 50 → ~1 full cycle across the shape at value=50.
    const waveFreq = p.mouth_wave_freq * 0.02;
    const waveAmp = p.mouth_wave_amp;
    const minThick = p.mouth_thick;

    for (let lx = -halfw; lx <= halfw; lx++) {
      const n = lx / halfw;
      let yt = curveAt(p.mouth_top_apex, p.mouth_top_corner, n);
      let yb = curveAt(p.mouth_bot_apex, p.mouth_bot_corner, n);
      if (waveAmp !== 0) {
        const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
        yt += w; yb += w;
      }
      if (yb < yt) { const tmp = yt; yt = yb; yb = tmp; }
      if ((yb - yt) < minThick) {
        const mid = 0.5 * (yt + yb);
        yt = mid - 0.5 * minThick;
        yb = mid + 0.5 * minThick;
      }
      paintLocalSpan(s, cx, cy, lx, yt, yb, cosA, sinA, fg);
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
      // Mirror firmware: render smooth circular pupil first.
      s.fillSmoothCircle(px, py, Math.round(pupilR), fg, bg);
    }

    // --- Interior fill (column-major over the inner envelope) ---
    for (let lx = -halfw; lx <= halfw; lx++) {
      const n = lx / halfw;
      let yt = curveAt(p.eye_top_apex, p.eye_top_corner, n) * blinkScale;
      let yb = curveAt(p.eye_bot_apex, p.eye_bot_corner, n) * blinkScale;
      if (waveAmp !== 0) {
        const w = waveAmp * Math.sin(2 * Math.PI * waveFreq * n + wavePhase);
        yt += w; yb += w;
      }
      if (yb < yt) { const tmp = yt; yt = yb; yb = tmp; }
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

    // Clip pupil side overhang beyond eye width with bg rectangular bands.
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

    // --- Outward strokes: concentric arc layers ---
    const thick = p.eye_thick > 0 ? p.eye_thick : 1;
    drawEdgeStroke(s, cx, cy, halfw, p.eye_top_apex, p.eye_top_corner,
                   blinkScale, thick, -1,
                   waveAmp, waveFreq, wavePhase, cosA, sinA, fg);
    drawEdgeStroke(s, cx, cy, halfw, p.eye_bot_apex, p.eye_bot_corner,
                   blinkScale, thick, +1,
                   waveAmp, waveFreq, wavePhase, cosA, sinA, fg);
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
