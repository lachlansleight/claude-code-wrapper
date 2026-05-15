/** Blended base-layer arm motion (matches firmware `EmotionArmMotion`). */
export interface EmotionArmMotion {
    min_offset_deg: number;
    max_offset_deg: number;
    waggle_period_s: number;
    waggle_interval_s: number;
}

export interface TriangulationAnchor {
    v: number;
    a: number;
    emotion: string;
}

export interface EmotionTriangulationTable {
    domain: { v: readonly [number, number]; a: readonly [number, number] };
    anchors: readonly TriangulationAnchor[];
    triangles: readonly (readonly [number, number, number])[];
}

export interface BlendTriangle {
    indices: [number, number, number];
    weights: [number, number, number];
}
