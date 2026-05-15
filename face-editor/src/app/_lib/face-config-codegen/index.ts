import { mkdirSync, writeFileSync } from "fs";
import path from "path";
import type { FaceConfigState } from "../face-engine/faceConfigState";
import { emitEmotionTriangulationH } from "./emitEmotionTriangulationH";
import { emitFaceConfigH } from "./emitFaceConfigH";
import { emitFaceConfigTs } from "./emitFaceConfigTs";
import { emitVerbEnumH } from "./emitVerbEnumH";
import { faceConfigArtifactPaths } from "./paths";
import { prepareFaceConfigForSave } from "./prepareForSave";
import { faceConfigToSnapshot } from "./snapshot";
import { validateFaceConfigState } from "./validate";

export { loadFaceConfigFromDisk } from "./loadFaceConfigFromDisk";
export { prepareFaceConfigForSave } from "./prepareForSave";
export { repoRootFromCwd, faceConfigArtifactPaths } from "./paths";

export interface EmitAllResult {
    filesWritten: string[];
    config: FaceConfigState;
}

export function emitAllFaceConfigArtifacts(
    repoRoot: string,
    rawConfig: FaceConfigState
): EmitAllResult {
    const err = validateFaceConfigState(rawConfig);
    if (err) throw new Error(err);

    const config = prepareFaceConfigForSave(rawConfig);
    const paths = faceConfigArtifactPaths(repoRoot);

    const writes: { file: string; content: string }[] = [
        { file: paths.faceConfigTs, content: emitFaceConfigTs(config) },
        { file: paths.faceConfigH, content: emitFaceConfigH(config) },
        { file: paths.emotionTriangulationH, content: emitEmotionTriangulationH(config) },
        {
            file: paths.faceConfigSnapshot,
            content: JSON.stringify(faceConfigToSnapshot(config), null, 2) + "\n",
        },
        { file: paths.verbEnumH, content: emitVerbEnumH(config) },
    ];

    const filesWritten: string[] = [];
    for (const { file, content } of writes) {
        mkdirSync(path.dirname(file), { recursive: true });
        writeFileSync(file, content, "utf8");
        filesWritten.push(file);
    }

    return { filesWritten, config };
}
