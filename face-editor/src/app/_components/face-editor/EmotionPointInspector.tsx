"use client";

import { useState } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { emotionVA } from "../../_lib/face-editor/simulatorBlendShared";
import { EMOTION_COLOR } from "../../_lib/face-editor/simulatorLayout";
import {
    paramRecordToPartialFaceParams,
    type ParamValueRecord,
} from "../../_lib/face-editor/inspectorParamClipboard";
import { InspectorParamActions } from "./InspectorParamActions";
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
    const { v, a } = emotionVA(emotion, fc.emotionTriangulation());

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
                <InspectorParamActions
                    params={sliderMode === "value" ? params : strengthParams}
                    onPaste={(record: ParamValueRecord) => {
                        const partial = paramRecordToPartialFaceParams(record);
                        if (sliderMode === "value") {
                            fc.patchLiveBaseFaceParams(emotion, partial);
                            onParamsChange({ ...params, ...partial } as FaceParams);
                        } else {
                            fc.patchLiveBaseFaceStrengths(emotion, partial);
                            onStrengthParamsChange({
                                ...strengthParams,
                                ...partial,
                            } as FaceParams);
                        }
                        onDirty();
                    }}
                />
                <div className="mb-2 flex justify-center gap-6 text-sm font-mono text-face-muted">
                    <span>valence {v.toFixed(2)}</span>
                    <span>arousal {a.toFixed(2)}</span>
                </div>
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
