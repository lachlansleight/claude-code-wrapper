import type { FaceConfigState } from "../face-engine/faceConfigState";
import { FieldIndex, P, type ParamI16 } from "../face-engine/faceConfigTypes";

/** Legacy v2 arm table row (removed from schema v3). */
interface LegacyArmPreset {
    min_deg: number;
    max_deg: number;
    period_s: number;
    interval_s: number;
}

const FIELD_COUNT = FieldIndex.Count;

function armCellsFromPreset(p: LegacyArmPreset): ParamI16[] {
    const periodMs = Math.max(50, Math.round(p.period_s * 1000));
    const intervalMs = Math.max(0, Math.round(p.interval_s * 1000));
    return [P(p.min_deg), P(p.max_deg), P(periodMs), P(intervalMs)];
}

function defaultArmCells(): ParamI16[] {
    return [P(0), P(0), P(0), P(0)];
}

/** v2 → v3: fold `armPresets` into `baseTargets`; drop `motion` tables. */
export function migrateFaceConfigToSchemaV3(config: FaceConfigState): FaceConfigState {
    if ((config.schemaVersion ?? 2) >= 3) {
        const baseTargets = config.baseTargets.map(row => {
            if (row.length >= FIELD_COUNT) return row.map(c => ({ ...c }));
            const pad = defaultArmCells();
            return [...row.map(c => ({ ...c })), ...pad].slice(0, FIELD_COUNT);
        });
        const { armPresets: _a, motion: _m, motionRuntime: _r, ...rest } = config as FaceConfigState & {
            armPresets?: unknown;
            motion?: unknown;
            motionRuntime?: unknown;
        };
        return { ...rest, schemaVersion: 3, baseTargets };
    }

    const armPresets = (config as FaceConfigState & { armPresets?: LegacyArmPreset[] }).armPresets ?? [];
    const baseTargets = config.baseTargets.map((row, i) => {
        const cells = row.map(c => ({ value: c.value, strength: c.strength }));
        if (cells.length >= FIELD_COUNT) return cells.slice(0, FIELD_COUNT);
        const preset = armPresets[i];
        const arm = preset ? armCellsFromPreset(preset) : defaultArmCells();
        return [...cells, ...arm].slice(0, FIELD_COUNT);
    });

    const { armPresets: _ap, motion: _mo, motionRuntime: _mr, ...rest } = config as FaceConfigState & {
        armPresets?: unknown;
        motion?: unknown;
        motionRuntime?: unknown;
    };

    return {
        ...rest,
        schemaVersion: 3,
        baseTargets,
    };
}
