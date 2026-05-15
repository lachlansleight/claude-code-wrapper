import { existsSync, readFileSync } from "fs";
import { buildFaceConfigStateFromSource } from "../face-engine/mutableFaceConfig";
import type { FaceConfigState } from "../face-engine/faceConfigState";
import { faceConfigArtifactPaths } from "./paths";
import { type FaceConfigSnapshot, faceConfigFromSnapshot } from "./snapshot";

export function loadFaceConfigFromDisk(repoRoot: string): FaceConfigState {
    const paths = faceConfigArtifactPaths(repoRoot);
    if (existsSync(paths.faceConfigSnapshot)) {
        const raw = readFileSync(paths.faceConfigSnapshot, "utf8");
        const snapshot = JSON.parse(raw) as FaceConfigSnapshot;
        return faceConfigFromSnapshot(snapshot);
    }
    return buildFaceConfigStateFromSource();
}
