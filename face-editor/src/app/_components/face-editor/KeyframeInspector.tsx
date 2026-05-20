"use client";

import { useMemo, useState } from "react";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { fieldIndexFromParamField, PARAM_FIELDS } from "../../_lib/face-engine/faceParams";
import type { MutableVerbTimeline } from "../../_lib/face-engine/mutableVerbTimelines";
import type { ParamValueRecord } from "../../_lib/face-editor/inspectorParamClipboard";
import {
    applySliderToVerbTimeline,
    applyStrengthSliderToVerbTimeline,
    deleteKeyframeAtIndex,
    keyframeStrengthFaceParams,
    keyframeValueFaceParams,
    pasteStrengthsToKeyframe,
    pasteValuesToKeyframe,
    removeOverrideFromKeyframe,
    resolveVerbSliderKeyframeIndex,
} from "../../_lib/face-engine/verbTimelineEdit";
import Panel from "./atoms/Panel";
import { InspectorParamActions } from "./InspectorParamActions";
import { ParamSliderGrid } from "./ParamSliderGrid";

function zeroFaceParams(): FaceParams {
    const z = {} as FaceParams;
    for (const f of PARAM_FIELDS) z[f] = 0;
    return z;
}

export function KeyframeInspector({
    verbName,
    tab,
    /** Bumps when `tab` is mutated in place so derived memo (strength rows) recomputes. */
    timelineRev,
    /** Rendered face at playhead — live while playing, snapshotted when paused. */
    renderedParams,
    isPlaying,
    playheadMs,
    selectedKeyframeIndex,
    highlightFields,
    onTimelineMutated,
    onSelectKeyframe,
}: {
    verbName: string;
    tab: MutableVerbTimeline | undefined;
    timelineRev: number;
    renderedParams: FaceParams;
    isPlaying: boolean;
    playheadMs: number;
    selectedKeyframeIndex: number | null;
    highlightFields: ReadonlySet<ParamField>;
    onTimelineMutated: () => void;
    onSelectKeyframe: (index: number | null) => void;
}) {
    const [sliderMode, setSliderMode] = useState<"value" | "strength">("value");

    const editKeyframeIdx = useMemo(
        () => (tab ? resolveVerbSliderKeyframeIndex(tab, playheadMs, selectedKeyframeIndex) : null),
        [tab, playheadMs, selectedKeyframeIndex, timelineRev]
    );

    const valueParams = useMemo(() => {
        if (isPlaying || !tab) return renderedParams;
        return keyframeValueFaceParams(tab, editKeyframeIdx, renderedParams);
    }, [isPlaying, tab, editKeyframeIdx, renderedParams, timelineRev]);

    const strengthParams = useMemo(
        () => (tab ? keyframeStrengthFaceParams(tab, editKeyframeIdx) : zeroFaceParams()),
        [tab, editKeyframeIdx, timelineRev]
    );

    return (
        <>
            <h3 className="mt-4 mb-1.5 text-[24px] font-semibold uppercase tracking-wide text-center font-mono text-face-bg bg-face-panel-2 text-face-text">
                {verbName} · keyframes
            </h3>
            <p className="mb-1.5 text-[0.78em] text-face-muted">
                {isPlaying
                    ? "Playing: sliders follow the rendered face at the playhead."
                    : "Paused: drag to edit keyframe target values (green rows) or strengths. Other rows show the face at the playhead until you override them."}
            </p>
            <Panel>
                {selectedKeyframeIndex !== null && !isPlaying ? (
                    <InspectorParamActions
                        params={sliderMode === "value" ? valueParams : strengthParams}
                        showDelete
                        onPaste={(record: ParamValueRecord) => {
                            if (!tab || selectedKeyframeIndex === null) return;
                            if (sliderMode === "value") {
                                pasteValuesToKeyframe(tab, selectedKeyframeIndex, record);
                            } else {
                                pasteStrengthsToKeyframe(
                                    tab,
                                    selectedKeyframeIndex,
                                    record,
                                    renderedParams
                                );
                            }
                            onTimelineMutated();
                        }}
                        onDelete={() => {
                            if (!tab || selectedKeyframeIndex === null) return;
                            deleteKeyframeAtIndex(tab, selectedKeyframeIndex);
                            onSelectKeyframe(null);
                            onTimelineMutated();
                        }}
                    />
                ) : null}
                <ParamSliderGrid
                    sliderMode={sliderMode}
                    onSliderModeChange={setSliderMode}
                    params={sliderMode === "value" ? valueParams : strengthParams}
                    highlightFields={highlightFields}
                    disabled={isPlaying}
                    removeColumn={
                        isPlaying
                            ? undefined
                            : {
                                  removeableFields:
                                      tab !== undefined && selectedKeyframeIndex !== null
                                          ? highlightFields
                                          : new Set(),
                                  onRemove: field => {
                                      if (!tab) return;
                                      if (selectedKeyframeIndex === null) return;
                                      removeOverrideFromKeyframe(
                                          tab,
                                          selectedKeyframeIndex,
                                          fieldIndexFromParamField(field)
                                      );
                                      onTimelineMutated();
                                  },
                              }
                    }
                    onFieldChange={(field, v) => {
                        if (!tab || isPlaying) return;
                        const fi = fieldIndexFromParamField(field);
                        if (sliderMode === "value") {
                            applySliderToVerbTimeline(tab, {
                                playheadMs,
                                selectedKeyframeIndex,
                                field: fi,
                                targetValue: v,
                            });
                        } else {
                            applyStrengthSliderToVerbTimeline(tab, {
                                playheadMs,
                                selectedKeyframeIndex,
                                field: fi,
                                strength: v,
                                fallbackTargetValue: renderedParams[field] ?? 0,
                            });
                        }
                        onTimelineMutated();
                    }}
                />
            </Panel>
        </>
    );
}
