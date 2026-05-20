import fs from "fs";
import path from "path";
import { NextRequest, NextResponse } from "next/server";
import { repoRootFromCwd } from "../../_lib/face-config-codegen/paths";

export const runtime = "nodejs";

const LOGS_ROOT = () => path.join(repoRootFromCwd(), "plugin/logs");

export type BridgeLogFileInfo = {
    name: string;
    agent: string;
    size: number;
    mtimeMs: number;
};

export type BridgeLogsListResponse =
    | { ok: true; agents: string[]; files: BridgeLogFileInfo[] }
    | { ok: false; error: string };

export type BridgeLogsReadResponse =
    | { ok: true; agent: string; name: string; content: string }
    | { ok: false; error: string };

function resolveLogFile(agent: string, file: string): string | null {
    if (!agent || !file || file.includes("..") || path.isAbsolute(file)) return null;
    if (!file.endsWith(".log")) return null;
    const agentDir = path.resolve(LOGS_ROOT(), agent);
    const full = path.resolve(agentDir, file);
    if (!full.startsWith(agentDir + path.sep)) return null;
    return full;
}

function listLogFiles(): BridgeLogFileInfo[] {
    const root = LOGS_ROOT();
    if (!fs.existsSync(root)) return [];

    const files: BridgeLogFileInfo[] = [];
    for (const agent of fs.readdirSync(root, { withFileTypes: true })) {
        if (!agent.isDirectory()) continue;
        const agentDir = path.join(root, agent.name);
        for (const ent of fs.readdirSync(agentDir, { withFileTypes: true })) {
            if (!ent.isFile() || !ent.name.endsWith(".log")) continue;
            const full = path.join(agentDir, ent.name);
            const st = fs.statSync(full);
            files.push({
                name: ent.name,
                agent: agent.name,
                size: st.size,
                mtimeMs: st.mtimeMs,
            });
        }
    }
    files.sort((a, b) => b.mtimeMs - a.mtimeMs);
    return files;
}

export async function GET(req: NextRequest): Promise<NextResponse> {
    try {
        const agent = req.nextUrl.searchParams.get("agent");
        const file = req.nextUrl.searchParams.get("file");

        if (agent && file) {
            const full = resolveLogFile(agent, file);
            if (!full || !fs.existsSync(full)) {
                return NextResponse.json(
                    { ok: false, error: "log_not_found" } satisfies BridgeLogsReadResponse,
                    { status: 404 }
                );
            }
            const content = fs.readFileSync(full, "utf8");
            return NextResponse.json({
                ok: true,
                agent,
                name: file,
                content,
            } satisfies BridgeLogsReadResponse);
        }

        const files = listLogFiles();
        const agents = Array.from(new Set(files.map(f => f.agent))).sort();
        return NextResponse.json({ ok: true, agents, files } satisfies BridgeLogsListResponse);
    } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        console.error("[bridgeLogs]", message, e);
        return NextResponse.json({ ok: false, error: message }, { status: 500 });
    }
}
