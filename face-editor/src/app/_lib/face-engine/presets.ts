import {
  EXPRESSIONS,
  kBaseTargets,
  kExpressionIsEmotion,
  expressionIndexFromName,
  type ExpressionName,
} from "./FACE_CONFIG_DATA";
import {
  PARAM_FIELDS,
  faceParamsFromIndexed,
  type FaceParams,
  type ParamField,
} from "./faceParams";

export { EXPRESSIONS, type ExpressionName };
export { PARAM_FIELDS, type FaceParams, type ParamField };

export function isEmotionExpression(name: string): boolean {
  const i = expressionIndexFromName(name);
  return !!kExpressionIsEmotion[i];
}

export function baseTargetForExpression(name: string): FaceParams {
  const i = expressionIndexFromName(name);
  return faceParamsFromIndexed(kBaseTargets[i]!);
}

export function paramFieldsList(): readonly ParamField[] {
  return PARAM_FIELDS;
}

export function expressionsList(): readonly string[] {
  return [...EXPRESSIONS];
}
