# EDITOR_BRIEF — Face editor & Phase 1 firmware

This folder is a **self-contained product + engineering brief**. One
audience is a future agent building the **face animation editor** (separate
repo). The other is anyone implementing **Phase 1** in this monorepo
(`robot_v3/`): **`FACE_CONFIG.h`**, the **render pipeline**, and related
rewiring.

## Start here

| Order | File | Purpose |
|------:|------|---------|
| 1 | [`08_DECISIONS.md`](08_DECISIONS.md) | **Locked** scope, PR strategy, pipeline order, combine rules — **read first** before coding. |
| 2 | [`00_OVERVIEW.md`](00_OVERVIEW.md) | Product vision, two phases, workflow diagram. |
| 3 | [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md) | Algebra: `ParamI16`, emotion blend, verb timelines, sampling, combine. |
| 4 | [`02_FACE_CONFIG_H_SPEC.md`](02_FACE_CONFIG_H_SPEC.md) | Target C++ header shape the editor will emit. |
| 5 | [`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md) | **Stacked PRs** A → B → C: concrete firmware steps. |
| 6 | [`05_FACE_RENDER_REFERENCE.md`](05_FACE_RENDER_REFERENCE.md) | Visual contract: 240×240 geometry, field meanings (pixels, arcs). |
| 7 | [`01_CURRENT_STATE.md`](01_CURRENT_STATE.md) | **Legacy** inventory + verbatim tables for **bootstrap** migration. |
| 8 | [`04_EDITOR_REQUIREMENTS.md`](04_EDITOR_REQUIREMENTS.md) | Phase 2 editor (Next.js, UI, export, validation). |
| 9 | [`07_BRIDGE_INTEGRATION.md`](07_BRIDGE_INTEGRATION.md) | On-device preview via bridge HTTP/WS. |

If **`08`** and **`06`** disagree on a forked choice, **`08` wins**. If **`06`**
and **`01`** disagree on behaviour, **`06`** describes the **target**
runtime; **`01`** is a snapshot of **today’s** code.

## Glossary (quick)

- **`FACE_CONFIG.h`**: Generated / editor-owned header under
  `robot_v3/src/face/`; single source for expression + emotion + verb
  timelines + motion + idle animation **policy** numbers.
- **Pipeline B**: `ParamI16` presets, strength-aware Delaunay blend,
  **emotion-only** ~250 ms tween, verb **sparse** keyframe overrides,
  **combine**, then **blink / gaze / bob** application, then vector
  renderer.
- **EffectsRenderer**: Bespoke **visual** overlays (fades, etc.) — **not**
  in `FACE_CONFIG.h`, **not** editor-authored; decoupled from verb face
  blending.

## Repo paths (firmware)

- Face runtime: `robot_v3/src/face/`, `robot_v3/src/behaviour/`
- Triangulation script: `scripts/gen_emotion_triangulation.py`
- Legacy web reference: `control/simulator_v3.html` (keep until editor ships;
  **no** Phase 1 requirement to keep it in sync)
