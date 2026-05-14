"use client";

import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import { fieldIndexFromParamField } from "../../_lib/face-engine/faceParams";
import type { MutableVerbTimeline } from "../../_lib/face-engine/mutableVerbTimelines";
import {
  applySliderToVerbTimeline,
  removeOverrideFromKeyframe,
} from "../../_lib/face-engine/verbTimelineEdit";
import Panel from "./atoms/Panel";
import { ParamSliderGrid } from "./ParamSliderGrid";

export function KeyframeInspector({
  verbName,
  tab,
  params,
  playheadMs,
  selectedKeyframeIndex,
  highlightFields,
  onTimelineMutated,
}: {
  verbName: string;
  tab: MutableVerbTimeline | undefined;
  params: FaceParams;
  playheadMs: number;
  selectedKeyframeIndex: number | null;
  highlightFields: ReadonlySet<ParamField>;
  onTimelineMutated: () => void;
}) {
  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[24px] font-semibold uppercase tracking-wide text-center font-mono text-face-bg bg-face-panel-2 text-face-text">
        {verbName} · keyframes
      </h3>
      <p className="mb-1.5 text-[0.78em] text-face-muted">
        Sliders reflect the rendered face at the playhead. Green rows match overrides
        on the selected keyframe. Drag to edit at the playhead, add on the selection, or
        create a keyframe at the playhead.
      </p>
      <Panel>
        <ParamSliderGrid
          params={params}
          highlightFields={highlightFields}
          removeColumn={{
            removeableFields: tab !== undefined && selectedKeyframeIndex !== null ? highlightFields : new Set(),
            onRemove: (field) => {
              if (!tab) return;
              if (selectedKeyframeIndex === null) return;
              removeOverrideFromKeyframe(
                tab,
                selectedKeyframeIndex,
                fieldIndexFromParamField(field),
              );
              onTimelineMutated();
            }
          }}
          onFieldChange={(field, v) => {
            if (!tab) return;
            applySliderToVerbTimeline(tab, {
              playheadMs,
              selectedKeyframeIndex,
              field: fieldIndexFromParamField(field),
              targetValue: v,
            });
            onTimelineMutated();
          }}
        />
      </Panel>
    </>
  );
}
