import type { ArmPreset } from "./faceConfigTypes";
import {
    EXPRESSIONS,
    kArmPresets,
    kExpressionIsEmotion,
    kNamedEmotionToExpressionIndex,
} from "./FACE_CONFIG_DATA";

export type ExpressionName = (typeof EXPRESSIONS)[number];

export function expressionIndexFromName(name: string): number {
    const i = (EXPRESSIONS as readonly string[]).indexOf(name);
    return i >= 0 ? i : 0;
}

export function isEmotionExpressionIndex(idx: number): boolean {
    return idx >= 0 && idx < kExpressionIsEmotion.length && !!kExpressionIsEmotion[idx];
}

export function armPresetForExpressionIndex(idx: number): ArmPreset {
    if (idx < 0 || idx >= kArmPresets.length) return kArmPresets[0]!;
    return kArmPresets[idx]!;
}

export function expressionForNamedEmotionIndex(namedIdx: number): number {
    if (namedIdx < 0 || namedIdx >= kNamedEmotionToExpressionIndex.length) return 0;
    return kNamedEmotionToExpressionIndex[namedIdx]!;
}

export function isEmotionExpressionName(name: string): boolean {
    return isEmotionExpressionIndex(expressionIndexFromName(name));
}
