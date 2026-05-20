# Face Animation Editor — Brief

> **Schema v3 (landed):** Arm fields live on each `FaceParams` / `kBaseTargets`
> row (`arm_min_deg`, `arm_max_deg`, `arm_period_ms`, `arm_interval_ms`). Legacy
> `kArmPresets` / `kMotion` docs below are historical. Current editor coverage:
> [`face-editor/EDITOR_OUTPUT.md`](../face-editor/EDITOR_OUTPUT.md).

This directory is a self-contained brief for a future coding agent that
will build a **face animation editor** in a **separate repository**.
That agent may not have access to the firmware repo this brief was
written from, so every fact it needs is captured here. Where firmware
code is referenced, the relevant excerpts are inlined in
[`01_CURRENT_STATE.md`](01_CURRENT_STATE.md) and
[`05_FACE_RENDER_REFERENCE.md`](05_FACE_RENDER_REFERENCE.md).

**Folder index:** [`README.md`](README.md) lists every doc and the
recommended reading order.

## What it is

A desktop / browser-based editor for the procedural **vector face** on an
ESP32-S3 robot (240×240). The face is driven by **resolved scalar**
geometry each frame, but **authoring** uses richer data:

- **Emotions** sit on a 2D **(valence, activation)** plane and are blended
  with **Delaunay triangulation**. Each anchor preset stores every numeric
  channel as **`(value, strength)`** — strength lets a preset abstain from
  a field so neighbours (or later stages) decide.
- **Verb face geometry** is **not** a static preset row: it is **keyframed
  timelines** of sparse field overrides on top of the emotion blend.
- **Arm motion:** four fields on each `FaceParams` row (min/max deg, period
  and interval in **ms**), blended with emotions and overridable in verb
  timelines. Body bob **position** follows live servo angle; idle rows
  still author blink/gaze/bob amplitude policy.
- **Bespoke visual overlays** (sparkles, fades, one-off effects) stay in
  firmware (**`EffectsRenderer`**-style), **decoupled** from the parametric
  face pipeline — not edited in this tool.

The editor is the authoring tool for everything that lives in
**`FACE_CONFIG.h`**:

- Edit emotion anchors, strengths, and **`FaceParams`** fields with a
  **pixel-faithful** preview (same curve model as firmware).
- Edit **verb timelines** (keyframes, sparse overrides).
- Edit **motion** and **blink / gaze / bob** policy (see
  [`08_DECISIONS.md`](08_DECISIONS.md)).
- **Export** `FACE_CONFIG.h` and run **`scripts/gen_emotion_triangulation.py`**
  so `EmotionTriangulation.h` and `emotion-triangulation.js` stay aligned.

## End-to-end workflow

```
[ Editor in browser — Next.js + React + Tailwind, localhost:3000 ]
    │  user edits expressions, drags emotion anchors,
    │  authors verb timelines, motion + idle layers, previews in canvas
    │
    │  (optional, when "Preview on device" is on:)
    │  fetch → http://localhost:8787 (bridge)
    │      POST /api/raw/emotion/set-both, /api/raw/verb/start, …
    │      → bridge broadcasts to firmware over WS → real face moves
    ▼
[ "Export to FACE_CONFIG.h" button ]
    │  fetch → POST /api/export  (Next.js Route Handler)
    ▼
[ Next.js Route Handler (same Next.js process) ]
    │  1. writes  <firmware_repo>/robot_v3/src/face/FACE_CONFIG.h
    │  2. spawns  python scripts/gen_emotion_triangulation.py
    │     (regenerates EmotionTriangulation.h + emotion-triangulation.js)
    ▼
[ User opens Arduino IDE, presses Build → Upload ]
```

The editor is a **Next.js (App Router) + TypeScript + Tailwind**
application. Route Handlers (`app/api/*/route.ts`) run **locally** — they
need filesystem access to write the header; they are **not** aimed at
Vercel-only hosting. Live device preview uses the **bridge** on port
**`8787`**; see [`07_BRIDGE_INTEGRATION.md`](07_BRIDGE_INTEGRATION.md).

The editor never ships agent hooks or bridge **runtime** code. It is an
**authoring** tool for **`FACE_CONFIG.h`** (+ spawned tooling).

## Two phases, in order

### Phase 1 — Firmware (this repo)

Expression/emotion/motion/idle data is **scattered** today (~10+ touch
points; see [`01_CURRENT_STATE.md`](01_CURRENT_STATE.md) and
`docs/firmware/KBASETARGETS_WIRING.md`). Phase 1 **collapses authored data**
into **`robot_v3/src/face/FACE_CONFIG.h`**, implements **pipeline B** (see
[`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)), and
rewires consumers.

Implementation is **three stacked sub-phases** (each can be a PR); see
[`03_FIRMWARE_REFACTOR.md`](03_FIRMWARE_REFACTOR.md) and
[`08_DECISIONS.md`](08_DECISIONS.md).

After Phase 1, routine tuning is **edit header → run triangulation script →
build** — no hunt across `FrameController.cpp`, `EmotionSystem.cpp`, and
`MotionBehaviors.cpp` for the same logical change.

### Phase 2 — Editor (separate repo)

A Next.js app with a capable UI (successor to ideas in
`control/simulator_v3.html` — Blend / Static / Timeline / motion / idle).
Requirements: [`04_EDITOR_REQUIREMENTS.md`](04_EDITOR_REQUIREMENTS.md).

## Non-goals

- The editor does **not** simulate **behaviour policy** (verb linger,
  overlay queueing, permission UX, agent hooks). Those stay in firmware.
- The editor does **not** run on the device.
- The editor is **not** the agent bridge; bridge is only for **optional**
  live preview commands.
- The editor does **not** author **`EffectsRenderer`**-style bespoke effects
  or **`NamedColor`** / TextScene accent wiring — those stay firmware-only
  for Phase 1 (`08`).

## Locked decisions (owner Q&A)

[`08_DECISIONS.md`](08_DECISIONS.md) is the **authority** for forks left open
in older brief sections (combine formula family, `W == 0`, tween scope,
overlay vs verb split, simulator fate, PR strategy).

## Reading order

See **[`README.md`](README.md)** for the full table. Minimal path for
implementers: **`08` → `06` → `03`** (decisions, algebra, PR steps).
