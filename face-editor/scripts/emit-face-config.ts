import {
    loadFaceConfigFromDisk,
    emitAllFaceConfigArtifacts,
    repoRootFromCwd,
} from "../src/app/_lib/face-config-codegen/index";

const root = repoRootFromCwd();
const config = loadFaceConfigFromDisk(root);
const r = emitAllFaceConfigArtifacts(root, config);
console.log("Wrote:\n" + r.filesWritten.join("\n"));
