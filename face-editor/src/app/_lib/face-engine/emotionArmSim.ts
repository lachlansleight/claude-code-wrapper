/**
 * Emotion-arm sweep — mirrors `robot_v3/src/hal/Motion.cpp` emotion layer and
 * `MotionBehaviors::tick` period/interval scaling.
 */

import { FieldIndex, type ParamI16 } from "./faceConfigTypes";

export interface ArmSweepParams {
    lo: number;
    hi: number;
    periodS: number;
    intervalS: number;
}

/** Read arm fields with the same clamps as firmware. */
export function armSweepParams(effective: readonly ParamI16[]): ArmSweepParams {
    let lo = effective[FieldIndex.ArmMinDeg]!.value;
    let hi = effective[FieldIndex.ArmMaxDeg]!.value;
    if (lo > hi) {
        const t = lo;
        lo = hi;
        hi = t;
    }
    const periodMs = effective[FieldIndex.ArmPeriodMs]!.value;
    const intervalMs = effective[FieldIndex.ArmIntervalMs]!.value;
    const periodS = periodMs < 50 ? 0.05 : periodMs / 1000;
    const intervalS = intervalMs < 0 ? 0 : intervalMs / 1000;
    return { lo, hi, periodS, intervalS };
}

/** Servo offset (deg) at `elapsedS` along repeating arch + dwell cycles. */
export function armOffsetDegFromElapsed(elapsedS: number, effective: readonly ParamI16[]): number {
    const { lo, hi, periodS, intervalS } = armSweepParams(effective);
    if (lo === hi) return lo;

    const dwellS = intervalS < 0.02 ? 0 : intervalS;
    const cycleLen = periodS + dwellS;
    let cycleT = elapsedS % cycleLen;
    if (cycleT < 0) cycleT += cycleLen;

    if (cycleT < periodS) {
        const oscDraw = cycleT / periodS;
        const u = Math.sin(Math.PI * oscDraw);
        return lo + (hi - lo) * u;
    }
    return lo;
}

export interface EmotionArmPhaseState {
    inOsc: boolean;
    osc01: number;
    dwellS: number;
}

export function createEmotionArmPhaseState(): EmotionArmPhaseState {
    return { inOsc: true, osc01: 0, dwellS: 0 };
}

export function resetEmotionArmPhaseState(s: EmotionArmPhaseState): void {
    s.inOsc = true;
    s.osc01 = 0;
    s.dwellS = 0;
}

/** Advance real-time phase by `dt` seconds (firmware `Motion::tick` emotion path). */
export function tickEmotionArmPhase(
    s: EmotionArmPhaseState,
    dt: number,
    effective: readonly ParamI16[]
): number {
    const { lo, hi, periodS, intervalS } = armSweepParams(effective);
    if (lo === hi) return lo;

    if (s.inOsc) {
        s.osc01 += dt / periodS;
        const oscDraw = s.osc01 >= 1 ? 1 : s.osc01;
        if (s.osc01 >= 1) {
            s.osc01 = 0;
            if (intervalS < 0.02) {
                /* immediate next arch */
            } else {
                s.inOsc = false;
                s.dwellS = intervalS;
            }
        }
        const u = Math.sin(Math.PI * oscDraw);
        return lo + (hi - lo) * u;
    }

    s.dwellS -= dt;
    if (s.dwellS <= 0) {
        s.inOsc = true;
        s.osc01 = 0;
    }
    return lo;
}
