import type { FaceConfigState } from "./faceConfigState";
import { isSystemVerbExpression } from "./faceSchema";

/** PascalCase expression names that have a verb timeline row. */
export function verbTimelineExpressionNames(config: FaceConfigState): string[] {
    const names: string[] = [];
    for (const tab of config.verbTimelines) {
        const n = config.expressions[tab.verb];
        if (n) names.push(n);
    }
    return names;
}

export function isVerbTimelineName(config: FaceConfigState, name: string): boolean {
    return verbTimelineExpressionNames(config).includes(name);
}

export function canRemoveVerbExpression(config: FaceConfigState, expressionName: string): boolean {
    return !isSystemVerbExpression(expressionName);
}
