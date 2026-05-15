/**
 * `face-engine` is React-free: rendering, tweening, and emotion blend math
 * mirror `robot_v3` (`FaceRenderer`, `FrameController`, `EmotionBlend`).
 *
 * Dependency direction: `presets` / `emotionTriangulation` → `emotionBlend` →
 * `frameController`; `faceRenderer` + `robotSettings` + `tftSprite` are used by the controller tick.
 *
 * Regenerate triangulation: `python scripts/gen_emotion_triangulation.py` (update output path in that script to `emotionTriangulation.ts` when ready).
 */

export {
    createFrameController,
    type FrameController,
    type StaticOverrideState,
} from "./frameController";
export { createFaceRenderer, type FaceRendererApi } from "./faceRenderer";
export { createEmotionBlend, type EmotionBlendApi } from "./emotionBlend";
export { createRobotSettings, type RobotSettings } from "./robotSettings";
export { EMOTION_TRIANGULATION } from "./emotionTriangulation";
export { tft, TFTSprite } from "./tftSprite";
export type { FaceParams, ParamField } from "./faceParams";
export {
    PARAM_FIELD_LABEL,
    PARAM_FIELDS_UI_ORDER,
    PARAM_UI_SECTIONS,
    paramFieldLabel,
} from "./faceParams";
export {
    PARAM_FIELDS,
    expressionsList,
    paramFieldsList,
    baseTargetForExpression,
    isEmotionExpression,
} from "./presets";
