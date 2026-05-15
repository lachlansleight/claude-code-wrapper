"use client";

import { useCallback, useState } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import { cloneFaceConfigState } from "../../_lib/face-engine/mutableFaceConfig";
import { retriangulateEmotionAnchors } from "../../_lib/face-engine/emotionTriangulationLive";
import type { MutableEmotionTriangulation } from "../../_lib/face-engine/emotionTriangulationLive";
import Panel from "./atoms/Panel";

export function SaveFaceConfigButton({ fc }: { fc: FrameController }): JSX.Element {
    const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
    const [message, setMessage] = useState("");

    const onSave = useCallback(async () => {
        setStatus("saving");
        setMessage("");
        try {
            const tri = fc.emotionTriangulation() as MutableEmotionTriangulation;
            retriangulateEmotionAnchors(tri);

            const cfg = fc.faceConfig();
            const payload = cloneFaceConfigState({
                ...cfg,
                emotionTriangulation: tri,
            });

            const res = await fetch("/api/saveData", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ config: payload }),
            });
            const body = (await res.json()) as
                | { ok: true; filesWritten: string[]; config: typeof payload }
                | { ok: false; error: string };

            if (!body.ok) throw new Error(body.error ?? "Save failed");

            const reloaded = cloneFaceConfigState(body.config);
            fc.replaceFaceConfigState(reloaded);

            const n = body.filesWritten.length;
            setStatus("ok");
            setMessage(
                `Wrote ${n} file(s) (FACE_CONFIG_DATA.ts/.h, EmotionTriangulation.h, snapshot). Ready for Arduino upload.`
            );
        } catch (e) {
            setStatus("error");
            setMessage(e instanceof Error ? e.message : String(e));
        }
    }, [fc]);

    return (
        <Panel className="mb-2 flex flex-col gap-2">
            <button
                type="button"
                disabled={status === "saving"}
                className="rounded border border-face-accent bg-face-panel-2 px-3 py-2 text-sm font-inherit text-face-accent hover:bg-face-panel disabled:opacity-50"
                onClick={() => void onSave()}
            >
                {status === "saving" ? "Saving…" : "Save Face Config Data"}
            </button>
            {message ? (
                <p
                    className={
                        status === "error" ? "text-xs text-red-400" : "text-xs text-face-muted"
                    }
                >
                    {message}
                </p>
            ) : null}
        </Panel>
    );
}
