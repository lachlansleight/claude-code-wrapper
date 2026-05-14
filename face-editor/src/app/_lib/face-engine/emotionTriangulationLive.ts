import Delaunator from "delaunator";
import { EMOTION_TRIANGULATION } from "./emotionTriangulation";
import type { EmotionTriangulationTable, TriangulationAnchor } from "./types";

/** Editor-local triangulation: anchors and triangle indices are mutated in place. */
export type MutableEmotionTriangulation = {
  domain: EmotionTriangulationTable["domain"];
  anchors: TriangulationAnchor[];
  triangles: [number, number, number][];
};

export function cloneMutableEmotionTriangulation(
  src: EmotionTriangulationTable = EMOTION_TRIANGULATION,
): MutableEmotionTriangulation {
  return {
    domain: {
      v: [src.domain.v[0]!, src.domain.v[1]!],
      a: [src.domain.a[0]!, src.domain.a[1]!],
    },
    anchors: src.anchors.map((a) => ({
      v: a.v,
      a: a.a,
      emotion: a.emotion,
    })),
    triangles: src.triangles.map((t) => [
      t[0]!,
      t[1]!,
      t[2]!,
    ]),
  };
}

/** Replaces `tri.triangles` with a Delaunay triangulation of current anchor V/A. */
export function retriangulateEmotionAnchors(tri: MutableEmotionTriangulation): void {
  if (tri.anchors.length < 3) {
    tri.triangles.length = 0;
    return;
  }
  const d = Delaunator.from(tri.anchors, (p) => p.v, (p) => p.a);
  const next: [number, number, number][] = [];
  for (let i = 0; i < d.triangles.length; i += 3) {
    next.push([d.triangles[i]!, d.triangles[i + 1]!, d.triangles[i + 2]!]);
  }
  tri.triangles.length = 0;
  for (const t of next) tri.triangles.push(t);
}
