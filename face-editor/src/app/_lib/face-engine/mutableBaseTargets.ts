import type { ParamI16 } from "./FACE_CONFIG_DATA";
import { kBaseTargets } from "./FACE_CONFIG_DATA";

/** Deep clone of shipped `kBaseTargets` for editor-local mutation. */
export function cloneMutableBaseTargets(): ParamI16[][] {
  return kBaseTargets.map((row) =>
    row.map((c) => ({ value: c.value, strength: c.strength })),
  );
}
