import { createEmotionBlend } from "../face-engine/emotionBlend";
import { EMOTION_TRIANGULATION } from "../face-engine/emotionTriangulation";
import type { TriangulationAnchor } from "../face-engine/types";

/** Shared triangulation blend (V/A diagram + `blendedFaceParams` for static sync). */
export const emotionBlendDraw = createEmotionBlend({
  triangulation: EMOTION_TRIANGULATION,
});

export function emotionVA(name: string): { v: number; a: number } {
  const tab = EMOTION_TRIANGULATION;
  const an = tab.anchors.find((x: TriangulationAnchor) => x.emotion === name);
  return an ? { v: an.v, a: an.a } : { v: 0, a: 0.5 };
}
