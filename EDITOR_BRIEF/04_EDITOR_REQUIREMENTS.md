# Phase 2 — Face editor requirements

**Product brief** for the standalone **Next.js** app. Phase 1 firmware must
land first so **`FACE_CONFIG.h`** is real.

**Pipeline:** [`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)  
**Header schema:** [`02_FACE_CONFIG_H_SPEC.md`](02_FACE_CONFIG_H_SPEC.md)  
**Locks:** [`08_DECISIONS.md`](08_DECISIONS.md)  
**Bridge preview:** [`07_BRIDGE_INTEGRATION.md`](07_BRIDGE_INTEGRATION.md)

## Stack

- **Next.js (App Router)**, **TypeScript strict**, **Tailwind**.
- **Route Handlers** (`app/api/*/route.ts`) for **filesystem**: read/write
  `FACE_CONFIG.h`, spawn **`gen_emotion_triangulation.py`**. Local dev only
  (needs `FIRMWARE_REPO_PATH` or equivalent).
- **Browser → bridge** directly for **`http://localhost:8787`** (CORS open).

## Architecture

```
Browser (Next.js, :3000)
  ├─ fetch → Route Handlers  →  fs: FACE_CONFIG.h + spawn gen_emotion_triangulation.py
  └─ fetch / WS → Bridge (:8787) → robot (optional live preview: V/A, verb, palette)
```

Bridge never writes the header; route handlers never flash firmware.

## Core UI

### Blend mode (V/A plane)

Anchors, triangulation overlay, draggable `(v, a)` cursor, barycentric readout,
tie-break preview. Ring dot colours from resolved **`ring_*`** at anchors.

### Static mode — emotion geometry

- List **emotion** expressions (and any legacy rows still carrying
  **`FaceParams`** for preview).
- All **`FaceParams`** channels: **value + strength (0…100)** sliders.
- **`mood_ring_enabled`** toggle per row.
- Preview toggles: **solo row** vs **as placed on V/A hull**.

### Timeline mode — verbs

- One **looping** timeline per verb (`loop_duration_ms`).
- Lanes per **`FieldIndex`** (geometry + extended idle indices per `02`).
- Keyframes hold up to **32** sparse overrides; linear interp in time,
  including wrap seam.
- Preview: run **full pipeline** (emotion blend + tween + verb + idle apply)
  at chosen `(v, a)`.

### Motion & idle panels

- **`kMotion[]`** (or successor table): per-expression **period / arm sync**
  fields — mirror firmware naming from PR C.
- **Idle policy** rows: blink interval min/max + duration; bob Hz, amount,
  offset; gaze randomness + reroll interval + move duration (`08`).
- These numbers use **`ParamI16`** where they participate in blend/verb;
  editor enforces the same validation rules as geometry where applicable.

### Preview canvas

- **240×240**, same curve model as [`05_FACE_RENDER_REFERENCE.md`](05_FACE_RENDER_REFERENCE.md).
- Runs the **full** resolver stack (`06` + `08`), not geometry-only.
- **30+ fps** canvas2D.

### Top bar

Export (diff preview + spawn python), reload from disk, firmware path
setting, unsaved badge.

## Editor responsibilities

1. Expressions / enums / `ExpressionKind`.
2. Emotion **`FaceParams`** (`ParamI16` everywhere).
3. **`kEmotions[]`** anchors, tie-break ranks, arm presets.
4. **`kVerbTimelines[]`**.
5. **`kMotion[]`** (or equivalent).
6. **Idle animation policy** table.
7. Invoke triangulation script on export.

## Editor non-responsibilities

- **`EffectsRenderer`** bespoke overlays — firmware, not data-driven here.
- **`NamedColor` / palette / `accentNamedColor()`** — firmware (`08`).
- Verb **policy** (linger, queueing), permission UI, agent hooks.
- Pushing arbitrary **`FaceParams`** over the bridge (no endpoint yet; see
  `07`).

## Persistence & undo

In-memory + localStorage; **50+** undo steps; **idempotent** export bytes.

## Validation (export gate)

Emotion rows valid; **`tie_break_rank`** permutation; anchors in domain;
verb timelines within capacity caps; **`FieldIndex`** in range; warn on
degenerate anchors / hull issues per earlier spec.

## Implementation notes

- Share TS types with the C++ header layout (`FieldIndex` order critical).
- Parse/load: regex over generated initializer (same strategy as Python).

## TBD / follow-ups

- Wireframes / layout polish.
- Bootstrap import tool from **legacy** `kBaseTargets` (`01`) for one-shot
  migration assistance.
- Optional: authorable **emotion tween duration** (today fixed ~250 ms).
