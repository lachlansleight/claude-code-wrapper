import {
  EXPRESSIONS,
  Expression,
  EXPRESSION_COUNT,
} from "../face-engine/FACE_CONFIG_DATA";

/** Slug for `POST /api/raw/verb/start` body `{ verb: "<slug>" }` (e.g. `thinking`). */
export function robotVerbStartSlug(verb: Expression): string | null {
  const vi = verb as number;
  if (!Number.isFinite(vi) || vi < 0 || vi >= EXPRESSION_COUNT) return null;
  const list = EXPRESSIONS as readonly string[];
  const name = list[vi];
  if (typeof name !== "string") return null;
  if (!name.startsWith("Verb")) return null;
  const tail = name.slice(4);
  if (!tail) return null;
  return tail.charAt(0).toLowerCase() + tail.slice(1);
}
