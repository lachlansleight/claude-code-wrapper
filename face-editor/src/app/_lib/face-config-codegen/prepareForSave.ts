import type { FaceConfigState } from "../face-engine/faceConfigState";
import { ensureSystemVerbTimelines } from "../face-engine/faceConfigMutations";
import {
    buildEmotionTriangulationFromPoints,
    syncEmotionPointFromAnchor,
} from "../face-engine/emotionTriangulationLive";

/** Align emotionPoints with anchors, then rebuild Delaunay mesh for save/firmware. */
export function prepareFaceConfigForSave(config: FaceConfigState): FaceConfigState {
    let next = ensureSystemVerbTimelines(config);
    const emotionPoints = next.emotionPoints.map(p => ({ v: p.v, a: p.a }));
    for (const an of next.emotionTriangulation.anchors) {
        syncEmotionPointFromAnchor(next.emotionNames, emotionPoints, an);
    }
    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        next.emotionNames,
        emotionPoints
    );
    return {
        ...next,
        schemaVersion: 2,
        emotionPoints,
        emotionTriangulation,
    };
}
