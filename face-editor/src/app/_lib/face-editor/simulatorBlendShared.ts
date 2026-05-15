import type { EmotionTriangulationTable } from "../face-engine/types";
import { EMOTION_TRIANGULATION } from "../face-engine/emotionTriangulation";

export function emotionVA(
    name: string,
    tri: EmotionTriangulationTable = EMOTION_TRIANGULATION
): { v: number; a: number } {
    const an = tri.anchors.find(x => x.emotion === name);
    return an ? { v: an.v, a: an.a } : { v: 0, a: 0.5 };
}
