import type { FaceParams, ParamField } from "../face-engine/faceParams";

/** kBaseTargets paste format — matches robot_v3 FrameController.cpp layout. */
export function formatKBaseTargetsCpp(
  exprName: string,
  params: FaceParams,
  paramFields: readonly ParamField[],
): string {
  const v = paramFields.map((k) => params[k] | 0);
  const signedIdx = new Set([3, 14]);
  function fmt(i: number): string {
    const n = v[i]!;
    if (n > 0 && signedIdx.has(i)) return `+${n}`;
    return String(n);
  }
  const head = `    /* ${exprName} */`;
  const headPad = Math.max(1, 27 - head.length);
  const headPart = head + " ".repeat(headPad);
  const ind = " ".repeat(30);
  const l1 =
    headPart +
    "{ " +
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(fmt).join(", ") +
    ",\n";
  const l2 = ind + [11, 12, 13, 14, 15, 16, 17, 18].map(fmt).join(", ") + ",\n";
  const l3 = ind + [19, 20, 21, 22, 23].map(fmt).join(", ") + " },\n";
  return l1 + l2 + l3;
}

export function formatParamIntsSignedCsv(
  params: FaceParams,
  paramFields: readonly ParamField[],
): string {
  return paramFields
    .map((k) => {
      const n = params[k] | 0;
      return n > 0 ? `+${n}` : String(n);
    })
    .join(", ");
}

export function parseCommaSeparatedParamValues(
  text: string,
): { ok: true; nums: number[] } | { ok: false; error: string } {
  const numPat = /[-+]?(?:\d+\.\d*|\d*\.\d+|\d+)(?:[eE][-+]?\d+)?/g;
  const segments = text
    .split(",")
    .map((s) => s.trim())
    .filter((s) => s.length > 0);
  const nums: number[] = [];
  for (let i = 0; i < segments.length; i++) {
    const withoutF = segments[i]!.replace(/[fF]\s*$/i, "").trim();
    let last: string | null = null;
    let m: RegExpExecArray | null;
    numPat.lastIndex = 0;
    while ((m = numPat.exec(withoutF)) !== null) {
      last = m[0];
    }
    if (last === null) {
      return {
        ok: false,
        error: `Could not parse a number in segment ${i + 1}: ${JSON.stringify(segments[i])}`,
      };
    }
    const n = Math.trunc(parseFloat(last));
    if (Number.isNaN(n)) {
      return {
        ok: false,
        error: `Invalid number in segment ${i + 1}: ${JSON.stringify(last)}`,
      };
    }
    nums.push(n);
  }
  return { ok: true, nums };
}
