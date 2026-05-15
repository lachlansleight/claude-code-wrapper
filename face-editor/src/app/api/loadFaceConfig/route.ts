import { NextResponse } from "next/server";
import { buildFaceConfigStateFromSource } from "../../_lib/face-engine/mutableFaceConfig";
import type { FaceConfigState } from "../../_lib/face-engine/faceConfigState";

export const runtime = "nodejs";

export type LoadFaceConfigResponse =
    | { ok: true; config: FaceConfigState }
    | { ok: false; error: string };

export async function GET(): Promise<NextResponse<LoadFaceConfigResponse>> {
    try {
        const config = buildFaceConfigStateFromSource();
        return NextResponse.json({ ok: true, config });
    } catch (e) {
        const message = e instanceof Error ? e.message : String(e);
        console.error("[loadFaceConfig]", message, e);
        return NextResponse.json({ ok: false, error: message }, { status: 500 });
    }
}
