"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
    PARAM_UI_SECTIONS,
    fieldIndexFromParamField,
    paramFieldLabel,
    type ParamField,
} from "../../_lib/face-engine/faceParams";
import type { MutableVerbTimeline } from "../../_lib/face-engine/mutableVerbTimelines";
import {
    VERB_PLAYHEAD_QUANT_MS,
    snapVerbPlayheadMs,
} from "../../_lib/face-engine/verbTimelineEdit";
import { FaFastBackward, FaPause, FaPlay } from "react-icons/fa";
import Panel from "./atoms/Panel";

const ROW_H = 14;
/** Ruler row: ticks + 8px ms labels on whole seconds */
const RULER_H = 24;
const TICK_MINOR_PX = 4;
const TICK_HALF_PX = 8;
const TICK_MAJOR_PX = 12;

import { VERB_TIMELINE_NAMES, type VerbTimelineName } from "../../_lib/face-engine/verbCatalog";

export { VERB_TIMELINE_NAMES, type VerbTimelineName };

export function VerbTimelinePanel({
    selectedVerb,
    onVerbChange,
    tab,
    timelineRev,
    playheadMs,
    onPlayheadMs,
    selectedKeyframeIndex,
    onSelectKeyframe,
    playSpeed,
    onPlaySpeed,
    onJumpToStart,
    onLoopDurationMsCommit,
}: {
    selectedVerb: VerbTimelineName;
    onVerbChange: (v: VerbTimelineName) => void;
    tab: MutableVerbTimeline | undefined;
    timelineRev: number;
    playheadMs: number;
    onPlayheadMs: (ms: number) => void;
    selectedKeyframeIndex: number | null;
    onSelectKeyframe: (i: number | null) => void;
    playSpeed: 0 | 0.25 | 0.5 | 1 | 2;
    onPlaySpeed: (s: 0 | 0.25 | 0.5 | 1 | 2) => void;
    onJumpToStart: () => void;
    /** Mutates `tab` in place; parent should bump revision and re-snap playhead. */
    onLoopDurationMsCommit: (ms: number) => void;
}) {
    const trackRef = useRef<HTMLDivElement>(null);
    const [loopDraft, setLoopDraft] = useState("1000");

    const loopMs = tab?.loop_duration_ms ?? 1000;
    const displayMs = Math.max(loopMs, 1000);

    useEffect(() => {
        if (!tab) {
            setLoopDraft("1000");
            return;
        }
        setLoopDraft(String(tab.loop_duration_ms));
    }, [tab, timelineRev]);

    const commitLoopDraft = useCallback(() => {
        if (!tab) return;
        const trimmed = loopDraft.trim();
        const n = parseInt(trimmed, 10);
        if (Number.isNaN(n)) {
            setLoopDraft(String(tab.loop_duration_ms));
            return;
        }
        onLoopDurationMsCommit(n);
    }, [tab, loopDraft, onLoopDurationMsCommit]);

    const rulerTicks = useMemo(() => {
        const dm = Math.round(displayMs);
        const out: { t: number; kind: "minor" | "half" | "major" }[] = [];
        for (let t = 0; t <= dm; t += 100) {
            let kind: "minor" | "half" | "major" = "minor";
            if (t % 1000 === 0) kind = "major";
            else if (t % 500 === 0) kind = "half";
            out.push({ t, kind });
        }
        return out;
    }, [displayMs]);

    const xToMs = useCallback(
        (clientX: number): number => {
            const el = trackRef.current;
            if (!el) return 0;
            const rect = el.getBoundingClientRect();
            if (rect.width <= 0) return 0;
            const u = (clientX - rect.left) / rect.width;
            const raw = u * displayMs;
            return snapVerbPlayheadMs(Math.max(0, Math.min(displayMs, raw)), loopMs);
        },
        [displayMs, loopMs]
    );

    const onTrackPointerDown = (e: React.PointerEvent) => {
        trackRef.current?.setPointerCapture(e.pointerId);
        onPlayheadMs(xToMs(e.clientX));
    };

    const onTrackPointerMove = (e: React.PointerEvent) => {
        if (!trackRef.current?.hasPointerCapture(e.pointerId)) return;
        if (e.buttons !== 1) return;
        onPlayheadMs(xToMs(e.clientX));
    };

    function keyframeMarkersForField(f: ParamField): React.ReactNode[] {
        if (!tab) return [];
        const fi = fieldIndexFromParamField(f);
        const nodes: React.ReactNode[] = [];
        const kfs = tab.keyframes.slice(0, tab.keyframe_count);
        for (let ki = 0; ki < kfs.length; ki++) {
            const kf = kfs[ki]!;
            const n = Math.min(kf.override_count, kf.overrides.length);
            for (let oi = 0; oi < n; oi++) {
                const o = kf.overrides[oi];
                if (!o || o.strength <= 0) continue;
                if (o.field !== fi) continue;
                const snappedT = snapVerbPlayheadMs(kf.time_ms, loopMs);
                const leftPct = (snappedT / displayMs) * 100;
                const sel = selectedKeyframeIndex === ki;
                nodes.push(
                    <button
                        key={`${ki}-${oi}`}
                        type="button"
                        onPointerDown={e => {
                            e.stopPropagation();
                            onSelectKeyframe(ki);
                            onPlayheadMs(snappedT);
                        }}
                        className={
                            sel
                                ? "absolute z-[2] h-2 w-2 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-emerald-400 bg-emerald-300 p-0 shadow"
                                : "absolute z-[1] h-2 w-2 -translate-x-1/2 -translate-y-1/2 rounded-full border border-face-border bg-face-accent p-0 shadow hover:bg-face-accent/90"
                        }
                        style={{
                            left: `${leftPct}%`,
                            top: "50%",
                        }}
                        title={`${paramFieldLabel(f)} · kf ${ki} @ ${kf.time_ms}ms (tick ${snappedT}ms)`}
                    />
                );
            }
        }
        return nodes;
    }

    return (
        <Panel>
            <div className="mb-2 flex flex-wrap items-center gap-2 text-[0.85em]">
                <label className="flex items-center gap-1.5 text-face-text">
                    <span className="text-face-muted">Verb</span>
                    <select
                        className="rounded border border-face-border bg-face-panel px-2 py-1 font-mono text-face-text"
                        value={selectedVerb}
                        onChange={e => onVerbChange(e.target.value as VerbTimelineName)}
                    >
                        {VERB_TIMELINE_NAMES.map(n => (
                            <option key={n} value={n}>
                                {n}
                            </option>
                        ))}
                    </select>
                </label>
                <span className="text-face-muted">·</span>
                <label className="flex items-center gap-1.5 text-face-text">
                    <span className="text-face-muted">Loop</span>
                    <input
                        type="number"
                        className="w-[5.5rem] rounded border border-face-border bg-face-panel px-1.5 py-0.5 font-mono text-[0.8em] text-face-text"
                        min={VERB_PLAYHEAD_QUANT_MS}
                        step={VERB_PLAYHEAD_QUANT_MS}
                        disabled={!tab}
                        value={loopDraft}
                        onChange={e => setLoopDraft(e.target.value)}
                        onBlur={commitLoopDraft}
                        onKeyDown={e => {
                            if (e.key === "Enter") {
                                (e.target as HTMLInputElement).blur();
                            }
                        }}
                        title="Loop length in ms (60 ms steps). Shorter/longer than last keyframe is clamped."
                    />
                    <span className="text-face-muted">ms</span>
                </label>
                <span className="font-mono text-[0.8em] text-face-muted">· view {displayMs}ms</span>
                <span className="text-face-muted hidden sm:inline">·</span>
                <span className="text-[0.78em] text-face-muted">rev {timelineRev}</span>
            </div>

            <div className="mb-2 flex flex-wrap items-center gap-1.5">
                <button
                    type="button"
                    className="rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                    onClick={onJumpToStart}
                >
                    <FaFastBackward />
                </button>
                {playSpeed === 0 ? (
                    <button
                        type="button"
                        className="rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                        onClick={() => onPlaySpeed(1)}
                    >
                        <FaPlay />
                    </button>
                ) : (
                    <button
                        type="button"
                        className="rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                        onClick={() => onPlaySpeed(0)}
                    >
                        <FaPause />
                    </button>
                )}
                <button
                    type="button"
                    className={
                        playSpeed === 0.25
                            ? "rounded border border-face-accent bg-face-panel-2 px-2 py-1 text-xs font-inherit text-face-accent"
                            : "rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                    }
                    onClick={() => onPlaySpeed(0.25)}
                >
                    0.25x
                </button>
                <button
                    type="button"
                    className={
                        playSpeed === 0.5
                            ? "rounded border border-face-accent bg-face-panel-2 px-2 py-1 text-xs font-inherit text-face-accent"
                            : "rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                    }
                    onClick={() => onPlaySpeed(0.5)}
                >
                    0.5x
                </button>
                <button
                    type="button"
                    className={
                        playSpeed === 1
                            ? "rounded border border-face-accent bg-face-panel-2 px-2 py-1 text-xs font-inherit text-face-accent"
                            : "rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                    }
                    onClick={() => onPlaySpeed(1)}
                >
                    1x
                </button>
                <button
                    type="button"
                    className={
                        playSpeed === 2
                            ? "rounded border border-face-accent bg-face-panel-2 px-2 py-1 text-xs font-inherit text-face-accent"
                            : "rounded border border-face-border bg-face-panel px-2 py-1 text-xs font-inherit text-face-text hover:bg-face-panel-2"
                    }
                    onClick={() => onPlaySpeed(2)}
                >
                    2x
                </button>
                <span className="ml-2 font-mono text-[0.75em] text-face-muted">
                    t={Math.round(playheadMs)}ms
                </span>
            </div>

            <div className="flex border border-face-border text-[0.65em]">
                <div
                    className="w-[6.5rem] shrink-0 border-r border-face-border py-0.5 pl-1 text-face-muted"
                    style={{ paddingTop: 2 }}
                >
                    <div
                        className="shrink-0 border-b border-face-border/40 bg-face-panel/50"
                        style={{ height: RULER_H }}
                        aria-hidden
                    />
                    {PARAM_UI_SECTIONS.map((sec, secIndex) => (
                        <div
                            key={sec.section}
                            className={
                                secIndex === 0
                                    ? "mt-0 border-t-0 pt-0"
                                    : "mt-2.5 border-t border-face-border pt-1.5"
                            }
                        >
                            {sec.groups.map((group, gi) => (
                                <div
                                    key={`${sec.section}-g${gi}`}
                                    className={
                                        gi === 0
                                            ? "mt-0 border-t-0 pt-0"
                                            : "mt-1.5 border-t border-dashed border-face-border pt-1.5"
                                    }
                                >
                                    {group.map(f => (
                                        <div
                                            key={f}
                                            className="truncate leading-none text-[12px]"
                                            style={{ height: ROW_H, lineHeight: `${ROW_H}px` }}
                                            title={f}
                                        >
                                            {paramFieldLabel(f)}
                                        </div>
                                    ))}
                                </div>
                            ))}
                        </div>
                    ))}
                </div>
                <div
                    ref={trackRef}
                    className="relative flex min-h-0 min-w-[320px] flex-1 touch-none select-none cursor-crosshair flex-col bg-face-canvas"
                    onPointerDown={onTrackPointerDown}
                    onPointerMove={onTrackPointerMove}
                    onPointerUp={e => {
                        try {
                            trackRef.current?.releasePointerCapture(e.pointerId);
                        } catch {
                            /* no capture */
                        }
                    }}
                >
                    <div
                        className="relative shrink-0 border-b border-face-border/50 bg-face-panel/30"
                        style={{ height: RULER_H }}
                        aria-hidden
                    >
                        {rulerTicks.map(({ t, kind }) => {
                            const leftPct = (t / displayMs) * 100;
                            const h =
                                kind === "major"
                                    ? TICK_MAJOR_PX
                                    : kind === "half"
                                      ? TICK_HALF_PX
                                      : TICK_MINOR_PX;
                            const lineClass =
                                kind === "major"
                                    ? "bg-face-muted"
                                    : kind === "half"
                                      ? "bg-face-border"
                                      : "bg-face-border/70";
                            return (
                                <div
                                    key={t}
                                    className="pointer-events-none absolute inset-y-0 w-0"
                                    style={{
                                        left: `${leftPct}%`,
                                        transform: "translateX(-50%)",
                                    }}
                                >
                                    {kind === "major" ||
                                    (kind === "half" && rulerTicks.length < 20) ? (
                                        <span className="absolute left-1/2 top-0 -translate-x-1/2 font-mono text-[10px] tabular-nums leading-none text-face-muted">
                                            {t}
                                        </span>
                                    ) : null}
                                    <div
                                        className={`absolute bottom-0 left-1/2 w-px -translate-x-1/2 ${lineClass}`}
                                        style={{ height: h }}
                                    />
                                </div>
                            );
                        })}
                    </div>
                    <div className="flex min-h-0 flex-1 flex-col">
                        {PARAM_UI_SECTIONS.map((sec, secIndex) => (
                            <div
                                key={sec.section}
                                className={
                                    secIndex === 0
                                        ? "mt-0 border-t-0 pt-0"
                                        : "mt-2.5 border-t border-face-border pt-1.5"
                                }
                            >
                                {sec.groups.map((group, gi) => (
                                    <div
                                        key={`${sec.section}-track-g${gi}`}
                                        className={
                                            gi === 0
                                                ? "mt-0 border-t-0 pt-0"
                                                : "mt-1.5 border-t border-dashed border-face-border pt-1.5"
                                        }
                                    >
                                        {group.map(f => (
                                            <div
                                                key={f}
                                                className="relative shrink-0 border-b border-face-border/25"
                                                style={{ height: ROW_H }}
                                            >
                                                {keyframeMarkersForField(f)}
                                            </div>
                                        ))}
                                    </div>
                                ))}
                            </div>
                        ))}
                    </div>

                    <div className="pointer-events-none absolute inset-0 z-[3] w-full" aria-hidden>
                        <div
                            className="absolute top-0 bottom-0 w-px bg-red-500/90 shadow"
                            style={{ left: `${(playheadMs / displayMs) * 100}%` }}
                        />
                    </div>
                </div>
            </div>
        </Panel>
    );
}
