/** Parse bridge turn logs (JSONL) and replay via HTTP hooksRaw — ported from control/index.html. */

import { postRaw } from "./bridge";

export interface ParsedLogEvent {
    event: { kind: string; [key: string]: unknown };
    session_id?: string;
    turn_id?: string;
}

export interface TurnLogEntry {
    ts: number;
    delta_ms?: number;
    agent: string;
    hook_type: string;
    payload: unknown;
    parsed: ParsedLogEvent[];
}

export function parseLogText(text: string): TurnLogEntry[] {
    const entries: TurnLogEntry[] = [];
    for (const line of text.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!trimmed) continue;
        try {
            const raw = JSON.parse(trimmed) as Record<string, unknown>;
            entries.push({
                ts: typeof raw.ts === "number" ? raw.ts : 0,
                delta_ms: typeof raw.delta_ms === "number" ? raw.delta_ms : undefined,
                agent: typeof raw.agent === "string" ? raw.agent : "cursor",
                hook_type: typeof raw.hook_type === "string" ? raw.hook_type : "",
                payload: raw.payload ?? null,
                parsed: Array.isArray(raw.parsed)
                    ? (raw.parsed as ParsedLogEvent[]).filter(
                          p => p && typeof p === "object" && p.event && typeof p.event.kind === "string"
                      )
                    : [],
            });
        } catch {
            // skip bad lines
        }
    }
    return entries;
}

export function resolveReplayContext(entries: TurnLogEntry[]): {
    agent: string;
    sessionId: string;
} {
    let agent = entries[0]?.agent || "cursor";
    let sessionId = `replay_${Date.now()}`;

    outer: for (const entry of entries) {
        if (entry.agent) agent = entry.agent;
        for (const pe of entry.parsed) {
            if (pe.session_id) {
                sessionId = pe.session_id;
                break outer;
            }
        }
        const p = (entry.payload ?? {}) as Record<string, unknown>;
        const sid =
            typeof p.conversation_id === "string" && p.conversation_id
                ? p.conversation_id
                : typeof p.session_id === "string" && p.session_id
                  ? p.session_id
                  : "";
        if (sid) sessionId = sid;
    }

    return { agent, sessionId };
}

export interface LogReplayOptions {
    speed?: number;
    leadMs?: number;
    onStatus?: (msg: string) => void;
    onEntryIndex?: (index: number) => void;
    onFinished?: () => void;
}

export function scheduleLogReplay(
    entries: TurnLogEntry[],
    options: LogReplayOptions = {}
): { cancel: () => void } {
    const speed = Math.max(0.1, options.speed ?? 1);
    const leadMs = options.leadMs ?? 1000;
    const timers: ReturnType<typeof setTimeout>[] = [];
    const { agent, sessionId } = resolveReplayContext(entries);
    const baseTs = entries[0]?.ts ?? Date.now();

    const cancel = () => {
        timers.forEach(t => clearTimeout(t));
        timers.length = 0;
    };

    const emit = (events: ParsedLogEvent[]) => {
        if (events.length === 0) return;
        void postRaw(`/hooksRaw/${encodeURIComponent(agent)}`, { events });
    };

    options.onStatus?.(
        `Replaying ${entries.length} entries at ${speed}× (session.started now, hooks after +${leadMs}ms) …`
    );

    emit([
        {
            event: { kind: "session.started", cause: "startup" },
            session_id: sessionId,
        },
    ]);

    entries.forEach((entry, i) => {
        const offset = (entry.ts - baseTs) / speed + leadMs;
        const t = setTimeout(() => {
            if (entry.parsed.length === 0) {
                options.onStatus?.(`(skip) #${i} hook_type=${entry.hook_type} — no parsed events`);
            } else {
                emit(entry.parsed);
            }
            options.onEntryIndex?.(i);
            if (i === entries.length - 1) {
                options.onStatus?.("Replay finished.");
                options.onFinished?.();
            }
        }, offset);
        timers.push(t);
    });

    return { cancel };
}

export function formatEntrySummary(entry: TurnLogEntry, index: number): string {
    const kinds = entry.parsed.map(p => p.event.kind).join(", ") || "(no parsed)";
    const rel = entry.delta_ms !== undefined ? `+${entry.delta_ms}ms` : "";
    return `#${index} ${entry.hook_type} ${rel} → ${kinds}`;
}
