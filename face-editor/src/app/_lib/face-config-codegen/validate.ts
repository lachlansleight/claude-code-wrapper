import { EXPRESSIONS } from "../face-engine/faceConfigTypes";
import type { FaceConfigState } from "../face-engine/faceConfigState";

export function validateFaceConfigState(config: FaceConfigState): string | null {
    if (config.expressions.length !== EXPRESSIONS.length) {
        return `expressions.length must be ${EXPRESSIONS.length}`;
    }
    if (config.expressionIsEmotion.length !== EXPRESSIONS.length) {
        return `expressionIsEmotion.length must be ${EXPRESSIONS.length}`;
    }
    if (config.emotionNames.length !== config.emotionPoints.length) {
        return "emotionNames and emotionPoints length mismatch";
    }
    if (config.emotionNames.length < 3) {
        return "need at least 3 emotion points for triangulation";
    }
    if (config.baseTargets.length !== EXPRESSIONS.length) {
        return `baseTargets must have ${EXPRESSIONS.length} rows`;
    }
    for (let i = 0; i < config.baseTargets.length; i++) {
        const row = config.baseTargets[i];
        if (!row || row.length !== 24) {
            return `baseTargets[${i}] must have 24 ParamI16 cells`;
        }
    }
    if (config.armPresets.length !== EXPRESSIONS.length) {
        return `armPresets must have ${EXPRESSIONS.length} rows`;
    }
    if (config.motion.length !== EXPRESSIONS.length) {
        return `motion must have ${EXPRESSIONS.length} rows`;
    }
    if (config.idleAnim.length !== EXPRESSIONS.length) {
        return `idleAnim must have ${EXPRESSIONS.length} rows`;
    }
    if (config.pickOrderIndices.length !== config.emotionNames.length) {
        return "pickOrderIndices length must match emotionNames";
    }
    if (config.namedEmotionToExpressionIndex.length !== config.emotionNames.length) {
        return "namedEmotionToExpressionIndex length must match emotionNames";
    }

    for (const tab of config.verbTimelines) {
        if (tab.keyframe_count !== tab.keyframes.length) {
            return `verb ${tab.verb}: keyframe_count !== keyframes.length`;
        }
        if (tab.keyframes.length > config.verbKeyframesMax) {
            return `verb ${tab.verb}: too many keyframes (max ${config.verbKeyframesMax})`;
        }
        for (const kf of tab.keyframes) {
            if (kf.override_count !== kf.overrides.length) {
                return `verb ${tab.verb} @${kf.time_ms}ms: override_count mismatch`;
            }
            if (kf.overrides.length > config.verbKeyframeOverridesMax) {
                return `verb ${tab.verb} @${kf.time_ms}ms: too many overrides`;
            }
        }
    }

    return null;
}
