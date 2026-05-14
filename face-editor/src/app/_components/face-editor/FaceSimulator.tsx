"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { createFrameController } from "../../_lib/face-engine/frameController";
import {
  PARAM_FIELDS,
  type FaceParams,
  type ParamField,
} from "../../_lib/face-engine/faceParams";
import { formatKBaseTargetsCpp } from "../../_lib/face-editor/cppRowFormat";
import { postRaw } from "../../_lib/face-editor/bridge";
import { OVERLAY_MAP } from "../../_lib/face-editor/simulatorLayout";
import { BlendPanel } from "./BlendPanel";
import { EmotionPointInspector } from "./EmotionPointInspector";
import { ExpressionPickers, handleExpressionBridge } from "./ExpressionPickers";
import { FaceStage } from "./FaceStage";
import { LiveParamsReadout } from "./LiveParamsReadout";
import { StaticModePanel } from "./StaticModePanel";

export function FaceSimulator() {
  const fc = useMemo(() => createFrameController(), []);
  const faceCanvasRef = useRef<HTMLCanvasElement>(null);

  const [currentExpr, setCurrentExpr] = useState(fc.currentExpression());
  const [blendOn, setBlendOn] = useState(true);
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
  const inspectorSendDirty = useRef(false);
  const inspectorLiveRef = useRef<{
    emotion: string;
    params: FaceParams;
  } | null>(null);

  const [inspectorEmotion, setInspectorEmotion] = useState<string | null>(null);
  const [inspectorParams, setInspectorParams] = useState<FaceParams | null>(
    null,
  );
  const [inspectorSendLive, setInspectorSendLive] = useState(false);

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

  useEffect(() => {
    if (inspectorEmotion && inspectorParams) {
      inspectorLiveRef.current = {
        emotion: inspectorEmotion,
        params: inspectorParams,
      };
    } else {
      inspectorLiveRef.current = null;
    }
  }, [inspectorEmotion, inspectorParams]);

  useEffect(() => {
    if (!blendOn || staticOn) {
      setInspectorEmotion(null);
      setInspectorParams(null);
      setInspectorSendLive(false);
    }
  }, [blendOn, staticOn]);

  useEffect(() => {
    const id = window.setInterval(() => {
      if (!inspectorSendDirty.current) return;
      inspectorSendDirty.current = false;
      if (!inspectorSendLive || !blendOn) return;
      const cur = inspectorLiveRef.current;
      if (!cur) return;
      void postRaw("/api/raw/face/live-base-row", {
        expression: cur.emotion,
        values: PARAM_FIELDS.map((f) => cur.params[f]),
      });
    }, 100);
    return () => clearInterval(id);
  }, [inspectorSendLive, blendOn]);

  const onEmotionPointSelect = useCallback((emotion: string) => {
    setInspectorEmotion(emotion);
    setInspectorParams({ ...fc.liveBaseFaceParams(emotion) });
  }, [fc]);

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
    const blend = fc.emotionBlendApi();
    if (blend.ready()) {
      const blended = blend.blendedFaceParams(va.v, va.a);
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

  /** Keep `fc` blend flag in sync with React (checkbox alone only updates on user click). */
  useEffect(() => {
    fc.setBlendMode(blendOn);
    if (blendOn) {
      syncStaticSlidersFromBlendedParams();
      markBlendDirty();
    }
  }, [fc, blendOn, syncStaticSlidersFromBlendedParams]);

  useEffect(() => {
    refreshCopyOutput();
    // eslint-disable-next-line react-hooks/exhaustive-deps -- one-shot after mount
  }, []);

  async function onExpressionClick(name: string): Promise<void> {
    fc.requestExpression(name);
    await handleExpressionBridge(name, {
      emotionTriangulation: fc.emotionTriangulation(),
    });
  }

  async function onOverlayClick(name: string): Promise<void> {
    fc.requestExpression(name);
    const verb = OVERLAY_MAP[name];
    const ms = overlayMs || 1200;
    await postRaw("/api/raw/verb/overlay", { verb, duration_ms: ms });
  }

  /** Match robot `clearVerb` + emotion-driven face: end verb on bridge and snap preview off verb timelines. */
  async function onClearVerb(): Promise<void> {
    await postRaw("/api/raw/verb/clear", {});
    fc.requestExpression("Neutral");
    await handleExpressionBridge("Neutral");
  }

  const expressions = fc.expressions();

  return (
    <div className="mx-auto max-w-[1600px] px-4 py-0 text-face-text">
      <h1 className="my-2 text-[24px] font-mono font-semibold">Face Editor</h1>

      <div className="grid grid-cols-1 items-start gap-0 min-[881px]:grid-cols-12">
        <div className="col-span-2">
          <ExpressionPickers
            expressions={expressions}
            currentExpr={currentExpr}
            overlayMs={overlayMs}
            onOverlayMsChange={setOverlayMs}
            onExpressionClick={onExpressionClick}
            onOverlayClick={onOverlayClick}
            onClearVerb={onClearVerb}
          />

          {/* <LiveParamsReadout text={paramsText} /> */}
        </div>

        <div className="col-span-6">
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
            onEmotionPointSelect={onEmotionPointSelect}
          />
        </div>
        <div className="col-span-4">
          {(inspectorEmotion && inspectorParams) ? (
            <EmotionPointInspector
                fc={fc}
                emotion={inspectorEmotion}
                params={inspectorParams}
                onParamsChange={(next) => setInspectorParams(next)}
                onDirty={() => {
                  inspectorSendDirty.current = true;
                }}
                sendLive={inspectorSendLive}
                setSendLive={setInspectorSendLive}
              />
          ) : (
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
          )}
        </div>
      </div>
    </div>
  );
}
