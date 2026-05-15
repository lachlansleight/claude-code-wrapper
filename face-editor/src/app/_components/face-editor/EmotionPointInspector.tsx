"use client";

import { useState } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { EMOTION_COLOR } from "../../_lib/face-editor/simulatorLayout";
import { ParamSliderGrid } from "./ParamSliderGrid";
import Panel from "./atoms/Panel";

export function EmotionPointInspector({
    fc,
    emotion,
    params,
    strengthParams,
    onParamsChange,
    onStrengthParamsChange,
    onDirty,
    sendLive,
    setSendLive,
}: {
    fc: FrameController;
    emotion: string;
    params: FaceParams;
    strengthParams: FaceParams;
    onParamsChange: (next: FaceParams) => void;
    onStrengthParamsChange: (next: FaceParams) => void;
    onDirty: () => void;
    sendLive: boolean;
    setSendLive: (v: boolean) => void;
}) {
    const [sliderMode, setSliderMode] = useState<"value" | "strength">("value");

    return (
        <>
            <h3
                className="mt-4 mb-1.5 text-[24px] font-semibold uppercase tracking-wide text-center font-mono text-face-bg"
                style={{
                    backgroundColor: EMOTION_COLOR[emotion] || "#ffffff",
                }}
            >
                {emotion}
            </h3>
            <Panel>
                <label className="mb-2 flex cursor-pointer items-center gap-2 text-[0.85em] font-semibold text-face-muted">
                    <input
                        type="checkbox"
                        checked={sendLive}
                        onChange={e => setSendLive(e.target.checked)}
                    />
                    Send live (robot) — POST /api/raw/face/live-base-row when values change
                </label>

                <ParamSliderGrid
                    sliderMode={sliderMode}
                    onSliderModeChange={setSliderMode}
                    params={sliderMode === "value" ? params : strengthParams}
                    onFieldChange={(field, v) => {
                        if (sliderMode === "value") {
                            fc.patchLiveBaseFaceParams(emotion, {
                                [field]: v,
                            } as Partial<FaceParams>);
                            onParamsChange({
                                ...params,
                                [field]: v,
                            } as FaceParams);
                        } else {
                            fc.patchLiveBaseFaceStrengths(emotion, {
                                [field]: v,
                            } as Partial<FaceParams>);
                            onStrengthParamsChange({
                                ...strengthParams,
                                [field]: v,
                            } as FaceParams);
                        }
                        onDirty();
                    }}
                />
            </Panel>
        </>
    );
}
