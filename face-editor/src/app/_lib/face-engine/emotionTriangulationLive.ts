import Delaunator from "delaunator";
import type { EmotionPoint } from "./faceConfigTypes";
import type { EmotionTriangulationTable, TriangulationAnchor } from "./types";

/** Editor-local triangulation: anchors and triangle indices are mutated in place. */
export type MutableEmotionTriangulation = {
    domain: EmotionTriangulationTable["domain"];
    anchors: TriangulationAnchor[];
    triangles: [number, number, number][];
};

const DOMAIN_V: [number, number] = [-1.0, 1.0];
const DOMAIN_A: [number, number] = [0.0, 1.0];

/** `kEmotionNames` slug → PascalCase anchor label (e.g. `disappointed` → `Disappointed`). */
export function namedEmotionAnchorLabel(emotionName: string): string {
    if (!emotionName) return emotionName;
    return emotionName.charAt(0).toUpperCase() + emotionName.slice(1);
}

/** Slug for `kEmotionNames` from a triangulation anchor label. */
export function namedEmotionSlugFromAnchor(anchorEmotion: string): string {
    if (!anchorEmotion) return anchorEmotion;
    return anchorEmotion.charAt(0).toLowerCase() + anchorEmotion.slice(1);
}

export function cloneMutableEmotionTriangulation(
    src: EmotionTriangulationTable
): MutableEmotionTriangulation {
    return {
        domain: {
            v: [src.domain.v[0]!, src.domain.v[1]!],
            a: [src.domain.a[0]!, src.domain.a[1]!],
        },
        anchors: src.anchors.map(a => ({
            v: a.v,
            a: a.a,
            emotion: a.emotion,
        })),
        triangles: src.triangles.map(t => [t[0]!, t[1]!, t[2]!]),
    };
}

/** Build anchors from `kEmotionPoints` and Delaunay-triangulate (editor; firmware uses baked mesh). */
export function buildEmotionTriangulationFromPoints(
    emotionNames: readonly string[],
    emotionPoints: readonly EmotionPoint[]
): MutableEmotionTriangulation {
    if (emotionNames.length !== emotionPoints.length) {
        throw new Error(
            `emotionNames.length (${emotionNames.length}) !== emotionPoints.length (${emotionPoints.length})`
        );
    }
    const tri: MutableEmotionTriangulation = {
        domain: { v: [...DOMAIN_V], a: [...DOMAIN_A] },
        anchors: emotionNames.map((name, i) => {
            const p = emotionPoints[i]!;
            return {
                v: p.v,
                a: p.a,
                emotion: namedEmotionAnchorLabel(name),
            };
        }),
        triangles: [],
    };
    retriangulateEmotionAnchors(tri);
    return tri;
}

/** Replaces `tri.triangles` with a Delaunay triangulation of current anchor V/A. */
export function retriangulateEmotionAnchors(tri: MutableEmotionTriangulation): void {
    if (tri.anchors.length < 3) {
        tri.triangles.length = 0;
        return;
    }
    const d = Delaunator.from(
        tri.anchors,
        p => p.v,
        p => p.a
    );
    const next: [number, number, number][] = [];
    for (let i = 0; i < d.triangles.length; i += 3) {
        next.push([d.triangles[i]!, d.triangles[i + 1]!, d.triangles[i + 2]!]);
    }
    tri.triangles.length = 0;
    for (const t of next) tri.triangles.push(t);
}

/** Keep `emotionPoints` aligned when an anchor is dragged (same index order as `kEmotionNames`). */
export function syncEmotionPointFromAnchor(
    emotionNames: readonly string[],
    emotionPoints: EmotionPoint[],
    anchor: TriangulationAnchor
): void {
    const slug = namedEmotionSlugFromAnchor(anchor.emotion);
    const idx = emotionNames.indexOf(slug);
    if (idx < 0) return;
    emotionPoints[idx]!.v = anchor.v;
    emotionPoints[idx]!.a = anchor.a;
}
