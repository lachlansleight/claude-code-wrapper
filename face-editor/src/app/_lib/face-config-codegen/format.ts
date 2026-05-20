import { BOB_AMP_FOLLOW_EMOTION_ARM, FieldIndex } from "../face-engine/faceConfigTypes";
import type { ParamI16 } from "../face-engine/faceConfigTypes";

const SIGNED_PARAM_INDICES = new Set([3, 14]);

export function expressionToFirmwareSlug(name: string): string {
    return name.replace(/([a-z0-9])([A-Z])/g, "$1_$2").toLowerCase();
}

export function namedEmotionEnumName(emotionSlug: string): string {
    return emotionSlug.charAt(0).toUpperCase() + emotionSlug.slice(1);
}

export function fieldIndexEnumName(field: number): string {
    const name = FieldIndex[field];
    if (!name || name === "Count") {
        throw new Error(`Invalid FieldIndex value: ${field}`);
    }
    return name;
}

/**
 * C++ `float` field initializers that use unary +/- (e.g. emotion V/A points).
 * Always includes a `.` before `f` so lexers never split `123` + user-defined `f`
 * (Arduino / some GCC builds treat `1f` / `6000f` as `operator""f`).
 */
export function fmtCppFloat(v: number): string {
    const s = v.toFixed(6).replace(/\.?0+$/, "");
    if (s === "-0") return "+0.0f";
    const n = Number(s);
    const body = s.includes(".") ? s : `${s}.0`;
    if (n > 0) return `+${body}f`;
    if (n < 0) return `${body}f`;
    return "+0.0f";
}

/** Plain `float` literal for struct fields (no leading `+`). */
export function fmtCppFloatLiteral(v: number): string {
    const n = Number(v);
    if (!Number.isFinite(n)) return "0.0f";
    const s = n.toFixed(6).replace(/\.?0+$/, "");
    if (s === "-0") return "0.0f";
    const body = s.includes(".") ? s : `${s}.0`;
    return `${body}f`;
}

export function fmtTsFloat(v: number): string {
    const n = Math.round(v * 1e6) / 1e6;
    return Object.is(n, -0) ? "0.0" : String(n);
}

function fmtParamValue(n: number, i: number): string {
    if (n > 0 && SIGNED_PARAM_INDICES.has(i)) return `+${n}`;
    return String(n);
}

export function emitFacePRowCpp(exprName: string, row: readonly ParamI16[]): string {
    const vals = row.map(c => c.value | 0);
    const head = `    /* ${exprName} */`;
    const headPad = Math.max(1, 27 - head.length);
    const headPart = head + " ".repeat(headPad);
    const ind = " ".repeat(30);
    const l1 =
        headPart +
        "{ " +
        [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
            .map(i => `FACE_P(${fmtParamValue(vals[i]!, i)})`)
            .join(", ") +
        ",\n";
    const l2 =
        ind +
        [11, 12, 13, 14, 15, 16, 17, 18]
            .map(i => `FACE_P(${fmtParamValue(vals[i]!, i)})`)
            .join(", ") +
        ",\n";
    const l3 =
        ind +
        [19, 20, 21, 22, 23, 24, 25, 26, 27]
            .map(i => `FACE_P(${fmtParamValue(vals[i]!, i)})`)
            .join(", ") +
        " },\n";
    return l1 + l2 + l3;
}

export function emitParamIRowTs(row: readonly ParamI16[], indent: string): string {
    const lines = row.map(c => `${indent}P(${c.value}, ${c.strength}),`);
    return `[\n${lines.join("\n")}\n${indent.slice(0, -2)}]`;
}

export function bobAmpTsLiteral(px: number): string {
    return px === BOB_AMP_FOLLOW_EMOTION_ARM ? "BOB_AMP_FOLLOW_EMOTION_ARM" : String(px);
}

export function bobAmpCppLiteral(px: number): string {
    return px === BOB_AMP_FOLLOW_EMOTION_ARM ? "kBobAmpFollowEmotionArm" : String(px);
}
