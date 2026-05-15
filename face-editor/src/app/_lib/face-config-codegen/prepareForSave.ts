import type { FaceConfigState } from "../face-engine/faceConfigState";
import {
    buildEmotionTriangulationFromPoints,
    syncEmotionPointFromAnchor,
} from "../face-engine/emotionTriangulationLive";

/** Align emotionPoints with anchors, then rebuild Delaunay mesh for save/firmware. */
export function prepareFaceConfigForSave(config: FaceConfigState): FaceConfigState {
    const emotionPoints = config.emotionPoints.map(p => ({ v: p.v, a: p.a }));
    for (const an of config.emotionTriangulation.anchors) {
        syncEmotionPointFromAnchor(config.emotionNames, emotionPoints, an);
    }
    const emotionTriangulation = buildEmotionTriangulationFromPoints(
        config.emotionNames,
        emotionPoints
    );
    return {
        ...config,
        emotionPoints,
        emotionTriangulation,
    };
}
