"use client";

import type { FrameController } from "../../_lib/face-engine/frameController";
import type { FaceParams } from "../../_lib/face-engine/faceParams";
import type {
  SliderRowDef,
  SliderSectionDef,
} from "../../_lib/face-editor/simulatorLayout";
import { EMOTION_COLOR, PARAM_LAYOUT } from "../../_lib/face-editor/simulatorLayout";

export function EmotionPointInspector({
  fc,
  emotion,
  params,
  onParamsChange,
  onDirty,
  sendLive,
  setSendLive,
}: {
  fc: FrameController;
  emotion: string;
  params: FaceParams;
  onParamsChange: (next: FaceParams) => void;
  onDirty: () => void;
  sendLive: boolean;
  setSendLive: (v: boolean) => void;
}) {
  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[24px] font-semibold uppercase tracking-wide text-center font-mono text-face-bg" style={{
        backgroundColor: EMOTION_COLOR[emotion] || "#ffffff",
      }}>
        {emotion}
      </h3>
      <div className="my-1.5 rounded-md border border-face-border bg-face-panel px-3 py-2">
        <label className="mb-2 flex cursor-pointer items-center gap-2 text-[0.85em] font-semibold text-face-muted">
          <input
            type="checkbox"
            checked={sendLive}
            onChange={(e) => setSendLive(e.target.checked)}
          />
          Send live (robot) — POST /api/raw/face/live-base-row when values change
        </label>

        {PARAM_LAYOUT.map((sec: SliderSectionDef, secIndex: number) => (
          <div
            key={sec.section}
            className={
              secIndex === 0
                ? "mt-0 border-t-0 pt-0"
                : "mt-2.5 border-t border-face-border pt-1.5"
            }
          >
            <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
              {sec.section}
            </h4>
            {sec.groups.map((group: SliderRowDef[], gi: number) => (
              <div
                key={gi}
                className={
                  gi === 0
                    ? "mt-0 border-t-0 pt-0"
                    : "mt-1.5 border-t border-dashed border-face-border pt-1.5"
                }
              >
                {group.map((row: SliderRowDef) => {
                  const [field, label, min, max, step, reversed] = row;
                  const fk = field as keyof FaceParams;
                  return (
                    <div
                      key={field}
                      className="my-0.5 grid grid-cols-[9rem_1fr_4rem] items-center gap-2 text-[0.78em]"
                    >
                      <label className="font-mono text-face-muted">{label}</label>
                      <input
                        type="range"
                        className={`w-full border-0 bg-transparent p-0 ${reversed ? "[direction:rtl]" : ""}`}
                        min={min}
                        max={max}
                        step={step}
                        value={params[fk] ?? 0}
                        onChange={(e) => {
                          const v =
                            step < 1
                              ? parseFloat(e.target.value)
                              : parseInt(e.target.value, 10);
                          fc.patchLiveBaseFaceParams(emotion, {
                            [field]: v,
                          } as Partial<FaceParams>);
                          onParamsChange({
                            ...params,
                            [field]: v,
                          } as FaceParams);
                          onDirty();
                        }}
                      />
                      <input
                        type="number"
                        min={min}
                        max={max}
                        step={step}
                        className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
                        value={params[fk] ?? 0}
                        onChange={(e) => {
                          const v =
                            step < 1
                              ? parseFloat(e.target.value)
                              : parseInt(e.target.value, 10);
                          if (Number.isNaN(v)) return;
                          fc.patchLiveBaseFaceParams(emotion, {
                            [field]: v,
                          } as Partial<FaceParams>);
                          onParamsChange({
                            ...params,
                            [field]: v,
                          } as FaceParams);
                          onDirty();
                        }}
                      />
                    </div>
                  );
                })}
              </div>
            ))}
          </div>
        ))}
      </div>
    </>
  );
}
