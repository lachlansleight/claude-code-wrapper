import type { FaceConfigState } from "../face-engine/faceConfigState";
import {
    SYSTEM_VERB_EXPRESSION_NAMES,
    validateUniqueSlugs,
    expressionNameToVerbSlug,
} from "../face-engine/faceSchema";

const MAX_EMOTIONS = 32;
const MAX_VERBS = 24;
const MAX_EXPRESSIONS = 64;

export function validateFaceConfigState(config: FaceConfigState): string | null {
    if (config.expressions.length !== config.expressionIsEmotion.length) {
        return "expressions and expressionIsEmotion length mismatch";
    }
    if (config.expressions.length > MAX_EXPRESSIONS) {
        return `at most ${MAX_EXPRESSIONS} expressions`;
    }
    if (config.baseTargets.length !== config.expressions.length) {
        return "baseTargets row count must match expressions";
    }
    if (config.armPresets.length !== config.expressions.length) {
        return "armPresets row count must match expressions";
    }
    if (config.motion.length !== config.expressions.length) {
        return "motion row count must match expressions";
    }
    if (config.idleAnim.length !== config.expressions.length) {
        return "idleAnim row count must match expressions";
    }

    if (config.emotionNames.length !== config.emotionPoints.length) {
        return "emotionNames and emotionPoints length mismatch";
    }
    if (config.emotionNames.length < 3) {
        return "need at least 3 emotion points for triangulation";
    }
    if (config.emotionNames.length > MAX_EMOTIONS) {
        return `at most ${MAX_EMOTIONS} emotions`;
    }
    if (!config.emotionNames.includes("neutral")) {
        return 'emotion "neutral" is required';
    }

    const verbSlugs: string[] = [];
    for (let i = 0; i < config.expressions.length; i++) {
        if (config.expressionIsEmotion[i]) continue;
        const slug = expressionNameToVerbSlug(config.expressions[i]!);
        if (slug) verbSlugs.push(slug);
    }
    if (verbSlugs.length > MAX_VERBS) {
        return `at most ${MAX_VERBS} verbs`;
    }

    const slugErr = validateUniqueSlugs(config.emotionNames, verbSlugs);
    if (slugErr) return slugErr;

    if (config.pickOrderIndices.length !== config.emotionNames.length) {
        return "pickOrderIndices length must match emotionNames";
    }
    if (config.namedEmotionToExpressionIndex.length !== config.emotionNames.length) {
        return "namedEmotionToExpressionIndex length must match emotionNames";
    }

    const pickSet = new Set(config.pickOrderIndices);
    if (pickSet.size !== config.emotionNames.length) {
        return "pickOrderIndices must be a permutation of emotion indices";
    }
    for (let i = 0; i < config.emotionNames.length; i++) {
        if (!pickSet.has(i)) return "pickOrderIndices must include every emotion index";
    }

    for (const name of SYSTEM_VERB_EXPRESSION_NAMES) {
        if (!config.expressions.includes(name)) {
            return `missing required system verb expression "${name}"`;
        }
        const idx = config.expressions.indexOf(name);
        if (!config.verbTimelines.some(t => t.verb === idx)) {
            return `missing verb timeline for "${name}"`;
        }
    }

    for (const tab of config.verbTimelines) {
        const exprName = config.expressions[tab.verb];
        if (!exprName) return `verb timeline references invalid expression index ${tab.verb}`;
        if (config.expressionIsEmotion[tab.verb]) {
            return `verb timeline must reference a verb expression, not "${exprName}"`;
        }
        if (tab.keyframe_count !== tab.keyframes.length) {
            return `verb ${exprName}: keyframe_count !== keyframes.length`;
        }
        if (tab.keyframes.length > config.verbKeyframesMax) {
            return `verb ${exprName}: too many keyframes (max ${config.verbKeyframesMax})`;
        }
        for (const kf of tab.keyframes) {
            if (kf.override_count !== kf.overrides.length) {
                return `verb ${exprName} @${kf.time_ms}ms: override_count mismatch`;
            }
            if (kf.overrides.length > config.verbKeyframeOverridesMax) {
                return `verb ${exprName} @${kf.time_ms}ms: too many overrides`;
            }
        }
    }

    for (let i = 0; i < config.expressions.length; i++) {
        const row = config.baseTargets[i];
        if (!row || row.length !== 24) {
            return `baseTargets[${i}] must have 24 ParamI16 cells`;
        }
    }

    return null;
}
