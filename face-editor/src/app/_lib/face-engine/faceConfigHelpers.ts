import type { ArmPreset } from "./faceConfigTypes";
import { type ExpressionName, expressionIndexFromName } from "./faceConfigTypes";
import {
    kArmPresets,
    kExpressionIsEmotion,
    kNamedEmotionToExpressionIndex,
} from "./FACE_CONFIG_DATA";

export { expressionIndexFromName };

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

export type { ExpressionName };
