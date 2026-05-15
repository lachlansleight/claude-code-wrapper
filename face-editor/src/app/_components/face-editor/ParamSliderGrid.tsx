"use client";

import type { FaceParams, ParamField } from "../../_lib/face-engine/faceParams";
import type { SliderRowDef, SliderSectionDef } from "../../_lib/face-editor/simulatorLayout";
import { PARAM_LAYOUT } from "../../_lib/face-editor/simulatorLayout";
import { FaTimes } from "react-icons/fa";

export type ParamSliderGridMode = "value" | "strength";

export function ParamSliderGrid({
    params,
    onFieldChange,
    sliderMode,
    onSliderModeChange,
    highlightFields,
    removeColumn,
}: {
    params: FaceParams;
    onFieldChange: (field: ParamField, value: number) => void;
    sliderMode: ParamSliderGridMode;
    onSliderModeChange: (m: ParamSliderGridMode) => void;
    highlightFields?: ReadonlySet<ParamField>;
    /** Fourth column: × clears override; blank cell keeps rows aligned. */
    removeColumn?: {
        removeableFields: ReadonlySet<ParamField>;
        onRemove: (field: ParamField) => void;
    };
}) {
    const gridCols = removeColumn
        ? "grid-cols-[9rem_1fr_4rem_1.5rem]"
        : "grid-cols-[9rem_1fr_4rem]";
    const strengthMode = sliderMode === "strength";

    return (
        <>
            <div className="mb-2.5 flex flex-wrap items-center gap-2 text-[0.78em]">
                <span className="font-semibold uppercase tracking-wide text-face-muted">
                    Sliders
                </span>
                <div className="inline-flex rounded border border-face-border bg-face-bg p-0.5">
                    <button
                        type="button"
                        className={
                            !strengthMode
                                ? "rounded bg-face-panel-2 px-2 py-0.5 font-semibold text-face-text"
                                : "rounded px-2 py-0.5 text-face-muted hover:text-face-text"
                        }
                        onClick={() => onSliderModeChange("value")}
                    >
                        Values
                    </button>
                    <button
                        type="button"
                        className={
                            strengthMode
                                ? "rounded bg-face-panel-2 px-2 py-0.5 font-semibold text-face-text"
                                : "rounded px-2 py-0.5 text-face-muted hover:text-face-text"
                        }
                        onClick={() => onSliderModeChange("strength")}
                    >
                        Strength (0–100)
                    </button>
                </div>
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
                                const [field, label, rowMin, rowMax, rowStep, reversed] = row;
                                const fk = field as keyof FaceParams;
                                const hi = highlightFields?.has(field as ParamField);
                                const min = strengthMode ? 0 : rowMin;
                                const max = strengthMode ? 100 : rowMax;
                                const step = strengthMode ? 1 : rowStep;
                                const displayLabel = strengthMode ? `${label} · strength` : label;
                                return (
                                    <div
                                        key={field}
                                        className={`my-0.5 grid ${gridCols} items-center gap-2 text-[0.78em] px-1 py-0.5 -mx-1 -my-0.5 ${hi ? "rounded bg-emerald-950/35" : ""}`}
                                    >
                                        <label
                                            className={
                                                hi
                                                    ? "font-mono font-semibold text-emerald-400"
                                                    : "font-mono text-face-muted"
                                            }
                                        >
                                            {displayLabel}
                                        </label>
                                        <input
                                            type="range"
                                            className={`w-full border-0 bg-transparent p-0 ${reversed && !strengthMode ? "[direction:rtl]" : ""}`}
                                            min={min}
                                            max={max}
                                            step={step}
                                            value={params[fk] ?? 0}
                                            onChange={e => {
                                                const v =
                                                    step < 1
                                                        ? parseFloat(e.target.value)
                                                        : parseInt(e.target.value, 10);
                                                onFieldChange(field as ParamField, v);
                                            }}
                                        />
                                        <input
                                            type="number"
                                            min={min}
                                            max={max}
                                            step={step}
                                            className="w-full rounded border border-face-border bg-face-panel px-1.5 py-0.5 text-right text-[0.85em] font-inherit text-face-text"
                                            value={params[fk] ?? 0}
                                            onChange={e => {
                                                const v =
                                                    step < 1
                                                        ? parseFloat(e.target.value)
                                                        : parseInt(e.target.value, 10);
                                                if (Number.isNaN(v)) return;
                                                onFieldChange(field as ParamField, v);
                                            }}
                                        />
                                        {removeColumn ? (
                                            removeColumn.removeableFields.has(
                                                field as ParamField
                                            ) ? (
                                                <button
                                                    type="button"
                                                    className="flex h-6 w-6 shrink-0 items-center justify-center rounded border border-emerald-600 bg-emerald-750 text-[0.85em] leading-none text-emerald-600 hover:border-red-500/60 hover:bg-red-950/40 hover:text-red-300"
                                                    title="Remove override from this keyframe"
                                                    aria-label={`Remove ${label} override`}
                                                    onClick={() =>
                                                        removeColumn.onRemove(field as ParamField)
                                                    }
                                                >
                                                    <FaTimes />
                                                </button>
                                            ) : (
                                                <span
                                                    className="inline-block h-6 w-6 shrink-0"
                                                    aria-hidden
                                                />
                                            )
                                        ) : null}
                                    </div>
                                );
                            })}
                        </div>
                    ))}
                </div>
            ))}
        </>
    );
}
