"use client";

import type { Dispatch, SetStateAction } from "react";
import type { FrameController } from "../../_lib/face-engine/frameController";
import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import {
  formatParamIntsSignedCsv,
  parseCommaSeparatedParamValues,
} from "../../_lib/face-editor/cppRowFormat";
import type {
  SliderRowDef,
  SliderSectionDef,
} from "../../_lib/face-editor/simulatorLayout";
import {
  MOD_LAYOUT,
  PARAM_LAYOUT,
} from "../../_lib/face-editor/simulatorLayout";
import type { SliderSnapState } from "./simulatorTypes";
import Panel from "./atoms/Panel";

export function StaticModePanel({
  fc,
  expressions,
  blendOn,
  setBlendOn,
  staticOn,
  setStaticOn,
  staticPreset,
  setStaticPreset,
  sliderSnap,
  setSliderSnap,
  copyOut,
  refreshCopyOutput,
}: {
  fc: FrameController;
  expressions: readonly string[];
  blendOn: boolean;
  setBlendOn: (v: boolean) => void;
  staticOn: boolean;
  setStaticOn: (v: boolean) => void;
  staticPreset: string;
  setStaticPreset: (v: string) => void;
  sliderSnap: SliderSnapState;
  setSliderSnap: Dispatch<SetStateAction<SliderSnapState>>;
  copyOut: string;
  refreshCopyOutput: () => void;
}) {
  return (
    <>
      <h3 className="mt-4 mb-1.5 text-[0.85em] font-semibold uppercase tracking-wide text-face-muted">
        <label className="inline-flex cursor-pointer items-center gap-1.5 text-[0.95em] font-semibold normal-case tracking-normal text-face-text">
          <input
            type="checkbox"
            checked={staticOn}
            onChange={(e) => {
              const on = e.target.checked;
              if (on) {
                setBlendOn(false);
                fc.setStaticMode(true);
                const o = fc.staticOverride();
                setSliderSnap({
                  params: { ...o.params },
                  blinkAmt: o.blinkAmt,
                  gdx: o.gdx,
                  gdy: o.gdy,
                });
                refreshCopyOutput();
              } else {
                fc.setStaticMode(false);
              }
              setStaticOn(on);
            }}
          />
          Static mode (bypass animation)
        </label>
      </h3>

      <Panel disabled={!staticOn}>
        <div className="my-2 flex flex-wrap items-center gap-2">
          <button
            type="button"
            disabled={!staticOn}
            className="rounded border border-face-border bg-face-panel px-[8px] py-[4px] text-[12px] font-inherit text-face-text hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
            onClick={async () => {
              const o = fc.staticOverride();
              const text = formatParamIntsSignedCsv(o.params, fc.paramFields());
              try {
                await navigator.clipboard.writeText(text);
              } catch (err) {
                console.warn("Clipboard write blocked", err);
              }
            }}
          >
            Copy values
          </button>
          <button
            type="button"
            disabled={!staticOn}
            className="rounded border border-face-border bg-face-panel px-[8px] py-[4px] text-[12px] font-inherit text-face-text hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
            onClick={async () => {
              let text: string;
              try {
                text = await navigator.clipboard.readText();
              } catch (err) {
                console.warn("Clipboard read blocked", err);
                return;
              }
              const parsed = parseCommaSeparatedParamValues(text);
              if (!parsed.ok) {
                alert(parsed.error);
                return;
              }
              const fields = fc.paramFields();
              if (parsed.nums.length !== fields.length) {
                alert(
                  `Expected ${fields.length} comma-separated values (same order as Copy values), got ${parsed.nums.length}.`,
                );
                return;
              }
              const params = {} as FaceParams;
              fields.forEach((f: ParamField, i: number) => {
                params[f] = parsed.nums[i]!;
              });
              if (blendOn) {
                setBlendOn(false);
                fc.setBlendMode(false);
              }
              if (!staticOn) {
                setStaticOn(true);
                fc.setStaticMode(true);
              }
              setSliderSnap((s) => ({ ...s, params }));
              fc.setStaticOverride({ params, expression: staticPreset });
              refreshCopyOutput();
            }}
          >
            Paste values
          </button>
        </div>

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
                  return (
                    <div
                      key={field}
                      className="my-0.5 grid grid-cols-[9rem_1fr_4rem] items-center gap-2 text-[0.78em]"
                    >
                      <label className="font-mono text-face-muted">
                        {label}
                      </label>
                      <input
                        type="range"
                        className={`w-full border-0 bg-transparent p-0 ${reversed ? "[direction:rtl]" : ""}`}
                        min={min}
                        max={max}
                        step={step}
                        disabled={!staticOn}
                        value={
                          sliderSnap.params[field as keyof FaceParams] ?? 0
                        }
                        onChange={(e) => {
                          const v =
                            step < 1
                              ? parseFloat(e.target.value)
                              : parseInt(e.target.value, 10);
                          setSliderSnap((s) => {
                            const next = {
                              ...s.params,
                              [field]: v,
                            } as FaceParams;
                            fc.setStaticOverride({
                              params: { [field]: v } as Partial<FaceParams>,
                            });
                            return { ...s, params: next };
                          });
                          refreshCopyOutput();
                        }}
                      />
                      <input
                        type="number"
                        min={min}
                        max={max}
                        step={step}
                        disabled={!staticOn}
                        className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
                        value={
                          sliderSnap.params[field as keyof FaceParams] ?? 0
                        }
                        onChange={(e) => {
                          const v =
                            step < 1
                              ? parseFloat(e.target.value)
                              : parseInt(e.target.value, 10);
                          if (Number.isNaN(v)) return;
                          setSliderSnap((s) => {
                            const next = {
                              ...s.params,
                              [field]: v,
                            } as FaceParams;
                            fc.setStaticOverride({
                              params: { [field]: v } as Partial<FaceParams>,
                            });
                            return { ...s, params: next };
                          });
                          refreshCopyOutput();
                        }}
                      />
                    </div>
                  );
                })}
              </div>
            ))}
          </div>
        ))}

        <div className="mt-2.5 border-t border-face-border pt-1.5">
          <h4 className="mb-1.5 mt-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
            Modulations
          </h4>
          {MOD_LAYOUT.map((group: SliderRowDef[], gi: number) => (
            <div
              key={gi}
              className={
                gi === 0
                  ? "mt-0 border-t-0 pt-0"
                  : "mt-1.5 border-t border-dashed border-face-border pt-1.5"
              }
            >
              {group.map((row: SliderRowDef) => {
                const [field, label, min, max, step] = row;
                return (
                  <div
                    key={field}
                    className="my-0.5 grid grid-cols-[9rem_1fr_4rem] items-center gap-2 text-[0.78em]"
                  >
                    <label className="font-mono text-face-muted">{label}</label>
                    <input
                      type="range"
                      className="w-full border-0 bg-transparent p-0"
                      min={min}
                      max={max}
                      step={step}
                      disabled={!staticOn}
                      value={sliderSnap[field as "blinkAmt" | "gdx" | "gdy"]}
                      onChange={(e) => {
                        const v =
                          step < 1
                            ? parseFloat(e.target.value)
                            : parseInt(e.target.value, 10);
                        setSliderSnap((s) => ({ ...s, [field]: v }));
                        fc.setStaticOverride({ [field]: v } as {
                          blinkAmt?: number;
                          gdx?: number;
                          gdy?: number;
                        });
                        refreshCopyOutput();
                      }}
                    />
                    <input
                      type="number"
                      min={min}
                      max={max}
                      step={step}
                      disabled={!staticOn}
                      className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
                      value={sliderSnap[field as "blinkAmt" | "gdx" | "gdy"]}
                      onChange={(e) => {
                        const v =
                          step < 1
                            ? parseFloat(e.target.value)
                            : parseInt(e.target.value, 10);
                        if (Number.isNaN(v)) return;
                        setSliderSnap((s) => ({ ...s, [field]: v }));
                        fc.setStaticOverride({ [field]: v } as {
                          blinkAmt?: number;
                          gdx?: number;
                          gdy?: number;
                        });
                        refreshCopyOutput();
                      }}
                    />
                  </div>
                );
              })}
            </div>
          ))}
        </div>
      </Panel>
    </>
  );
}
