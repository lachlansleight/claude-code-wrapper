"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { createFrameController } from "../../_lib/face-engine/frameController";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { formatKBaseTargetsCpp } from "../../_lib/face-editor/cppRowFormat";
import { postRaw } from "../../_lib/face-editor/bridge";
import { OVERLAY_MAP } from "../../_lib/face-editor/simulatorLayout";
import { emotionBlendDraw } from "../../_lib/face-editor/simulatorBlendShared";
import { BlendPanel } from "./BlendPanel";
import { ExpressionPickers, handleExpressionBridge } from "./ExpressionPickers";
import { FaceStage } from "./FaceStage";
import { LiveParamsReadout } from "./LiveParamsReadout";
import { StaticModePanel } from "./StaticModePanel";

export function FaceSimulator() {
  const fc = useMemo(() => createFrameController(), []);
  const faceCanvasRef = useRef<HTMLCanvasElement>(null);

  const [currentExpr, setCurrentExpr] = useState(fc.currentExpression());
  const [blendOn, setBlendOn] = useState(false);
  const [staticOn, setStaticOn] = useState(false);
  const [autoSend, setAutoSend] = useState(false);
  const [overlayMs, setOverlayMs] = useState(1200);
  const [paramsText, setParamsText] = useState("");
  const [fps, setFps] = useState("—");
  const [armDeg, setArmDeg] = useState("—");
  const [copyOut, setCopyOut] = useState("/* tune sliders to populate */");
  const [staticPreset, setStaticPreset] = useState("Neutral");
  const [sliderSnap, setSliderSnap] = useState(() => ({
    params: { ...fc.baseTargetForExpression("Neutral") } as FaceParams,
    blinkAmt: 0,
    gdx: 0,
    gdy: 0,
  }));

  const blendSendDirty = useRef(false);

  useEffect(() => {
    const canvas = faceCanvasRef.current;
    if (!canvas) return;
    fc.start(canvas);
    fc.setBlendVA(0, 0.5);
    return () => fc.stop();
  }, [fc]);

  useEffect(() => {
    fc.onExpressionChange((n: string) => setCurrentExpr(n));
  }, [fc]);

  /** Mirror animated face (expression mode) into static sliders — verb timelines, bob, breath, etc. */
  useEffect(() => {
    if (staticOn || blendOn) return;
    let raf = 0;
    const step = () => {
      const m = fc.liveRenderMod();
      setSliderSnap({
        params: { ...fc.params() },
        blinkAmt: m.blinkAmt,
        gdx: m.gdx,
        gdy: m.gdy,
      });
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [fc, staticOn, blendOn]);

  useEffect(() => {
    const id = window.setInterval(() => {
      setArmDeg(`${fc.armOffsetDeg().toFixed(0)}°`);
    }, 100);
    return () => clearInterval(id);
  }, [fc]);

  useEffect(() => {
    let frames = 0;
    let last = performance.now();
    let raf = 0;
    const loop = () => {
      frames++;
      const t = performance.now();
      if (t - last >= 1000) {
        setFps(`${frames} fps`);
        frames = 0;
        last = t;
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  useEffect(() => {
    const id = window.setInterval(() => {
      const p = fc.params();
      if (!p) return;
      setParamsText(
        fc
          .paramFields()
          .map(
            (k: ParamField) =>
              `${k.padEnd(18, " ")} ${String(p[k]).padStart(5, " ")}`,
          )
          .join("\n"),
      );
    }, 200);
    return () => clearInterval(id);
  }, [fc]);

  useEffect(() => {
    const id = window.setInterval(() => {
      if (!blendSendDirty.current) return;
      blendSendDirty.current = false;
      if (!blendOn || !autoSend) return;
      const va = fc.blendVA();
      void postRaw("/api/raw/emotion/set-both", { a: va.a, v: va.v });
    }, 100);
    return () => clearInterval(id);
  }, [blendOn, autoSend, fc]);

  function markBlendDirty(): void {
    blendSendDirty.current = true;
  }

  const refreshCopyOutput = useCallback(() => {
    const o = fc.staticOverride();
    setCopyOut(
      formatKBaseTargetsCpp(
        o.expression || "Neutral",
        o.params,
        fc.paramFields(),
      ),
    );
  }, [fc]);

  const syncStaticSlidersFromBlendedParams = useCallback(() => {
    if (!blendOn) return;
    const va = fc.blendVA();
    let p: FaceParams = { ...fc.params() };
    if (emotionBlendDraw.ready()) {
      const blended = emotionBlendDraw.blendedFaceParams(va.v, va.a);
      if (blended) p = blended;
    }
    setSliderSnap({
      params: { ...p },
      blinkAmt: 0,
      gdx: 0,
      gdy: 0,
    });
    fc.setStaticOverride({ params: p, blinkAmt: 0, gdx: 0, gdy: 0 });
    refreshCopyOutput();
  }, [blendOn, fc, refreshCopyOutput]);

  useEffect(() => {
    refreshCopyOutput();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- one-shot after mount
  }, []);

  async function onExpressionClick(name: string): Promise<void> {
    fc.requestExpression(name);
    await handleExpressionBridge(name);
  }

  async function onOverlayClick(name: string): Promise<void> {
    fc.requestExpression(name);
    const verb = OVERLAY_MAP[name];
    const ms = overlayMs || 1200;
    await postRaw("/api/raw/verb/overlay", { verb, duration_ms: ms });
  }

  const expressions = fc.expressions();

  return (
    <div className="mx-auto max-w-[1600px] px-4 py-0 text-face-text">
      <h1 className="my-2 text-xl">Face Simulator — v3</h1>
      <p className="mb-4 text-sm text-face-muted">
        Browser preview of robot_v3 face renderer + frame controller. Pick an
        expression to drive the tween, or use Static mode to tune FaceParams.
        The C++ row is paste-ready for kBaseTargets[].
      </p>

      <div className="grid grid-cols-1 items-start gap-6 min-[881px]:grid-cols-4">
        <div>
          <ExpressionPickers
            expressions={expressions}
            currentExpr={currentExpr}
            overlayMs={overlayMs}
            onOverlayMsChange={setOverlayMs}
            onExpressionClick={onExpressionClick}
            onOverlayClick={onOverlayClick}
          />

          {/* <LiveParamsReadout text={paramsText} /> */}
        </div>

        <div className="col-span-2">
          <FaceStage
            canvasRef={faceCanvasRef}
            currentExpr={currentExpr}
            armDeg={armDeg}
            fps={fps}
          />
          <BlendPanel
            fc={fc}
            blendOn={blendOn}
            setBlendOn={setBlendOn}
            staticOn={staticOn}
            setStaticOn={setStaticOn}
            autoSend={autoSend}
            setAutoSend={setAutoSend}
            markBlendDirty={markBlendDirty}
            onBlendVaCommit={syncStaticSlidersFromBlendedParams}
          />
        </div>
        <div>
          <StaticModePanel
            fc={fc}
            expressions={expressions}
            blendOn={blendOn}
            setBlendOn={setBlendOn}
            staticOn={staticOn}
            setStaticOn={setStaticOn}
            staticPreset={staticPreset}
            setStaticPreset={setStaticPreset}
            sliderSnap={sliderSnap}
            setSliderSnap={setSliderSnap}
            copyOut={copyOut}
            refreshCopyOutput={refreshCopyOutput}
          />
        </div>
      </div>
    </div>
  );
}
