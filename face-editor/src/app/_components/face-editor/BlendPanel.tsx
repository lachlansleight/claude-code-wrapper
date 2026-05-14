"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import type { MutableEmotionTriangulation } from "../../_lib/face-engine/emotionTriangulationLive";
import { retriangulateEmotionAnchors } from "../../_lib/face-engine/emotionTriangulationLive";
import { canvasClientToVa } from "../../_lib/face-editor/blendCanvasMath";
import {
  computeBlendMetaHtml,
  drawBlendDiagram,
  pickBlendAnchorIndexAtCanvas,
} from "../../_lib/face-editor/drawBlendDiagram";

const W = 720;
const H = 720;

type DragKind = "va" | "anchor";

export function BlendPanel({
  fc,
  blendOn,
  setBlendOn,
  staticOn,
  setStaticOn,
  autoSend,
  setAutoSend,
  markBlendDirty,
  onBlendVaCommit,
  onEmotionPointSelect,
}: {
  fc: FrameController;
  blendOn: boolean;
  setBlendOn: (v: boolean) => void;
  staticOn: boolean;
  setStaticOn: (v: boolean) => void;
  autoSend: boolean;
  setAutoSend: (v: boolean) => void;
  markBlendDirty: () => void;
  /** Called whenever V/A changes (pointer, sliders, numbers) — e.g. sync static sliders from blended params. */
  onBlendVaCommit: () => void;
  /** User pressed on an emotion point (V/A anchor) — open emotion point inspector. */
  onEmotionPointSelect?: (emotion: string) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ kind: DragKind; anchorIndex: number } | null>(null);
  const [metaHtml, setMetaHtml] = useState("triangle: — · weights: —");
  const [blendV, setBlendV] = useState(0);
  const [blendA, setBlendA] = useState(0.5);

  const redraw = useCallback(() => {
    const c = canvasRef.current;
    if (!c) return;
    const ctx = c.getContext("2d");
    if (!ctx) return;
    const blendApi = fc.emotionBlendApi();
    const tri = fc.emotionTriangulation();
    drawBlendDiagram(ctx, fc, blendApi, tri, W, H);
    setMetaHtml(computeBlendMetaHtml(fc, blendApi, tri));
  }, [fc]);

  const applyVa = useCallback(
    (v: number, a: number) => {
      fc.setBlendVA(v, a);
      setBlendV(v);
      setBlendA(a);
      redraw();
      onBlendVaCommit();
      markBlendDirty();
    },
    [fc, markBlendDirty, onBlendVaCommit, redraw],
  );

  useEffect(() => {
    if (blendOn && canvasRef.current) {
      canvasRef.current.style.cursor = "crosshair";
    }
  }, [blendOn]);

  useEffect(() => {
    if (!blendOn) return;
    const id = window.setInterval(redraw, 100);
    return () => clearInterval(id);
  }, [blendOn, redraw, blendV, blendA]);

  useEffect(() => {
    redraw();
  }, [blendOn, redraw]);

  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
        <div className="my-0 flex flex-wrap items-center gap-2">
          <label className="inline-flex cursor-pointer items-center gap-1.5 text-[0.95em] font-semibold normal-case tracking-normal text-face-text">
            <input
              type="checkbox"
              checked={blendOn}
              onChange={(e) => {
                const on = e.target.checked;
                if (on && staticOn) setStaticOn(false);
                setBlendOn(on);
                fc.setBlendMode(on);
                if (on) {
                  const va = fc.blendVA();
                  setBlendV(va.v);
                  setBlendA(va.a);
                  redraw();
                  onBlendVaCommit();
                  markBlendDirty();
                }
              }}
            />
            Blend mode (V/A → triangulated FaceParams)
          </label>
          <label className="ml-2 inline-flex cursor-pointer items-center gap-1.5 text-[0.95em] font-semibold normal-case tracking-normal text-face-muted">
            <input
              type="checkbox"
              checked={autoSend}
              onChange={(e) => {
                setAutoSend(e.target.checked);
                markBlendDirty();
              }}
            />
            Auto-Send (robot)
          </label>
        </div>
      </h3>
      <p className="mb-1.5 text-[0.78em] text-face-muted">
        Coloured rings are <strong className="font-semibold text-face-text">emotion points</strong>{" "}
        (V/A anchors). Drag empty space for blend cursor; drag a ring to move that point. Click a
        ring to open the emotion point inspector.
      </p>

      <div
        className={
          blendOn
            ? "my-1.5 rounded-md border border-face-border bg-face-panel px-3 py-2"
            : "pointer-events-none my-1.5 rounded-md border border-face-border bg-face-panel px-3 py-2 opacity-50"
        }
      >
        <div className="flex justify-center rounded border border-face-border bg-face-canvas p-1.5">
          <canvas
            ref={canvasRef}
            className="block aspect-square w-full max-w-[720px] touch-none [image-rendering:pixelated]"
            width={W}
            height={H}
            onPointerDown={(e) => {
              if (!blendOn) return;
              if (e.pointerType === "mouse" && e.button !== 0) return;
              e.preventDefault();
              const el = e.currentTarget as HTMLCanvasElement;
              el.setPointerCapture(e.pointerId);
              const rect = el.getBoundingClientRect();
              const cx = (e.clientX - rect.left) * (W / rect.width);
              const cy = (e.clientY - rect.top) * (H / rect.height);
              const tri = fc.emotionTriangulation();
              const hit = pickBlendAnchorIndexAtCanvas(cx, cy, tri, W, H);
              if (hit !== null) {
                dragRef.current = { kind: "anchor", anchorIndex: hit };
                el.style.cursor = "grabbing";
                const em = tri.anchors[hit]?.emotion;
                if (em) onEmotionPointSelect?.(em);
              } else {
                dragRef.current = { kind: "va", anchorIndex: -1 };
                const [v, a] = canvasClientToVa(
                  e.clientX,
                  e.clientY,
                  rect,
                  W,
                  H,
                );
                applyVa(v, a);
              }
            }}
            onPointerMove={(e) => {
              if (!blendOn) return;
              e.preventDefault();
              const el = e.currentTarget as HTMLCanvasElement;
              const rect = el.getBoundingClientRect();
              const cx = (e.clientX - rect.left) * (W / rect.width);
              const cy = (e.clientY - rect.top) * (H / rect.height);
              const tri = fc.emotionTriangulation() as MutableEmotionTriangulation;

              if (el.hasPointerCapture(e.pointerId)) {
                const d = dragRef.current;
                if (d?.kind === "anchor") {
                  const [v, a] = canvasClientToVa(
                    e.clientX,
                    e.clientY,
                    rect,
                    W,
                    H,
                  );
                  const an = tri.anchors[d.anchorIndex];
                  if (an) {
                    an.v = v;
                    an.a = a;
                  }
                  retriangulateEmotionAnchors(tri);
                  redraw();
                  markBlendDirty();
                } else if (d?.kind === "va") {
                  const [v, a] = canvasClientToVa(
                    e.clientX,
                    e.clientY,
                    rect,
                    W,
                    H,
                  );
                  applyVa(v, a);
                }
                return;
              }

              const hit = pickBlendAnchorIndexAtCanvas(cx, cy, tri, W, H);
              el.style.cursor = hit !== null ? "pointer" : "crosshair";
            }}
            onPointerLeave={(e) => {
              const el = e.currentTarget as HTMLCanvasElement;
              if (!el.hasPointerCapture(e.pointerId)) {
                el.style.cursor = "crosshair";
              }
            }}
            onPointerUp={(e) => {
              const el = e.currentTarget as HTMLCanvasElement;
              try {
                el.releasePointerCapture(e.pointerId);
              } catch {
                /* */
              }
              dragRef.current = null;
              if (blendOn) {
                const rect = el.getBoundingClientRect();
                const cx = (e.clientX - rect.left) * (W / rect.width);
                const cy = (e.clientY - rect.top) * (H / rect.height);
                const tri = fc.emotionTriangulation();
                const hit = pickBlendAnchorIndexAtCanvas(cx, cy, tri, W, H);
                el.style.cursor = hit !== null ? "pointer" : "crosshair";
              }
            }}
          />
        </div>
        <div className="my-0.5 grid grid-cols-[9rem_1fr_4rem] items-center gap-2 text-[0.78em]">
          <label className="font-mono text-face-muted">valence</label>
          <input
            type="range"
            min={-1}
            max={1}
            step={0.01}
            value={blendV}
            disabled={!blendOn}
            className="w-full border-0 bg-transparent p-0"
            onChange={(e) => {
              const v = parseFloat(e.target.value);
              fc.setBlendVA(v, blendA);
              setBlendV(v);
              redraw();
              onBlendVaCommit();
              markBlendDirty();
            }}
          />
          <input
            type="number"
            min={-1}
            max={1}
            step={0.01}
            value={blendV}
            disabled={!blendOn}
            className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
            onChange={(e) => {
              const v = parseFloat(e.target.value);
              if (Number.isNaN(v)) return;
              fc.setBlendVA(v, blendA);
              setBlendV(v);
              redraw();
              onBlendVaCommit();
              markBlendDirty();
            }}
          />
        </div>
        <div className="my-0.5 grid grid-cols-[9rem_1fr_4rem] items-center gap-2 text-[0.78em]">
          <label className="font-mono text-face-muted">activation</label>
          <input
            type="range"
            min={0}
            max={1}
            step={0.01}
            value={blendA}
            disabled={!blendOn}
            className="w-full border-0 bg-transparent p-0"
            onChange={(e) => {
              const a = parseFloat(e.target.value);
              fc.setBlendVA(blendV, a);
              setBlendA(a);
              redraw();
              onBlendVaCommit();
              markBlendDirty();
            }}
          />
          <input
            type="number"
            min={0}
            max={1}
            step={0.01}
            value={blendA}
            disabled={!blendOn}
            className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
            onChange={(e) => {
              const a = parseFloat(e.target.value);
              if (Number.isNaN(a)) return;
              fc.setBlendVA(blendV, a);
              setBlendA(a);
              redraw();
              onBlendVaCommit();
              markBlendDirty();
            }}
          />
        </div>
        <div
          className="mt-1.5 font-mono text-[0.78em] text-face-muted [&_b]:font-semibold [&_b]:text-face-text"
          dangerouslySetInnerHTML={{ __html: metaHtml }}
        />
      </div>
    </>
  );
}
