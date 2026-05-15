"use client";

import { useState } from "react";
import { expressionIndexFromName, type Expression } from "../../_lib/face-engine/FACE_CONFIG_DATA";
import type { FrameController } from "../../_lib/face-engine/frameController";
import { VERB_TIMELINE_NAMES } from "../../_lib/face-engine/verbCatalog";
import { postRaw } from "../../_lib/face-editor/bridge";
import { robotVerbStartSlug } from "../../_lib/face-editor/robotVerbBridge";
import Panel from "./atoms/Panel";

export function VerbButtons({ fc }: { fc: FrameController }) {
    const [activeVerb, setActiveVerb] = useState<Expression | null>(() => fc.blendVerbPreview());
    const [overlayMode, setOverlayMode] = useState(false);
    const [overlayMs, setOverlayMs] = useState(1500);

    return (
        <Panel className="mt-2">
            <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                Verbs + robot
            </h4>
            <p className="mb-2 text-[0.72em] leading-snug text-face-muted">
                Loop a verb timeline on the blended face, or play any verb as a timed overlay
                (500&nbsp;ms in, hold, 500&nbsp;ms out). Robot:{" "}
                <span className="font-mono">/api/raw/verb/start</span>,{" "}
                <span className="font-mono">/api/raw/verb/overlay</span>, or{" "}
                <span className="font-mono">/api/raw/verb/clear</span>.
            </p>
            <label className="mb-2 flex cursor-pointer items-center gap-2 text-[0.72em] text-face-muted">
                <input
                    type="checkbox"
                    checked={overlayMode}
                    onChange={e => setOverlayMode(e.target.checked)}
                    className="accent-sky-500"
                />
                Overlay mode (timed play; returns to prior loop verb when done)
            </label>
            {overlayMode ? (
                <label className="mb-2 flex items-center gap-2 text-[0.72em] text-face-muted">
                    <span>Duration (ms)</span>
                    <input
                        type="number"
                        min={600}
                        max={60000}
                        step={100}
                        value={overlayMs}
                        onChange={e => setOverlayMs(Number(e.target.value) || 1500)}
                        className="w-24 rounded border border-face-border bg-face-panel px-2 py-0.5 font-mono text-face-text"
                    />
                </label>
            ) : null}
            <div className="flex flex-wrap gap-1.5">
                <button
                    type="button"
                    className={
                        activeVerb === null && !overlayMode
                            ? "rounded border-2 border-emerald-500 bg-emerald-950/40 px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-emerald-300"
                            : "rounded border border-face-border bg-face-panel px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-face-muted hover:border-face-text hover:text-face-text"
                    }
                    onClick={() => {
                        fc.setBlendVerbPreview(null);
                        setActiveVerb(null);
                        void postRaw("/api/raw/verb/clear", {});
                    }}
                >
                    None
                </button>
                {VERB_TIMELINE_NAMES.map(name => {
                    const expr = expressionIndexFromName(name) as Expression;
                    const slug = robotVerbStartSlug(expr);
                    const on = !overlayMode && activeVerb === expr;
                    return (
                        <button
                            key={name}
                            type="button"
                            className={
                                on
                                    ? "rounded border-2 border-sky-500 bg-sky-950/40 px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-sky-200"
                                    : overlayMode
                                      ? "rounded border border-amber-700/60 bg-amber-950/30 px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-amber-200/90 hover:border-amber-500"
                                      : "rounded border border-face-border bg-face-panel px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-face-muted hover:border-face-text hover:text-face-text"
                            }
                            onClick={() => {
                                if (!slug) return;
                                if (overlayMode) {
                                    const ms = overlayMs > 0 ? overlayMs : 1500;
                                    fc.fireBlendVerbTransient(expr, ms, activeVerb);
                                    void postRaw("/api/raw/verb/overlay", {
                                        verb: slug,
                                        duration_ms: ms,
                                    });
                                    return;
                                }
                                fc.setBlendVerbPreview(expr);
                                setActiveVerb(expr);
                                void postRaw("/api/raw/verb/start", { verb: slug });
                            }}
                        >
                            {name.replace(/^Verb/, "")}
                        </button>
                    );
                })}
            </div>
        </Panel>
    );
}
