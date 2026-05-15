/**
 * Dynamic face schema: emotion/verb slugs, expression naming, system verb guards.
 */

/** Verbs wired in firmware EventRouter — timelines cannot be removed. */
export const SYSTEM_VERB_EXPRESSION_NAMES = [
    "VerbThinking",
    "VerbReading",
    "VerbWriting",
    "VerbExecuting",
    "VerbStraining",
    "VerbSleeping",
    "VerbWaking",
    "VerbAttractingAttention",
] as const;

export type SystemVerbExpressionName = (typeof SYSTEM_VERB_EXPRESSION_NAMES)[number];

/** VerbSystem::Verb slugs in enum order (index 0 = None). */
export const SYSTEM_VERB_SLUGS = [
    "none",
    "thinking",
    "reading",
    "writing",
    "executing",
    "straining",
    "sleeping",
    "waking",
    "attracting_attention",
] as const;

const SLUG_RE = /^[a-z][a-z0-9_]*$/;
const RESERVED_SLUGS = new Set(["none", "count"]);

export function emotionSlugToExpressionName(slug: string): string {
    if (slug === "neutral") return "Neutral";
    return slug.charAt(0).toUpperCase() + slug.slice(1);
}

export function verbSlugToExpressionName(slug: string): string {
    const base = slug
        .split("_")
        .map(part => part.charAt(0).toUpperCase() + part.slice(1))
        .join("");
    return `Verb${base}`;
}

export function expressionNameToVerbSlug(expressionName: string): string | null {
    if (!expressionName.startsWith("Verb")) return null;
    const tail = expressionName.slice(4);
    if (!tail) return null;
    return tail
        .replace(/([A-Z])/g, "_$1")
        .toLowerCase()
        .replace(/^_/, "");
}

export function expressionNameToEmotionSlug(expressionName: string): string | null {
    if (expressionName.startsWith("Verb")) return null;
    return expressionName.charAt(0).toLowerCase() + expressionName.slice(1);
}

export function validateSlug(slug: string, kind: "emotion" | "verb"): string | null {
    if (!slug.trim()) return `${kind} slug is required`;
    if (!SLUG_RE.test(slug)) {
        return `${kind} slug must match ${SLUG_RE} (e.g. "gleeful", "my_verb")`;
    }
    if (RESERVED_SLUGS.has(slug)) return `"${slug}" is reserved`;
    return null;
}

export function validateUniqueSlugs(
    emotionNames: readonly string[],
    verbSlugs: readonly string[]
): string | null {
    const seen = new Map<string, string>();
    for (const s of emotionNames) {
        const err = validateSlug(s, "emotion");
        if (err) return err;
        const key = s.toLowerCase();
        if (seen.has(key)) return `duplicate emotion slug "${s}"`;
        seen.set(key, "emotion");
    }
    for (const s of verbSlugs) {
        if (s === "none") continue;
        const err = validateSlug(s, "verb");
        if (err) return err;
        const key = s.toLowerCase();
        if (seen.has(key)) return `duplicate slug "${s}" (emotion and verb names must be unique)`;
        seen.set(key, "verb");
    }
    return null;
}

export function isSystemVerbExpression(name: string): boolean {
    return (SYSTEM_VERB_EXPRESSION_NAMES as readonly string[]).includes(name);
}

export function verbSlugsFromConfig(
    expressions: readonly string[],
    verbExpressionIndices: readonly number[]
): string[] {
    const slugs: string[] = ["none"];
    for (const idx of verbExpressionIndices) {
        const name = expressions[idx];
        if (!name) continue;
        const slug = expressionNameToVerbSlug(name);
        if (slug) slugs.push(slug);
    }
    return slugs;
}
