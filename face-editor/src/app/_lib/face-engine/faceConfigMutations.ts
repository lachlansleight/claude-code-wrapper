import type { FaceConfigState } from "./faceConfigState";
import {
    buildEmotionTriangulationFromPoints,
    namedEmotionAnchorLabel,
} from "./emotionTriangulationLive";
import type { ParamI16 } from "./faceConfigTypes";
import { P } from "./faceConfigTypes";
import {
    emotionSlugToExpressionName,
    isSystemVerbExpression,
    SYSTEM_VERB_EXPRESSION_NAMES,
    validateSlug,
    verbSlugToExpressionName,
} from "./faceSchema";
import type { MutableVerbTimeline } from "./mutableVerbTimelines";

const EMPTY_PARAM_ROW = (): ParamI16[] => Array.from({ length: 28 }, () => P(0, 0));

function cloneRow(row: ParamI16[]): ParamI16[] {
    return row.map(c => ({ value: c.value, strength: c.strength }));
}

function defaultEmotionRow(config: FaceConfigState): ParamI16[] {
    const neutralIdx = config.namedEmotionToExpressionIndex[0] ?? 0;
    const src = config.baseTargets[neutralIdx];
    return src ? cloneRow(src) : EMPTY_PARAM_ROW();
}

function defaultVerbIdleRow(config: FaceConfigState) {
    const thinking = config.expressions.findIndex(n => n === "VerbThinking");
    const idx = thinking >= 0 ? thinking : 0;
    return { ...config.idleAnim[idx]! };
}

function defaultVerbTimeline(expressionIndex: number): MutableVerbTimeline {
    return {
        verb: expressionIndex,
        loop_duration_ms: 4000,
        keyframe_count: 1,
        keyframes: [
            {
                time_ms: 0,
                override_count: 0,
                overrides: [],
            },
        ],
    };
}

function remapIndicesAfterRemove(indices: number[], removedAt: number): number[] {
    return indices.map(i => (i > removedAt ? i - 1 : i));
}

/** Remove one emotion; reindexes expressions and verb timeline verb indices. */
export function removeEmotion(
    config: FaceConfigState,
    emotionIndex: number
): { config: FaceConfigState } | { error: string } {
    if (emotionIndex < 0 || emotionIndex >= config.emotionNames.length) {
        return { error: "invalid emotion index" };
    }
    if (config.emotionNames.length <= 3) {
        return { error: "need at least 3 emotions for triangulation" };
    }
    const slug = config.emotionNames[emotionIndex]!;
    if (slug === "neutral") {
        return { error: "cannot remove neutral emotion" };
    }

    const exprIdx = config.namedEmotionToExpressionIndex[emotionIndex]!;

    const expressions = [...config.expressions];
    const expressionIsEmotion = [...config.expressionIsEmotion];
    const baseTargets = config.baseTargets.map(r => cloneRow(r));
    const idleAnim = config.idleAnim.map(r => ({ ...r }));

    expressions.splice(exprIdx, 1);
    expressionIsEmotion.splice(exprIdx, 1);
    baseTargets.splice(exprIdx, 1);
    idleAnim.splice(exprIdx, 1);

    const emotionNames = config.emotionNames.filter((_, i) => i !== emotionIndex);
    const emotionPoints = config.emotionPoints.filter((_, i) => i !== emotionIndex);
    let pickOrderIndices = config.pickOrderIndices.filter(i => i !== emotionIndex);
    pickOrderIndices = pickOrderIndices.map(i => (i > emotionIndex ? i - 1 : i));

    const namedEmotionToExpressionIndex = config.namedEmotionToExpressionIndex
        .map((e, i) => (i === emotionIndex ? -1 : e))
        .filter((_, i) => i !== emotionIndex)
        .map(e => (e > exprIdx ? e - 1 : e));

    const verbTimelines = config.verbTimelines.map(tab => ({
        ...tab,
        verb: tab.verb > exprIdx ? tab.verb - 1 : tab.verb,
    }));

    const emotionTriangulation = buildEmotionTriangulationFromPoints(emotionNames, emotionPoints);

    return {
        config: applyConfigPatch(config, {
            expressions,
            expressionIsEmotion,
            baseTargets,
            idleAnim,
            emotionNames,
            emotionPoints,
            pickOrderIndices,
            namedEmotionToExpressionIndex,
            verbTimelines,
            emotionTriangulation,
        }),
    };
}

/** Add emotion at default V/A; appends a new expression row. */
export function addEmotion(
    config: FaceConfigState,
    slug: string,
    v = 0,
    a = 0.5
): { config: FaceConfigState } | { error: string } {
    const err = validateSlug(slug, "emotion");
    if (err) return { error: err };
    if (config.emotionNames.some(n => n.toLowerCase() === slug.toLowerCase())) {
        return { error: `emotion "${slug}" already exists` };
    }

    const exprName = emotionSlugToExpressionName(slug);
    if (config.expressions.includes(exprName)) {
        return { error: `expression "${exprName}" already exists` };
    }

    const exprIdx = config.expressions.length;
    const expressions = [...config.expressions, exprName];
    const expressionIsEmotion = [...config.expressionIsEmotion, true];
    const baseTargets = [...config.baseTargets.map(r => cloneRow(r)), defaultEmotionRow(config)];
    const idleAnim = [...config.idleAnim.map(r => ({ ...r })), defaultVerbIdleRow(config)];

    const emotionNames = [...config.emotionNames, slug];
    const emotionPoints = [...config.emotionPoints.map(p => ({ ...p })), { v, a }];
    const pickOrderIndices = [...config.pickOrderIndices, emotionNames.length - 1];
    const namedEmotionToExpressionIndex = [...config.namedEmotionToExpressionIndex, exprIdx];

    const emotionTriangulation = buildEmotionTriangulationFromPoints(emotionNames, emotionPoints);

    const next = applyConfigPatch(config, {
        expressions,
        expressionIsEmotion,
        baseTargets,
        idleAnim,
        emotionNames,
        emotionPoints,
        pickOrderIndices,
        namedEmotionToExpressionIndex,
        verbTimelines: config.verbTimelines,
        emotionTriangulation,
    });
    return { config: next };
}

/** Rename emotion slug; updates triangulation anchor label. */
export function renameEmotion(
    config: FaceConfigState,
    emotionIndex: number,
    newSlug: string
): { config: FaceConfigState } | { error: string } {
    const err = validateSlug(newSlug, "emotion");
    if (err) return { error: err };
    if (emotionIndex < 0 || emotionIndex >= config.emotionNames.length) {
        return { error: "invalid emotion index" };
    }
    const oldSlug = config.emotionNames[emotionIndex]!;
    if (oldSlug === "neutral" && newSlug !== "neutral") {
        return { error: "cannot rename neutral" };
    }
    if (
        config.emotionNames.some(
            (n, i) => i !== emotionIndex && n.toLowerCase() === newSlug.toLowerCase()
        )
    ) {
        return { error: `emotion "${newSlug}" already exists` };
    }

    const exprIdx = config.namedEmotionToExpressionIndex[emotionIndex]!;
    const newExprName = emotionSlugToExpressionName(newSlug);
    const expressions = [...config.expressions];
    expressions[exprIdx] = newExprName;

    const emotionNames = [...config.emotionNames];
    emotionNames[emotionIndex] = newSlug;

    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        emotionNames,
        config.emotionPoints
    );

    return {
        config: applyConfigPatch(config, {
            expressions,
            emotionNames,
            emotionTriangulation,
        }),
    };
}

export function removeVerb(
    config: FaceConfigState,
    expressionIndex: number
): { config: FaceConfigState } | { error: string } {
    const exprName = config.expressions[expressionIndex];
    if (!exprName) return { error: "invalid verb" };
    if (isSystemVerbExpression(exprName)) {
        return { error: `cannot remove system verb "${exprName}"` };
    }
    if (!exprName.startsWith("Verb")) return { error: "not a verb expression" };

    const expressions = [...config.expressions];
    const expressionIsEmotion = [...config.expressionIsEmotion];
    const baseTargets = config.baseTargets.map(r => cloneRow(r));
    const idleAnim = config.idleAnim.map(r => ({ ...r }));

    expressions.splice(expressionIndex, 1);
    expressionIsEmotion.splice(expressionIndex, 1);
    baseTargets.splice(expressionIndex, 1);
    idleAnim.splice(expressionIndex, 1);

    const namedEmotionToExpressionIndex = remapIndicesAfterRemove(
        config.namedEmotionToExpressionIndex,
        expressionIndex
    );

    const verbTimelines = config.verbTimelines
        .filter(t => t.verb !== expressionIndex)
        .map(t => ({
            ...t,
            verb: t.verb > expressionIndex ? t.verb - 1 : t.verb,
        }));

    return {
        config: applyConfigPatch(config, {
            expressions,
            expressionIsEmotion,
            baseTargets,
            idleAnim,
            namedEmotionToExpressionIndex,
            verbTimelines,
        }),
    };
}

export function addVerb(
    config: FaceConfigState,
    slug: string
): { config: FaceConfigState } | { error: string } {
    const err = validateSlug(slug, "verb");
    if (err) return { error: err };
    if (slug === "none") return { error: '"none" is reserved' };

    const exprName = verbSlugToExpressionName(slug);
    if (config.expressions.includes(exprName)) {
        return { error: `verb "${slug}" already exists` };
    }
    if (config.emotionNames.some(n => n.toLowerCase() === slug.toLowerCase())) {
        return { error: `slug "${slug}" conflicts with an emotion name` };
    }

    const exprIdx = config.expressions.length;
    const expressions = [...config.expressions, exprName];
    const expressionIsEmotion = [...config.expressionIsEmotion, false];
    const baseTargets = [...config.baseTargets.map(r => cloneRow(r)), EMPTY_PARAM_ROW()];
    const idleAnim = [...config.idleAnim.map(r => ({ ...r })), defaultVerbIdleRow(config)];
    const verbTimelines = [...config.verbTimelines, defaultVerbTimeline(exprIdx)];

    return {
        config: applyConfigPatch(config, {
            expressions,
            expressionIsEmotion,
            baseTargets,
            idleAnim,
            verbTimelines,
        }),
    };
}

/** Ensure every system verb has a timeline row (migration helper). */
export function ensureSystemVerbTimelines(config: FaceConfigState): FaceConfigState {
    const verbTimelines = config.verbTimelines.map(t => ({
        ...t,
        keyframes: t.keyframes.map(k => ({ ...k, overrides: k.overrides.map(o => ({ ...o })) })),
    }));
    for (const name of SYSTEM_VERB_EXPRESSION_NAMES) {
        const idx = config.expressions.indexOf(name);
        if (idx < 0) continue;
        if (verbTimelines.some(t => t.verb === idx)) continue;
        verbTimelines.push(defaultVerbTimeline(idx));
    }
    return { ...config, verbTimelines };
}

export function listVerbExpressionNames(config: FaceConfigState): string[] {
    return config.expressions.filter(
        (_, i) => !config.expressionIsEmotion[i] && config.expressions[i]!.startsWith("Verb")
    );
}

function applyConfigPatch(
    config: FaceConfigState,
    patch: Partial<FaceConfigState>
): FaceConfigState {
    return { ...config, ...patch };
}

/** Resolve inspector / blend anchor label to emotion index. */
export function emotionIndexFromAnchorLabel(config: FaceConfigState, anchorLabel: string): number {
    const slug = anchorLabel.charAt(0).toLowerCase() + anchorLabel.slice(1);
    return config.emotionNames.findIndex(
        n => n === slug || namedEmotionAnchorLabel(n) === anchorLabel
    );
}
