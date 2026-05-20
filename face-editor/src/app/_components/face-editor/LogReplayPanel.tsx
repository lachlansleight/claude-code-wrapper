"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import Panel from "./atoms/Panel";
import { bridgeBase } from "../../_lib/face-editor/bridge";
import type { BridgeLogFileInfo } from "../../api/bridgeLogs/route";
import {
    formatEntrySummary,
    parseLogText,
    resolveReplayContext,
    scheduleLogReplay,
    type TurnLogEntry,
} from "../../_lib/face-editor/logReplay";

function formatBytes(n: number): string {
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
    return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

function formatMtime(ms: number): string {
    return new Date(ms).toLocaleString(undefined, {
        month: "short",
        day: "numeric",
        hour: "2-digit",
        minute: "2-digit",
    });
}

export function LogReplayPanel({
    selectedIndex,
    onSelectIndex,
    onEntriesChange,
}: {
    selectedIndex: number | null;
    onSelectIndex: (index: number | null) => void;
    onEntriesChange?: (entries: TurnLogEntry[]) => void;
}): JSX.Element {
    const [files, setFiles] = useState<BridgeLogFileInfo[]>([]);
    const [listError, setListError] = useState<string | null>(null);
    const [agentFilter, setAgentFilter] = useState<string>("");
    const [loadingFile, setLoadingFile] = useState(false);
    const [loadedName, setLoadedName] = useState<string | null>(null);
    const [entries, setEntries] = useState<TurnLogEntry[]>([]);
    const [speed, setSpeed] = useState(1);
    const [replaying, setReplaying] = useState(false);
    const [status, setStatus] = useState<string | null>(null);
    const [replayHighlight, setReplayHighlight] = useState<number | null>(null);
    const cancelReplayRef = useRef<(() => void) | null>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const refreshList = useCallback(async () => {
        setListError(null);
        try {
            const res = await fetch("/api/bridgeLogs");
            const body = (await res.json()) as
                | { ok: true; files: BridgeLogFileInfo[] }
                | { ok: false; error: string };
            if (!body.ok) throw new Error(body.error ?? "list failed");
            setFiles(body.files);
            if (!agentFilter && body.files.length > 0) {
                setAgentFilter(body.files[0]!.agent);
            }
        } catch (e) {
            setListError(e instanceof Error ? e.message : String(e));
        }
    }, [agentFilter]);

    useEffect(() => {
        void refreshList();
    }, [refreshList]);

    const loadFromText = useCallback(
        (text: string, label: string) => {
            cancelReplayRef.current?.();
            cancelReplayRef.current = null;
            setReplaying(false);
            const parsed = parseLogText(text);
            setEntries(parsed);
            onEntriesChange?.(parsed);
            setLoadedName(label);
            onSelectIndex(parsed.length > 0 ? 0 : null);
            setStatus(parsed.length > 0 ? `Loaded ${parsed.length} entries from ${label}` : "No valid entries");
        },
        [onSelectIndex, onEntriesChange]
    );

    const loadServerFile = useCallback(
        async (f: BridgeLogFileInfo) => {
            setLoadingFile(true);
            setStatus(null);
            try {
                const res = await fetch(
                    `/api/bridgeLogs?agent=${encodeURIComponent(f.agent)}&file=${encodeURIComponent(f.name)}`
                );
                const body = (await res.json()) as
                    | { ok: true; content: string }
                    | { ok: false; error: string };
                if (!body.ok) throw new Error(body.error ?? "read failed");
                loadFromText(body.content, `${f.agent}/${f.name}`);
            } catch (e) {
                setStatus(e instanceof Error ? e.message : String(e));
            } finally {
                setLoadingFile(false);
            }
        },
        [loadFromText]
    );

    const onLocalFile = useCallback(
        async (file: File | null) => {
            if (!file) return;
            const text = await file.text();
            loadFromText(text, file.name);
        },
        [loadFromText]
    );

    const stopReplay = useCallback(() => {
        cancelReplayRef.current?.();
        cancelReplayRef.current = null;
        setReplaying(false);
        setReplayHighlight(null);
        setStatus("Replay stopped.");
    }, []);

    const startReplay = useCallback(() => {
        if (entries.length === 0) return;
        cancelReplayRef.current?.();
        setReplaying(true);
        setReplayHighlight(null);
        const { cancel } = scheduleLogReplay(entries, {
            speed,
            onStatus: setStatus,
            onEntryIndex: setReplayHighlight,
            onFinished: () => {
                setReplaying(false);
                cancelReplayRef.current = null;
            },
        });
        cancelReplayRef.current = cancel;
    }, [entries, speed]);

    useEffect(() => () => cancelReplayRef.current?.(), []);

    const agents = Array.from(new Set(files.map(f => f.agent))).sort();
    const filteredFiles = agentFilter ? files.filter(f => f.agent === agentFilter) : files;
    const ctx = entries.length > 0 ? resolveReplayContext(entries) : null;
    return (
        <div className="flex min-h-0 flex-col gap-2">
            <Panel className="flex flex-col gap-2">
                <h4 className="m-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                    Log replay
                </h4>
                <p className="m-0 text-[0.72em] leading-snug text-face-muted">
                    Load JSONL turn logs from{" "}
                    <span className="font-mono">plugin/logs/</span> or a local file, inspect entries,
                    then replay parsed events to the bridge via{" "}
                    <span className="font-mono">POST /hooksRaw/:agent</span> (
                    {bridgeBase()}).
                </p>
                <div className="flex flex-wrap items-center gap-2">
                    <label className="text-[0.72em] text-face-muted">
                        Agent
                        <select
                            className="ml-1 rounded border border-face-border bg-face-panel px-2 py-1 font-inherit text-face-text"
                            value={agentFilter}
                            onChange={e => setAgentFilter(e.target.value)}
                        >
                            <option value="">All</option>
                            {agents.map(a => (
                                <option key={a} value={a}>
                                    {a}
                                </option>
                            ))}
                        </select>
                    </label>
                    <button
                        type="button"
                        className="rounded border border-face-border bg-face-panel px-2 py-1 text-[0.72em] font-inherit text-face-text hover:bg-face-panel-2"
                        onClick={() => void refreshList()}
                    >
                        Refresh list
                    </button>
                    <button
                        type="button"
                        className="rounded border border-face-border bg-face-panel px-2 py-1 text-[0.72em] font-inherit text-face-text hover:bg-face-panel-2"
                        onClick={() => fileInputRef.current?.click()}
                    >
                        Open local file…
                    </button>
                    <input
                        ref={fileInputRef}
                        type="file"
                        accept=".log,.jsonl,.txt"
                        className="hidden"
                        onChange={e => void onLocalFile(e.target.files?.[0] ?? null)}
                    />
                </div>
                {listError ? <p className="m-0 text-[0.72em] text-red-400">{listError}</p> : null}
            </Panel>

            <Panel className="flex max-h-[220px] min-h-0 flex-col gap-1 overflow-hidden p-0">
                <div className="border-b border-face-border px-2 py-1 text-[0.68em] font-semibold uppercase tracking-wide text-face-muted">
                    Server logs {loadingFile ? "(loading…)" : ""}
                </div>
                <ul className="m-0 min-h-0 flex-1 list-none overflow-y-auto p-0">
                    {filteredFiles.length === 0 ? (
                        <li className="px-2 py-2 text-[0.72em] text-face-muted">No log files found.</li>
                    ) : (
                        filteredFiles.map(f => (
                            <li key={`${f.agent}/${f.name}`}>
                                <button
                                    type="button"
                                    className="w-full border-b border-face-border/50 px-2 py-1.5 text-left text-[0.72em] text-face-text hover:bg-face-panel-2 disabled:opacity-50"
                                    disabled={loadingFile}
                                    onClick={() => void loadServerFile(f)}
                                >
                                    <span className="font-mono text-face-accent">{f.name}</span>
                                    <span className="ml-2 text-face-muted">
                                        {formatMtime(f.mtimeMs)} · {formatBytes(f.size)}
                                    </span>
                                </button>
                            </li>
                        ))
                    )}
                </ul>
            </Panel>

            <Panel className="flex flex-wrap items-center gap-2">
                <label className="flex items-center gap-1 text-[0.72em] text-face-text">
                    Speed
                    <input
                        type="number"
                        min={0.1}
                        step={0.1}
                        value={speed}
                        onChange={e => setSpeed(Math.max(0.1, parseFloat(e.target.value) || 1))}
                        className="w-14 rounded border border-face-border bg-face-panel px-1 py-0.5 font-mono text-face-text"
                    />
                    ×
                </label>
                <button
                    type="button"
                    className="rounded border border-face-accent bg-face-panel-2 px-3 py-1 text-[0.72em] font-inherit text-face-accent hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-45"
                    disabled={entries.length === 0 || replaying}
                    onClick={startReplay}
                >
                    Play
                </button>
                <button
                    type="button"
                    className="rounded border border-face-border bg-face-panel px-3 py-1 text-[0.72em] font-inherit text-face-text hover:bg-face-panel-2 disabled:cursor-not-allowed disabled:opacity-45"
                    disabled={!replaying}
                    onClick={stopReplay}
                >
                    Stop
                </button>
                {loadedName ? (
                    <span className="text-[0.72em] text-face-muted">
                        {loadedName} — {entries.length} entries
                        {ctx ? ` · agent=${ctx.agent}` : ""}
                    </span>
                ) : null}
            </Panel>

            {status ? (
                <p className="m-0 px-1 text-[0.72em] leading-snug text-face-muted">{status}</p>
            ) : null}

            <Panel className="flex min-h-[200px] flex-1 flex-col gap-0 overflow-hidden p-0">
                <div className="border-b border-face-border px-2 py-1 text-[0.68em] font-semibold uppercase tracking-wide text-face-muted">
                    Entries
                </div>
                <ul className="m-0 min-h-0 flex-1 list-none overflow-y-auto p-0">
                    {entries.length === 0 ? (
                        <li className="px-2 py-3 text-[0.72em] text-face-muted">
                            Select a log file to inspect entries.
                        </li>
                    ) : (
                        entries.map((entry, i) => {
                            const active = selectedIndex === i;
                            const duringReplay = replayHighlight === i;
                            return (
                                <li key={i}>
                                    <button
                                        type="button"
                                        className={
                                            active || duringReplay
                                                ? "w-full border-b border-face-border/50 bg-face-panel-2 px-2 py-1.5 text-left text-[0.72em] text-face-text"
                                                : "w-full border-b border-face-border/50 px-2 py-1.5 text-left text-[0.72em] text-face-text hover:bg-face-panel-2/60"
                                        }
                                        onClick={() => onSelectIndex(i)}
                                    >
                                        {formatEntrySummary(entry, i)}
                                    </button>
                                </li>
                            );
                        })
                    )}
                </ul>
            </Panel>
        </div>
    );
}

export function LogEntryInspector({ entry }: { entry: TurnLogEntry | null }): JSX.Element {
    if (!entry) {
        return (
            <Panel>
                <p className="m-0 text-[0.72em] text-face-muted">Select an entry to inspect payload and parsed events.</p>
            </Panel>
        );
    }

    return (
        <Panel className="flex min-h-0 flex-1 flex-col gap-2 overflow-hidden">
            <h4 className="m-0 text-[0.72em] font-bold uppercase tracking-wide text-face-muted">
                Entry detail
            </h4>
            <div className="text-[0.72em] text-face-text">
                <div>
                    <span className="text-face-muted">hook_type:</span> {entry.hook_type}
                </div>
                <div>
                    <span className="text-face-muted">ts:</span> {entry.ts}
                    {entry.delta_ms !== undefined ? ` (+${entry.delta_ms}ms)` : ""}
                </div>
                <div>
                    <span className="text-face-muted">parsed:</span> {entry.parsed.length} event(s)
                </div>
            </div>
            <details className="text-[0.72em] text-face-text" open>
                <summary className="cursor-pointer text-face-muted">Parsed events</summary>
                <pre className="mt-1 max-h-[40vh] overflow-auto rounded border border-face-border bg-face-canvas p-2 font-mono text-[0.68em] leading-snug text-face-text">
                    {JSON.stringify(entry.parsed, null, 2)}
                </pre>
            </details>
            <details className="text-[0.72em] text-face-text">
                <summary className="cursor-pointer text-face-muted">Raw payload</summary>
                <pre className="mt-1 max-h-[40vh] overflow-auto rounded border border-face-border bg-face-canvas p-2 font-mono text-[0.68em] leading-snug text-face-text">
                    {JSON.stringify(entry.payload, null, 2)}
                </pre>
            </details>
        </Panel>
    );
}
