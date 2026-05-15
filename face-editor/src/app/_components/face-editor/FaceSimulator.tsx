"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  expressionIndexFromName,
  Expression,
} from "../../_lib/face-engine/FACE_CONFIG_DATA";
import { createFrameController } from "../../_lib/face-engine/frameController";
import {
  PARAM_FIELDS,
  PARAM_FIELDS_UI_ORDER,
  paramFieldLabel,
  type FaceParams,
  type ParamField,
  paramFieldFromFieldIndex,
} from "../../_lib/face-engine/faceParams";
import { resetVerbTransition } from "../../_lib/face-engine/verbTimeline";
import {
  applyVerbLoopDurationMs,
  fieldsInKeyframeOverrides,
  snapVerbPlayheadMs,
} from "../../_lib/face-engine/verbTimelineEdit";
import { formatKBaseTargetsCpp } from "../../_lib/face-editor/cppRowFormat";
import { postRaw } from "../../_lib/face-editor/bridge";
import { OVERLAY_MAP } from "../../_lib/face-editor/simulatorLayout";
import { BlendPanel } from "./BlendPanel";
import { EmotionPointInspector } from "./EmotionPointInspector";
import { FaceStage } from "./FaceStage";
import { KeyframeInspector } from "./KeyframeInspector";
import { StaticModePanel } from "./StaticModePanel";
import { VerbTimelinePanel, type VerbTimelineName } from "./VerbTimelinePanel";
import PanelModeSwitcher from "./PanelModeSwitcher";

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
  /** While true, do not copy `currentExpr` → verb dropdown (avoids fighting the dropdown before `requestExpression` lands). */
  const verbDropdownAwaitingEngineRef = useRef(false);
  const inspectorSendDirty = useRef(false);
  const inspectorLiveRef = useRef<{
    emotion: string;
    params: FaceParams;
  } | null>(null);

  const [inspectorEmotion, setInspectorEmotion] = useState<string | null>(null);
  const [inspectorParams, setInspectorParams] = useState<FaceParams | null>(
    null,
  );
  const [inspectorStrengths, setInspectorStrengths] =
    useState<FaceParams | null>(null);
  const [inspectorSendLive, setInspectorSendLive] = useState(false);

  const [simulatorMode, setSimulatorMode] = useState<"blend" | "verbTimeline">(
    "blend",
  );
  const [verbTimelineName, setVerbTimelineName] =
    useState<VerbTimelineName>("VerbThinking");
  const [verbPlayheadMs, setVerbPlayheadMs] = useState(0);
  const [verbPlaySpeed, setVerbPlaySpeed] = useState<0 | 0.25 | 0.5 | 1 | 2>(0);
  const [verbSelectedKeyframe, setVerbSelectedKeyframe] = useState<
    number | null
  >(null);
  const [timelineRev, setTimelineRev] = useState(0);
  const [verbLiveParams, setVerbLiveParams] = useState<FaceParams>(() => ({
    ...fc.params(),
  }));

  const verbTimelineMode = simulatorMode === "verbTimeline";

  const verbEnum = useMemo(
    () => expressionIndexFromName(verbTimelineName) as Expression,
    [verbTimelineName],
  );

  const verbTab = fc.verbTimelines().find((t) => t.verb === verbEnum);

  const keyframeHighlightFields = useMemo(() => {
    const s = new Set<ParamField>();
    if (!verbTab || verbSelectedKeyframe === null) return s;
    for (const fi of fieldsInKeyframeOverrides(verbTab, verbSelectedKeyframe)) {
      const pf = paramFieldFromFieldIndex(fi);
      if (pf) s.add(pf);
    }
    return s;
    // eslint-disable-next-line react-hooks/exhaustive-deps -- timelineRev bumps on in-place table mutation
  }, [verbTab, verbSelectedKeyframe, timelineRev]);

  const bumpTimeline = useCallback(() => {
    setTimelineRev((n) => n + 1);
  }, []);

  const commitVerbLoopDurationMs = useCallback(
    (ms: number) => {
      const t = fc.verbTimelines().find((x) => x.verb === verbEnum);
      if (!t) return;
      const applied = applyVerbLoopDurationMs(t, ms);
      setVerbPlayheadMs((p) => snapVerbPlayheadMs(p, applied));
      bumpTimeline();
    },
    [fc, verbEnum, bumpTimeline],
  );

  const setVerbPlayheadSnapped = useCallback(
    (ms: number) => {
      const t = fc.verbTimelines().find((x) => x.verb === verbEnum);
      const loop = t?.loop_duration_ms ?? 1000;
      setVerbPlayheadMs(snapVerbPlayheadMs(ms, loop));
    },
    [fc, verbEnum],
  );

  useEffect(() => {
    if (verbSelectedKeyframe === null || !verbTab) return;
    if (verbSelectedKeyframe >= verbTab.keyframe_count) {
      setVerbSelectedKeyframe(null);
    }
  }, [verbTab, verbSelectedKeyframe, timelineRev]);

  const setModeBlend = useCallback(() => {
    verbDropdownAwaitingEngineRef.current = false;
    setSimulatorMode("blend");
    setBlendOn(true);
    setStaticOn(false);
    fc.setStaticMode(false);
    setVerbPlaySpeed(0);
    fc.setVerbTimelinePreview(null);
  }, [fc]);

  const setModeVerbTimeline = useCallback(() => {
    setBlendOn(false);
    setStaticOn(false);
    fc.setStaticMode(false);
    setInspectorEmotion(null);
    setInspectorParams(null);
    setInspectorStrengths(null);
    setInspectorSendLive(false);
    setSimulatorMode("verbTimeline");
    setVerbPlaySpeed(0);
  }, [fc]);

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
    if (staticOn || blendOn || verbTimelineMode) return;
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
  }, [fc, staticOn, blendOn, verbTimelineMode]);

  useEffect(() => {
    if (blendOn && verbTimelineMode) {
      setModeBlend();
    }
  }, [blendOn, verbTimelineMode, setModeBlend]);

  useEffect(() => {
    if (staticOn && verbTimelineMode) {
      setModeBlend();
    }
  }, [staticOn, verbTimelineMode, setModeBlend]);

  useEffect(() => {
    if (!verbTimelineMode) return;
    if (!currentExpr.startsWith("Verb")) {
      verbDropdownAwaitingEngineRef.current = false;
      return;
    }
    if (verbDropdownAwaitingEngineRef.current) {
      if (currentExpr === verbTimelineName) {
        verbDropdownAwaitingEngineRef.current = false;
      }
      return;
    }
    if (currentExpr !== verbTimelineName) {
      setVerbTimelineName(currentExpr as VerbTimelineName);
      setVerbPlayheadMs(0);
      setVerbSelectedKeyframe(null);
    }
  }, [verbTimelineMode, currentExpr, verbTimelineName]);

  useEffect(() => {
    if (!verbTimelineMode) {
      fc.setVerbTimelinePreview(null);
      return;
    }
    fc.setVerbTimelinePreview({ verb: verbEnum, timeMs: verbPlayheadMs });
  }, [verbTimelineMode, verbEnum, verbPlayheadMs, fc]);

  useEffect(() => {
    const vEnum = expressionIndexFromName(verbTimelineName) as Expression;
    const t = fc.verbTimelines().find((x) => x.verb === vEnum);
    const loop = t?.loop_duration_ms ?? 1000;
    setVerbPlayheadMs(snapVerbPlayheadMs(0, loop));
    setVerbSelectedKeyframe(null);
  }, [verbTimelineName, fc]);

  useEffect(() => {
    if (!verbTimelineMode || verbPlaySpeed === 0) return;
    let raf = 0;
    let last = 0;
    let primed = false;
    const step = (t: number) => {
      if (!primed) {
        primed = true;
        last = t;
        raf = requestAnimationFrame(step);
        return;
      }
      let dt = t - last;
      last = t;
      if (dt < 0) dt = 0;
      if (dt > 250) dt = 250;
      const tabNow = fc.verbTimelines().find((x) => x.verb === verbEnum);
      const loop = tabNow?.loop_duration_ms ?? 1000;
      setVerbPlayheadMs((p) => {
        const next = p + dt * verbPlaySpeed;
        const wrapped = ((next % loop) + loop) % loop;
        return wrapped;
      });
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [verbTimelineMode, verbPlaySpeed, verbEnum, fc, timelineRev]);

  useEffect(() => {
    if (!verbTimelineMode || !verbTab) return;
    const loop = verbTab.loop_duration_ms;
    const tick = snapVerbPlayheadMs(verbPlayheadMs, loop);
    let found: number | null = null;
    for (let i = 0; i < verbTab.keyframe_count; i++) {
      const kf = verbTab.keyframes[i];
      if (!kf) continue;
      if (snapVerbPlayheadMs(kf.time_ms, loop) === tick) {
        found = i;
        break;
      }
    }
    setVerbSelectedKeyframe(found);
  }, [verbTimelineMode, verbTab, verbPlayheadMs, timelineRev]);

  useEffect(() => {
    if (!verbTimelineMode) return;
    let raf = 0;
    const tick = () => {
      setVerbLiveParams({ ...fc.params() });
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [verbTimelineMode, fc, timelineRev, verbPlayheadMs]);

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
        PARAM_FIELDS_UI_ORDER.map(
          (k: ParamField) =>
            `${paramFieldLabel(k).padEnd(24, " ")} ${String(p[k]).padStart(5, " ")}`,
        ).join("\n"),
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
      setInspectorStrengths(null);
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

  const onEmotionPointSelect = useCallback(
    (emotion: string) => {
      setInspectorEmotion(emotion);
      setInspectorParams({ ...fc.liveBaseFaceParams(emotion) });
      setInspectorStrengths({ ...fc.liveBaseFaceStrengths(emotion) });
    },
    [fc],
  );

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
    verbDropdownAwaitingEngineRef.current = false;
    fc.requestExpression(name);
    await handleExpressionBridge(name, {
      emotionTriangulation: fc.emotionTriangulation(),
    });
    if (verbTimelineMode && name.startsWith("Verb")) {
      setVerbTimelineName(name as VerbTimelineName);
    }
  }

  async function onOverlayClick(name: string): Promise<void> {
    fc.requestExpression(name);
    const verb = OVERLAY_MAP[name];
    const ms = overlayMs || 1200;
    await postRaw("/api/raw/verb/overlay", { verb, duration_ms: ms });
  }

  /** Match robot `clearVerb` + emotion-driven face: end verb on bridge and snap preview off verb timelines. */
  async function onClearVerb(): Promise<void> {
    verbDropdownAwaitingEngineRef.current = false;
    await postRaw("/api/raw/verb/clear", {});
    fc.requestExpression("Neutral");
    await handleExpressionBridge("Neutral");
  }

  const expressions = fc.expressions();

  return (
    <div className="mx-auto max-w-[1600px] px-4 py-0 text-face-text">
      <div className="grid grid-cols-1 items-start gap-0 min-[881px]:grid-cols-12">
        <div className="col-span-3">
          <PanelModeSwitcher
            mode={simulatorMode}
            setMode={(m) => {
              if (m === "blend") {
                setModeBlend();
              }
              if (m === "verbTimeline") {
                setModeVerbTimeline();
              }
            }}
          />
          
          <FaceStage
            canvasRef={faceCanvasRef}
            currentExpr={currentExpr}
            armDeg={armDeg}
            fps={fps}
          />
        </div>

        <div className="col-span-6">
          {!verbTimelineMode ? (
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
          ) : (
            <VerbTimelinePanel
              selectedVerb={verbTimelineName}
              onVerbChange={(v) => {
                verbDropdownAwaitingEngineRef.current = true;
                setVerbTimelineName(v);
              }}
              tab={verbTab}
              timelineRev={timelineRev}
              playheadMs={verbPlayheadMs}
              onPlayheadMs={setVerbPlayheadSnapped}
              selectedKeyframeIndex={verbSelectedKeyframe}
              onSelectKeyframe={setVerbSelectedKeyframe}
              playSpeed={verbPlaySpeed}
              onPlaySpeed={setVerbPlaySpeed}
              onJumpToStart={() => {
                setVerbPlayheadSnapped(0);
                setVerbPlaySpeed(0);
              }}
              onLoopDurationMsCommit={commitVerbLoopDurationMs}
            />
          )}
        </div>
        <div className="col-span-3">
          {verbTimelineMode ? (
            <KeyframeInspector
              verbName={verbTimelineName}
              tab={verbTab}
              timelineRev={timelineRev}
              params={verbLiveParams}
              playheadMs={verbPlayheadMs}
              selectedKeyframeIndex={verbSelectedKeyframe}
              highlightFields={keyframeHighlightFields}
              onTimelineMutated={bumpTimeline}
            />
          ) : inspectorEmotion && inspectorParams && inspectorStrengths ? (
            <EmotionPointInspector
              fc={fc}
              emotion={inspectorEmotion}
              params={inspectorParams}
              strengthParams={inspectorStrengths}
              onParamsChange={(next) => setInspectorParams(next)}
              onStrengthParamsChange={(next) => setInspectorStrengths(next)}
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
