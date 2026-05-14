# `face-engine`

TypeScript ports of the v3 face stack from the former `control/scripts/` simulators:

- **`faceParams.ts`** — `PARAM_FIELDS` order and `FaceParams` type (matches firmware row layout).
- **`presets.ts`** — per-expression `NEW_TARGETS` and helpers.
- **`emotionTriangulation.ts`** — V/A mesh anchors and triangles (keep in sync with `scripts/gen_emotion_triangulation.py` / firmware `kEmotionPoints`).
- **`emotionBlend.ts`** — barycentric blend in (V, A); depends on triangulation + `baseTargetForExpression` from presets (no circular import with the frame controller).
- **`frameController.ts`** — RAF driver: tweens, blink, gaze, body bob, static/blend modes, arm overlay.
- **`faceRenderer.ts`** — draws into a 240×240 sprite (port of `FaceRenderer.cpp`).
- **`tftSprite.ts`** / **`robotSettings.ts`** — browser TFT shim and named colours.

**Rule:** no imports from `react` or `next/*` in this folder.
