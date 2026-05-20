# EDITOR_BRIEF_V2

> **Superseded (schema v3):** Arm motion is four fields on each `kBaseTargets`
> row (`arm_*`, milliseconds). `kArmPresets`, `kMotion`, and `kMotionRuntime`
> were removed. See [`face-editor/EDITOR_OUTPUT.md`](../../face-editor/EDITOR_OUTPUT.md)
> and [`docs/firmware2/MOTION.md`](../../docs/firmware2/MOTION.md).

This folder is the editor-only handoff.

It intentionally excludes historical firmware refactor sequencing and keeps
only what an editor implementation needs in order to generate
`robot_v3/src/face/FACE_CONFIG_DATA.h`.

## Goal

Build an editor app that can fully author the face/motion/emotion runtime data
and export a deterministic `FACE_CONFIG_DATA.h` initializer.

## Source docs to read (editor scope only)

- `EDITOR_BRIEF/02_FACE_CONFIG_H_SPEC.md` (target data layout)
- `EDITOR_BRIEF/04_EDITOR_REQUIREMENTS.md` (product and UX requirements)
- `EDITOR_BRIEF/06_RENDER_PIPELINE_OVERHAUL.md` (resolver math)
- `EDITOR_BRIEF/07_BRIDGE_INTEGRATION.md` (optional live preview bridge)
- `EDITOR_BRIEF/08_DECISIONS.md` (locked behavior decisions)

## Out of scope for this handoff

- Firmware PR choreography (A/B/C migration path)
- Mechanical refactor details that are already landed
- Editor UX design and visual polish

## Export contract (what editor must emit)

The editor must author data for all of the following, in one export:

1. Emotion anchors and tie-break order (`kEmotionPoints`, `kPickOrder`)
2. Per-expression `FaceParams` base rows (`kBaseTargets`, 28 fields incl. arm)
3. Verb sparse overrides (`kVerbTimelines`, may override `arm_*`)
4. Idle animation table (`kIdleAnim`)
5. Runtime simulation tunables (`kEmotionSim`, `kFrameAnim`, `kVerbSim`, `kVerbTransitionDurMs`)

If these are complete, firmware behavior is fully configurable from the single
data file.

## Canonical TypeScript schema

Use `editor-config-types.ts` in this folder as the canonical editor-side schema
for data modeling and export generation.

It is designed to map 1:1 to the current `FACE_CONFIG_DATA.h` contents.

