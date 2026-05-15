"use client";

import { useState } from "react";
import {
  expressionIndexFromName,
  type Expression,
} from "../../_lib/face-engine/FACE_CONFIG_DATA";
import type { FrameController } from "../../_lib/face-engine/frameController";
import { postRaw } from "../../_lib/face-editor/bridge";
import { robotVerbStartSlug } from "../../_lib/face-editor/robotVerbBridge";
import { VERB_TIMELINE_NAMES } from "./VerbTimelinePanel";
import Panel from "./atoms/Panel";

export function VerbButtons({ fc }: { fc: FrameController }) {
  const [activeVerb, setActiveVerb] = useState<Expression | null>(() =>
    fc.blendVerbPreview(),
  );

  return (
    <Panel className="mt-2">
      <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
        Verb overlay + robot
      </h4>
      <p className="mb-2 text-[0.72em] leading-snug text-face-muted">
        Layers a verb timeline on the blended face (500&nbsp;ms cross-fade
        in/out/between). POSTs{" "}
        <span className="font-mono">/api/raw/verb/start</span> or{" "}
        <span className="font-mono">/api/raw/verb/clear</span> (None).
      </p>
      <div className="flex flex-wrap gap-1.5">
        <button
          type="button"
          className={
            activeVerb === null
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
        {VERB_TIMELINE_NAMES.map((name) => {
          const expr = expressionIndexFromName(name) as Expression;
          const slug = robotVerbStartSlug(expr);
          const on = activeVerb === expr;
          return (
            <button
              key={name}
              type="button"
              className={
                on
                  ? "rounded border-2 border-sky-500 bg-sky-950/40 px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-sky-200"
                  : "rounded border border-face-border bg-face-panel px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-face-muted hover:border-face-text hover:text-face-text"
              }
              onClick={() => {
                if (!slug) return;
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
