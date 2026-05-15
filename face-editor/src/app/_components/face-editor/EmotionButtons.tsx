"use client";

import type { FrameController } from "../../_lib/face-engine/frameController";
import { postRaw } from "../../_lib/face-editor/bridge";
import { EMOTION_COLOR } from "../../_lib/face-editor/simulatorLayout";
import Panel from "./atoms/Panel";
import { useMemo } from "react";

export function EmotionButtons({ fc }: { fc: FrameController }) {
    const tri = fc.emotionTriangulation();

    const buttons = useMemo(() => {
        const ids = [
            ["Distressed", "Shocked", "Gleeful", "Joyful"],
            ["Frustrated", "Cheeky", "Excited"],
            ["Disappointed", "Sad", "Neutral", "Happy"],
            ["Depressed", "Sleepy", "Blissed"],
        ];
        return ids.map(row => {
            return row.map(emotionName => {
                const an = tri.anchors.find(an => an.emotion === emotionName);
                if (!an) return null;
                const bg = EMOTION_COLOR[an.emotion] || "#4a5568";
                return (
                    <button
                        key={an.emotion}
                        type="button"
                        className="rounded border border-face-border px-2 py-1 text-[0.72em] font-semibold uppercase tracking-wide text-face-bg shadow-sm hover:opacity-95"
                        style={{ backgroundColor: bg }}
                        onClick={() => {
                            fc.setVerbPreviewBaseVa(an.v, an.a);
                            void postRaw("/api/raw/emotion/set-both", { v: an.v, a: an.a });
                        }}
                    >
                        {an.emotion}
                    </button>
                );
            });
        });
    }, [tri.anchors]);

    return (
        <Panel className="mt-2">
            <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                Base emotion (V/A) + robot
            </h4>
            <p className="mb-2 text-[0.72em] leading-snug text-face-muted">
                Sets the blended face under the verb preview and POSTs{" "}
                <span className="font-mono">/api/raw/emotion/set-both</span> with the anchor&apos;s
                valence and arousal.
            </p>
            <div className="flex flex-col gap-1.5">
                {buttons.map((row, index) => {
                    return (
                        <div key={index} className="flex gap-1.5">
                            {row.map(button => button)}
                        </div>
                    );
                })}
            </div>
        </Panel>
    );
}
