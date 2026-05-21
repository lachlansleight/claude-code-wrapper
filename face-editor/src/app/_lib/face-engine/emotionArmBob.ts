/**
 * Head-bob mapping from emotion-arm sweep — mirrors `FrameController.cpp` /
 * `Motion.cpp` bob path (virtual range expansion only; servo uses real min/max).
 */

import { FieldIndex, type ParamI16 } from "./faceConfigTypes";
import { armOscSinUFromElapsed, type EmotionArmPhaseState, emotionArmOscSinU } from "./emotionArmSim";

/** Minimum arm span (deg) used for bob normalization when span is narrower. */
export const kBobArmGapMinDeg = 6;

export function sortedArmLoHi(effective: readonly ParamI16[]): { lo: number; hi: number } {
    let lo = effective[FieldIndex.ArmMinDeg]!.value;
    let hi = effective[FieldIndex.ArmMaxDeg]!.value;
    if (lo > hi) {
        const t = lo;
        lo = hi;
        hi = t;
    }
    return { lo, hi };
}

/** Virtual bob mapping range: pad symmetrically so span is 6° when span < 6°. */
export function bobArmMappingRange(lo: number, hi: number): { bobLo: number; bobHi: number } {
    const gap = hi - lo;
    if (gap >= kBobArmGapMinDeg) return { bobLo: lo, bobHi: hi };
    const pad = (kBobArmGapMinDeg - gap) * 0.5;
    return { bobLo: lo - pad, bobHi: hi + pad };
}

export function bodyBobPxFromArm(
    armDeg: number,
    effective: readonly ParamI16[],
    amp: number,
    armPhase?: EmotionArmPhaseState,
    verbElapsedS?: number
): number {
    if (amp === 0) return 0;
    const { lo, hi } = sortedArmLoHi(effective);
    const { bobLo, bobHi } = bobArmMappingRange(lo, hi);
    const span = bobHi - bobLo;
    if (span <= 0) return 0;

    let u: number;
    if (lo === hi) {
        if (verbElapsedS !== undefined) {
            u = armOscSinUFromElapsed(verbElapsedS, effective);
        } else if (armPhase) {
            u = emotionArmOscSinU(armPhase);
        } else {
            return 0;
        }
    } else {
        u = (armDeg - bobLo) / span;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
    }
    return Math.round(amp * (2 * u - 1));
}
