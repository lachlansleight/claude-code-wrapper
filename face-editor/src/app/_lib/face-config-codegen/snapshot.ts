import type { FaceConfigState } from "../face-engine/faceConfigState";
import { ensureSystemVerbTimelines } from "../face-engine/faceConfigMutations";
import { buildEmotionTriangulationFromPoints } from "../face-engine/emotionTriangulationLive";
import { migrateFaceConfigToSchemaV3 } from "./migrateSchemaV3";

/** JSON-safe snapshot (triangulation mesh rebuilt on load from emotionPoints). */
export type FaceConfigSnapshot = Omit<FaceConfigState, "emotionTriangulation">;

export function faceConfigToSnapshot(config: FaceConfigState): FaceConfigSnapshot {
    const { emotionTriangulation: _tri, ...rest } = config;
    return JSON.parse(JSON.stringify(rest)) as FaceConfigSnapshot;
}

export function faceConfigFromSnapshot(snapshot: FaceConfigSnapshot): FaceConfigState {
    const expressions = snapshot.expressions?.length ? [...snapshot.expressions] : [];
    if (!expressions.length) {
        throw new Error("snapshot missing expressions[]");
    }
    const expressionIsEmotion =
        snapshot.expressionIsEmotion?.length === expressions.length
            ? [...snapshot.expressionIsEmotion]
            : expressions.map(() => true);
    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        snapshot.emotionNames,
        snapshot.emotionPoints.map(p => ({ v: p.v, a: p.a }))
    );
    const merged: FaceConfigState = {
        schemaVersion: snapshot.schemaVersion ?? 2,
        expressions,
        expressionIsEmotion,
        emotionNames: [...snapshot.emotionNames],
        emotionPoints: snapshot.emotionPoints.map(p => ({ v: p.v, a: p.a })),
        pickOrderIndices: [...snapshot.pickOrderIndices],
        namedEmotionToExpressionIndex: [...snapshot.namedEmotionToExpressionIndex],
        baseTargets: snapshot.baseTargets.map(row =>
            row.map(c => ({ value: c.value, strength: c.strength }))
        ),
        verbKeyframeOverridesMax: snapshot.verbKeyframeOverridesMax,
        verbKeyframesMax: snapshot.verbKeyframesMax,
        verbTimelines: snapshot.verbTimelines.map(tab => ({
            ...tab,
            keyframes: tab.keyframes.map(kf => ({
                ...kf,
                overrides: kf.overrides.map(o => ({ ...o })),
            })),
        })),
        bobAmpFollowEmotionArm: snapshot.bobAmpFollowEmotionArm,
        idleAnim: snapshot.idleAnim.map(r => ({ ...r })),
        emotionSim: { ...snapshot.emotionSim },
        frameAnim: { ...snapshot.frameAnim },
        verbSim: { ...snapshot.verbSim },
        verbTransitionDurMs: snapshot.verbTransitionDurMs,
        emotionTriangulation,
    };
    return migrateFaceConfigToSchemaV3(ensureSystemVerbTimelines(merged));
}
