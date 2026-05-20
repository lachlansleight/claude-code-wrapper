/**
 * Port of `robot_v3/src/face/SceneTypes.cpp` — `combineEmotionVerbField`,
 * `combineEmotionVerbFace`, `smoothFaceValuesToward` (ParamI16 paths).
 */

import { FieldIndex, type ParamI16 } from "./faceConfigTypes";

const FIELD_COUNT = FieldIndex.Count;

function fieldRef(p: ParamI16[], i: FieldIndex): ParamI16 {
    return p[i]!;
}

function fieldSet(p: ParamI16[], i: FieldIndex, v: ParamI16): void {
    p[i] = v;
}

export function combineEmotionVerbField(e: ParamI16, hasVerb: boolean, v: ParamI16): ParamI16 {
    if (!hasVerb) return { ...e };

    const se = e.strength;
    const sv = v.strength;

    if (se === 0 && sv === 0) return { value: 0, strength: 0 };
    if (sv === 0) return { ...e };
    if (se === 0) return { ...v };

    const t = sv / 100.0;
    const kMaxPower = 5.0;

    let factor: number;
    if (se === 50) {
        factor = t;
    } else if (se < 50) {
        const power = 1.0 + ((50.0 - se) / 50.0) * (kMaxPower - 1.0);
        factor = 1.0 - (1.0 - t) ** power;
    } else {
        const power = 1.0 + ((se - 50.0) / 50.0) * (kMaxPower - 1.0);
        factor = t ** power;
    }

    let outStrength = Math.round(se > sv ? se : sv);
    if (outStrength > 100) outStrength = 100;
    return {
        value: Math.round(e.value + (v.value - e.value) * factor),
        strength: outStrength,
    };
}

export function combineEmotionVerbFace(
    emotion: readonly ParamI16[],
    verbHas: boolean[],
    verbVals: readonly ParamI16[]
): ParamI16[] {
    const out: ParamI16[] = [];
    for (let i = 0; i < FIELD_COUNT; ++i) {
        const fi = i as FieldIndex;
        out.push(
            combineEmotionVerbField(fieldRef(emotion as ParamI16[], fi), !!verbHas[i], verbVals[i]!)
        );
    }
    return out;
}

export function smoothFaceValuesToward(
    state: ParamI16[],
    target: readonly ParamI16[],
    alpha: number
): void {
    let a = alpha;
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    for (let i = 0; i < FIELD_COUNT; ++i) {
        const fi = i as FieldIndex;
        const s = fieldRef(state, fi);
        const t = fieldRef(target as ParamI16[], fi);
        fieldSet(state, fi, {
            strength: t.strength,
            value: Math.round(s.value + (t.value - s.value) * a),
        });
    }
}

export function cloneFaceParamsIndexed(src: readonly ParamI16[]): ParamI16[] {
    return src.map(x => ({ ...x }));
}
