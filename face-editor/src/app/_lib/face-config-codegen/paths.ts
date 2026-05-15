import path from "path";

export function repoRootFromCwd(): string {
    return path.resolve(process.cwd(), "..");
}

export interface FaceConfigArtifactPaths {
    faceConfigTs: string;
    faceConfigH: string;
    faceConfigSnapshot: string;
    emotionTriangulationH: string;
}

export function faceConfigArtifactPaths(repoRoot: string): FaceConfigArtifactPaths {
    return {
        faceConfigTs: path.join(
            repoRoot,
            "face-editor/src/app/_lib/face-engine/FACE_CONFIG_DATA.ts"
        ),
        faceConfigH: path.join(repoRoot, "robot_v3/src/face/FACE_CONFIG_DATA.h"),
        faceConfigSnapshot: path.join(
            repoRoot,
            "face-editor/src/app/_lib/face-engine/FACE_CONFIG_DATA.snapshot.json"
        ),
        emotionTriangulationH: path.join(
            repoRoot,
            "robot_v3/src/behaviour/EmotionTriangulation.h"
        ),
    };
}
