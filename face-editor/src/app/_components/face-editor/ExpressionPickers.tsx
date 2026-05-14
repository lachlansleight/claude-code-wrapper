"use client";

import { postRaw } from "../../_lib/face-editor/bridge";
import type { EmotionTriangulationTable } from "../../_lib/face-engine/types";
import { emotionVA } from "../../_lib/face-editor/simulatorBlendShared";
import {
  isEmotionExprName,
  OVERLAY_MAP,
  VERB_MAP,
} from "../../_lib/face-editor/simulatorLayout";
import Panel from "./atoms/Panel";

export function ExpressionPickers({
  expressions,
  currentExpr,
  overlayMs,
  onOverlayMsChange,
  onExpressionClick,
  onOverlayClick,
  onClearVerb,
}: {
  expressions: readonly string[];
  currentExpr: string;
  overlayMs: number;
  onOverlayMsChange: (ms: number) => void;
  onExpressionClick: (name: string) => void | Promise<void>;
  onOverlayClick: (name: string) => void | Promise<void>;
  onClearVerb: () => void | Promise<void>;
}) {
  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
        Force expression
      </h3>
      <Panel>
        <h3 className="mt-2.5 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
          Emotions
        </h3>
        <div className="grid grid-cols-[repeat(auto-fill,minmax(8rem,1fr))] gap-1.5">
          {expressions
            .filter((e) => !e.startsWith("Verb") && !e.startsWith("Overlay"))
            .map((e) => (
              <button
                key={e}
                type="button"
                className={
                  currentExpr === e
                    ? "rounded border border-face-accent bg-face-panel-2 px-1.5 py-2 text-sm font-inherit text-face-accent transition-colors disabled:cursor-not-allowed disabled:opacity-45"
                    : "rounded border border-face-border bg-face-panel px-1.5 py-2 text-sm font-inherit text-face-text transition-colors hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
                }
                onClick={() => void onExpressionClick(e)}
              >
                {e}
              </button>
            ))}
        </div>

        <h3 className="mt-2.5 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
          Verbs
        </h3>
        <div className="my-2 flex flex-wrap items-center gap-2">
          <button
            type="button"
            className="rounded border border-face-border bg-face-panel px-2.5 py-1.5 text-sm font-inherit text-face-text hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
            onClick={() => void onClearVerb()}
          >
            Clear verb
          </button>
        </div>
        <div className="grid grid-cols-[repeat(auto-fill,minmax(8rem,1fr))] gap-1.5">
          {expressions
            .filter((e) => e.startsWith("Verb"))
            .map((e) => (
              <button
                key={e}
                type="button"
                className={
                  currentExpr === e
                    ? "rounded border border-face-accent bg-face-panel-2 px-1.5 py-2 text-sm font-inherit text-face-accent transition-colors disabled:cursor-not-allowed disabled:opacity-45"
                    : "rounded border border-face-border bg-face-panel px-1.5 py-2 text-sm font-inherit text-face-text transition-colors hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
                }
                onClick={() => void onExpressionClick(e)}
              >
                {e}
              </button>
            ))}
        </div>

        <h3 className="mt-2.5 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
          Overlays
        </h3>
        <div className="my-2 flex flex-wrap items-center gap-2">
          <label className="text-sm text-face-muted">
            duration (ms)
            <input
              type="number"
              min={100}
              max={60000}
              value={overlayMs}
              onChange={(e) => onOverlayMsChange(Number(e.target.value) || 1200)}
              className="ml-1.5 w-[7.5em] rounded border border-face-border bg-face-panel px-2.5 py-1.5 text-sm text-face-text"
            />
          </label>
        </div>
        <div className="grid grid-cols-[repeat(auto-fill,minmax(8rem,1fr))] gap-1.5">
          {Object.keys(OVERLAY_MAP).map((e) => (
            <button
              key={e}
              type="button"
              className={
                currentExpr === e
                  ? "rounded border border-face-accent bg-face-panel-2 px-1.5 py-2 text-sm font-inherit text-face-accent transition-colors disabled:cursor-not-allowed disabled:opacity-45"
                  : "rounded border border-face-border bg-face-panel px-1.5 py-2 text-sm font-inherit text-face-text transition-colors hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
              }
              onClick={() => void onOverlayClick(e)}
            >
              {e}
            </button>
          ))}
        </div>
        </Panel>
    </>
  );
}

export async function handleExpressionBridge(
  name: string,
  opts?: { emotionTriangulation?: EmotionTriangulationTable },
): Promise<void> {
  if (name.startsWith("Verb")) {
    const verb = VERB_MAP[name];
    if (verb) await postRaw("/api/raw/verb/start", { verb });
    return;
  }
  if (isEmotionExprName(name)) {
    const va = emotionVA(name, opts?.emotionTriangulation);
    await postRaw("/api/raw/emotion/set-both", { a: va.a, v: va.v });
  }
}
