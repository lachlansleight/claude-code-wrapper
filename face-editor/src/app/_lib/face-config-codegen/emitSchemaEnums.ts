import type { FaceConfigState } from "../face-engine/faceConfigState";
import {
    expressionNameToVerbSlug,
    SYSTEM_VERB_SLUGS,
    verbSlugsFromConfig,
} from "../face-engine/faceSchema";
import { namedEmotionEnumName } from "./format";

export function emitFaceExpressionEnum(expressions: readonly string[]): string {
    const lines = expressions.map(
        (name, i) => `  ${name}${i < expressions.length - 1 ? "," : ","}`
    );
    lines.push("  Count");
    return `enum class Expression : uint8_t {\n${lines.join("\n")}\n};`;
}

export function emitNamedEmotionEnum(emotionNames: readonly string[]): string {
    const lines = emotionNames.map(
        (slug, i) => `  ${namedEmotionEnumName(slug)}${i < emotionNames.length - 1 ? "," : ","}`
    );
    lines.push("  Count");
    return `enum class NamedEmotion : uint8_t {\n${lines.join("\n")}\n};`;
}

/** Custom verb slugs appended after system verbs (before Count). */
export function customVerbSlugs(config: FaceConfigState): string[] {
    const all = new Set<string>();
    for (let i = 0; i < config.expressions.length; i++) {
        if (config.expressionIsEmotion[i]) continue;
        const slug = expressionNameToVerbSlug(config.expressions[i]!);
        if (slug && slug !== "none") all.add(slug);
    }
    return Array.from(all).filter(s => !(SYSTEM_VERB_SLUGS as readonly string[]).includes(s));
}

export function emitVerbEnum(config: FaceConfigState): string {
    const custom = customVerbSlugs(config);
    const lines: string[] = [
        "  None = 0,",
        "  Thinking,",
        "  Reading,",
        "  Writing,",
        "  Executing,",
        "  Straining,",
        "  Sleeping,",
        "  Waking,",
        "  AttractingAttention,",
    ];
    for (const slug of custom) {
        const en = slug
            .split("_")
            .map(p => p.charAt(0).toUpperCase() + p.slice(1))
            .join("");
        lines.push(`  ${en},`);
    }
    lines.push("  Count");
    return `enum class Verb : uint8_t {\n${lines.join("\n")}\n};`;
}

export function emitVerbToExpressionTable(config: FaceConfigState): string {
    const custom = customVerbSlugs(config);
    const slugToExpr = new Map<string, number>();
    for (let i = 0; i < config.expressions.length; i++) {
        const slug = expressionNameToVerbSlug(config.expressions[i]!);
        if (slug) slugToExpr.set(slug, i);
    }
    const neutralIdx = config.expressions.indexOf("Neutral");
    const rows: string[] = [];
    rows.push(
        `  Face::Expression::${config.expressions[neutralIdx >= 0 ? neutralIdx : 0]},  // None`
    );
    for (let i = 1; i < SYSTEM_VERB_SLUGS.length; i++) {
        const slug = SYSTEM_VERB_SLUGS[i]!;
        const exprIdx = slugToExpr.get(slug) ?? neutralIdx;
        const exprName = config.expressions[exprIdx >= 0 ? exprIdx : 0]!;
        rows.push(`  Face::Expression::${exprName},  // ${slug}`);
    }
    for (const slug of custom) {
        const exprIdx = slugToExpr.get(slug)!;
        const exprName = config.expressions[exprIdx]!;
        rows.push(`  Face::Expression::${exprName},  // ${slug}`);
    }
    return `static constexpr Face::Expression kVerbToExpression[(uint8_t)VerbSystem::Verb::Count] = {\n${rows.join("\n")},\n};`;
}

/** Slugs in `VerbSystem::Verb` enum order (None, Thinking, …, custom…). */
export function verbSlugsInEnumOrder(config: FaceConfigState): string[] {
    return [...SYSTEM_VERB_SLUGS, ...customVerbSlugs(config)];
}

export function emitVerbSlugTable(config: FaceConfigState): string {
    const lines = verbSlugsInEnumOrder(config).map(s => `    "${s}",`);
    return `static constexpr const char* kVerbSlugs[(uint8_t)VerbSystem::Verb::Count] = {\n${lines.join("\n")}\n};`;
}
