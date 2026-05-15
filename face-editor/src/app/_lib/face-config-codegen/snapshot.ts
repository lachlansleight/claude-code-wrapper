import type { FaceConfigState } from "../face-engine/faceConfigState";
import { buildEmotionTriangulationFromPoints } from "../face-engine/emotionTriangulationLive";

/** JSON-safe snapshot (triangulation mesh rebuilt on load from emotionPoints). */
export type FaceConfigSnapshot = Omit<FaceConfigState, "emotionTriangulation">;

export function faceConfigToSnapshot(config: FaceConfigState): FaceConfigSnapshot {
    const { emotionTriangulation: _tri, ...rest } = config;
    return JSON.parse(JSON.stringify(rest)) as FaceConfigSnapshot;
}

export function faceConfigFromSnapshot(snapshot: FaceConfigSnapshot): FaceConfigState {
    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        snapshot.emotionNames,
        snapshot.emotionPoints.map(p => ({ v: p.v, a: p.a }))
    );
    return {
        ...snapshot,
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
}
