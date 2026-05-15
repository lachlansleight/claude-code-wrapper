import { NextResponse } from "next/server";
import { emitAllFaceConfigArtifacts, repoRootFromCwd } from "../../_lib/face-config-codegen";
import type { FaceConfigState } from "../../_lib/face-engine/faceConfigState";

export const runtime = "nodejs";

export type SaveDataResponse =
    | { ok: true; filesWritten: string[]; config: FaceConfigState }
    | { ok: false; error: string };

export async function POST(req: Request): Promise<NextResponse<SaveDataResponse>> {
    try {
        const body = (await req.json()) as { config?: FaceConfigState };
        if (!body?.config) {
            return NextResponse.json(
                { ok: false, error: "Missing config in request body" },
                { status: 400 }
            );
        }

        const { filesWritten, config } = emitAllFaceConfigArtifacts(repoRootFromCwd(), body.config);

        return NextResponse.json({ ok: true, filesWritten, config });
    } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        console.error("[saveData]", message, e);
        return NextResponse.json({ ok: false, error: message }, { status: 500 });
    }
}
