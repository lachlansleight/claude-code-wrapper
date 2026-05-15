"use client";

import { useMemo, useState } from "react";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { fieldIndexFromParamField, PARAM_FIELDS } from "../../_lib/face-engine/faceParams";
import type { MutableVerbTimeline } from "../../_lib/face-engine/mutableVerbTimelines";
import {
  applySliderToVerbTimeline,
  applyStrengthSliderToVerbTimeline,
  keyframeStrengthFaceParams,
  removeOverrideFromKeyframe,
  resolveVerbSliderKeyframeIndex,
} from "../../_lib/face-engine/verbTimelineEdit";
import Panel from "./atoms/Panel";
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
  params,
  playheadMs,
  selectedKeyframeIndex,
  highlightFields,
  onTimelineMutated,
}: {
  verbName: string;
  tab: MutableVerbTimeline | undefined;
  timelineRev: number;
  params: FaceParams;
  playheadMs: number;
  selectedKeyframeIndex: number | null;
  highlightFields: ReadonlySet<ParamField>;
  onTimelineMutated: () => void;
}) {
  const [sliderMode, setSliderMode] = useState<"value" | "strength">("value");

  const editKeyframeIdx = useMemo(
    () =>
      tab
        ? resolveVerbSliderKeyframeIndex(tab, playheadMs, selectedKeyframeIndex)
        : null,
    [tab, playheadMs, selectedKeyframeIndex, timelineRev],
  );

  const strengthParams = useMemo(
    () =>
      tab ? keyframeStrengthFaceParams(tab, editKeyframeIdx) : zeroFaceParams(),
    [tab, editKeyframeIdx, timelineRev],
  );

  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[24px] font-semibold uppercase tracking-wide text-center font-mono text-face-bg bg-face-panel-2 text-face-text">
        {verbName} · keyframes
      </h3>
      <p className="mb-1.5 text-[0.78em] text-face-muted">
        Sliders reflect the rendered face at the playhead. Green rows match
        overrides on the selected keyframe. Drag to edit at the playhead, add on
        the selection, or create a keyframe at the playhead. Strength mode edits
        per-field override weight (0–100) on the same resolved keyframe.
      </p>
      <Panel>
        <ParamSliderGrid
          sliderMode={sliderMode}
          onSliderModeChange={setSliderMode}
          params={sliderMode === "value" ? params : strengthParams}
          highlightFields={highlightFields}
          removeColumn={{
            removeableFields:
              tab !== undefined && selectedKeyframeIndex !== null
                ? highlightFields
                : new Set(),
            onRemove: (field) => {
              if (!tab) return;
              if (selectedKeyframeIndex === null) return;
              removeOverrideFromKeyframe(
                tab,
                selectedKeyframeIndex,
                fieldIndexFromParamField(field),
              );
              onTimelineMutated();
            },
          }}
          onFieldChange={(field, v) => {
            if (!tab) return;
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
                fallbackTargetValue: params[field] ?? 0,
              });
            }
            onTimelineMutated();
          }}
        />
      </Panel>
    </>
  );
}
