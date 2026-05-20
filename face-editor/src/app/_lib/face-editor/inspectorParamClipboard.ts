import { PARAM_FIELDS, type FaceParams, type ParamField } from "../face-engine/faceParams";

/** Clipboard JSON: parameter name → number (values or strengths). */
export type ParamValueRecord = Record<string, number>;

export function faceParamsToParamRecord(params: FaceParams): ParamValueRecord {
    const out: ParamValueRecord = {};
    for (const f of PARAM_FIELDS) {
        out[f] = params[f] ?? 0;
    }
    return out;
}

export function parseParamValueRecord(
    raw: string
): { ok: true; record: ParamValueRecord } | { ok: false; error: string } {
    const trimmed = raw.trim();
    if (!trimmed) {
        return { ok: false, error: "Clipboard is empty." };
    }
    let parsed: unknown;
    try {
        parsed = JSON.parse(trimmed);
    } catch {
        return { ok: false, error: "Clipboard is not valid JSON." };
    }
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
        return { ok: false, error: "Expected a JSON object (Record<string, number>)." };
    }
    const obj = parsed as Record<string, unknown>;
    const record: ParamValueRecord = {};
    let found = 0;
    for (const f of PARAM_FIELDS) {
        if (!(f in obj)) continue;
        const v = obj[f];
        if (typeof v !== "number" || !Number.isFinite(v)) {
            return { ok: false, error: `Invalid number for "${f}".` };
        }
        record[f] = v;
        found++;
    }
    if (found === 0) {
        return { ok: false, error: "No recognized parameter names in JSON." };
    }
    return { ok: true, record };
}

export async function copyParamRecordToClipboard(record: ParamValueRecord): Promise<void> {
    await navigator.clipboard.writeText(JSON.stringify(record, null, 2));
}

export async function readParamRecordFromClipboard(): Promise<
    { ok: true; record: ParamValueRecord } | { ok: false; error: string }
> {
    let text: string;
    try {
        text = await navigator.clipboard.readText();
    } catch {
        return { ok: false, error: "Could not read clipboard (permission denied)." };
    }
    return parseParamValueRecord(text);
}

export function paramRecordToPartialFaceParams(
    record: ParamValueRecord
): Partial<FaceParams> {
    const out: Partial<FaceParams> = {};
    for (const f of PARAM_FIELDS) {
        if (f in record) out[f] = record[f]!;
    }
    return out;
}
