"use client";

import { useCallback, useState } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import { namedEmotionAnchorLabel } from "../../_lib/face-engine/emotionTriangulationLive";
import Panel from "./atoms/Panel";

export function EmotionSchemaPanel({
    fc,
    onSchemaChange,
}: {
    fc: FrameController;
    onSchemaChange?: () => void;
}) {
    const [draftSlug, setDraftSlug] = useState("");
    const [error, setError] = useState<string | null>(null);

    const names = fc.emotionNames();

    const commitAdd = useCallback(() => {
        const slug = draftSlug.trim().toLowerCase().replace(/\s+/g, "_");
        if (!slug) return;
        const err = fc.addEmotion(slug);
        if (err) {
            setError(err);
            return;
        }
        setDraftSlug("");
        setError(null);
        onSchemaChange?.();
    }, [draftSlug, fc, onSchemaChange]);

    return (
        <Panel className="mt-2">
            <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                Emotion points
            </h4>
            <ul className="mb-2 max-h-[400px] list-none space-y-0.5 overflow-y-auto p-0 text-[0.78em]">
                {names.map((slug, i) => (
                    <li
                        key={slug}
                        className="grid grid-cols-5 gap-2 rounded px-1 py-0.5 hover:bg-face-panel-2"
                    >
                        <span className="col-span-2 font-mono text-face-text">{slug}</span>
                        <span className="col-span-2 text-face-muted">
                            {namedEmotionAnchorLabel(slug)}
                        </span>
                        {slug !== "neutral" ? (
                            <button
                                type="button"
                                className="shrink-0 rounded border border-red-900/60 px-1.5 py-0.5 text-[0.7em] text-red-400 hover:bg-red-950/40"
                                onClick={() => {
                                    const err = fc.removeEmotion(i);
                                    if (err) setError(err);
                                    else {
                                        setError(null);
                                        onSchemaChange?.();
                                    }
                                }}
                            >
                                Remove
                            </button>
                        ) : (
                            <span className="text-[0.65em] text-face-muted">required</span>
                        )}
                    </li>
                ))}
            </ul>
            <div className="flex flex-wrap items-center gap-1.5">
                <input
                    type="text"
                    className="min-w-0 flex-1 rounded border border-face-border bg-face-panel px-2 py-1 font-mono text-[0.8em] text-face-text"
                    placeholder="new_slug"
                    value={draftSlug}
                    onChange={e => setDraftSlug(e.target.value)}
                    onKeyDown={e => {
                        if (e.key === "Enter") commitAdd();
                    }}
                />
                <button
                    type="button"
                    className="rounded border border-face-border bg-face-panel-2 px-2 py-1 text-[0.78em] text-face-text hover:bg-face-panel"
                    onClick={commitAdd}
                >
                    Add emotion
                </button>
            </div>
            {error ? <p className="mt-1 text-[0.72em] text-red-400">{error}</p> : null}
        </Panel>
    );
}
