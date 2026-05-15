/**
 * All expressions that have a `kVerbTimelines` row (aligned with firmware
 * `Face::expressionUsesVerbTimeline`).
 */
export const VERB_TIMELINE_NAMES = [
    "VerbThinking",
    "VerbReading",
    "VerbWriting",
    "VerbExecuting",
    "VerbStraining",
    "VerbSleeping",
    "VerbWaking",
    "VerbAttractingAttention",
] as const;

export type VerbTimelineName = (typeof VERB_TIMELINE_NAMES)[number];
