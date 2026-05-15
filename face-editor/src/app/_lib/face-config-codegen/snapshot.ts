import type { FaceConfigState } from "../face-engine/faceConfigState";
import { ensureSystemVerbTimelines } from "../face-engine/faceConfigMutations";
import { buildEmotionTriangulationFromPoints } from "../face-engine/emotionTriangulationLive";
import { EXPRESSIONS, kExpressionIsEmotion } from "../face-engine/FACE_CONFIG_DATA";

/** JSON-safe snapshot (triangulation mesh rebuilt on load from emotionPoints). */
export type FaceConfigSnapshot = Omit<FaceConfigState, "emotionTriangulation">;

export function faceConfigToSnapshot(config: FaceConfigState): FaceConfigSnapshot {
    const { emotionTriangulation: _tri, ...rest } = config;
    return JSON.parse(JSON.stringify(rest)) as FaceConfigSnapshot;
}

export function faceConfigFromSnapshot(snapshot: FaceConfigSnapshot): FaceConfigState {
    const expressions = snapshot.expressions?.length ? [...snapshot.expressions] : [...EXPRESSIONS];
    const expressionIsEmotion =
        snapshot.expressionIsEmotion?.length === expressions.length
            ? [...snapshot.expressionIsEmotion]
            : [...kExpressionIsEmotion];
    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        snapshot.emotionNames,
        snapshot.emotionPoints.map(p => ({ v: p.v, a: p.a }))
    );
    const merged: FaceConfigState = {
        ...snapshot,
        schemaVersion: snapshot.schemaVersion ?? 2,
        expressions,
        expressionIsEmotion,
        emotionPoints: snapshot.emotionPoints.map(p => ({ v: p.v, a: p.a })),
        baseTargets: snapshot.baseTargets.map(row =>
            row.map(c => ({ value: c.value, strength: c.strength }))
        ),
        verbTimelines: snapshot.verbTimelines.map(tab => ({
            ...tab,
            keyframes: tab.keyframes.map(kf => ({
                ...kf,
                overrides: kf.overrides.map(o => ({ ...o })),
            })),
        })),
        armPresets: snapshot.armPresets.map(p => ({ ...p })),
        motion: snapshot.motion.map(m => ({ ...m })),
        idleAnim: snapshot.idleAnim.map(r => ({ ...r })),
        emotionSim: { ...snapshot.emotionSim },
        frameAnim: { ...snapshot.frameAnim },
        verbSim: { ...snapshot.verbSim },
        motionRuntime: { ...snapshot.motionRuntime },
        emotionTriangulation,
    };
    return ensureSystemVerbTimelines(merged);
}
