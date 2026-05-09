# Bridge Integration

The editor talks to a pre-existing service called **the bridge**,
which already runs locally on every developer workstation as part of
the firmware monorepo. The bridge sits between agentic CLIs (Claude
Code, Codex, etc.) and the ESP32-S3 robot firmware. From the
editor's point of view it is just **the thing that can move the real
robot**.

- **Address:** `http://localhost:8787` (HTTP) and `ws://localhost:8787`
  (WebSocket — same port).
- **Always-on:** in this dev setup the bridge is assumed to be
  running. The editor should detect it (`GET /api/health`) and show
  a "bridge offline" indicator if it's down, but the editor itself
  stays usable for authoring without a live device.
- **CORS:** the bridge sets `Access-Control-Allow-Origin: *` and
  permits `Authorization, Content-Type, Accept, X-Requested-With`.
  The browser can call it directly without a proxy through Next.js.
- **Auth:** all endpoints except `GET /api/health` require a bearer
  token in `Authorization: Bearer <token>`. The token is the bridge
  config token (typically loaded from a local `.env`). The editor
  needs the user to provide / paste this token once and persist it.

## What the editor uses the bridge for

In v1, the editor uses the bridge to **drive the live face on the
real robot** so the user can sanity-check that what the editor's
preview shows matches what the device renders.

The runtime pipeline lives in firmware (see
[`06_RENDER_PIPELINE_OVERHAUL.md`](06_RENDER_PIPELINE_OVERHAUL.md)).
**Bespoke overlay pixels** (`EffectsRenderer`) are **not** exposed on the
bridge — preview uses the same **V/A + verb + palette** hooks as a human
operator. To "preview on device" the editor v1 typically:

1. Set `(valence, activation)` so the emotion blend lands where the
   user wants.
2. Optionally start a verb so its timeline kicks in.
3. Optionally set palette colours.

That maps directly to bridge endpoints that already exist.

### Useful endpoints (already exist)

All POSTs send `Content-Type: application/json` and require auth.

| Endpoint | Body | Purpose |
|---|---|---|
| `GET /api/health` | — | Liveness probe. Public. Returns `{ ok, version, uptime_seconds, agents }`. |
| `POST /api/raw/emotion/set-valence` | `{ "v": 0.4 }` | Snap firmware valence. |
| `POST /api/raw/emotion/set-arousal` | `{ "a": 0.7 }` | Snap firmware arousal. |
| `POST /api/raw/emotion/set-both` | `{ "v": 0.4, "a": 0.7 }` | Snap both at once. |
| `POST /api/raw/emotion/modify-valence` | `{ "delta_v": 0.1 }` | Relative nudge. |
| `POST /api/raw/emotion/modify-arousal` | `{ "delta_a": 0.1 }` | Relative nudge. |
| `POST /api/raw/verb/start` | `{ "verb": "thinking" }` | Start a verb. Recognised verbs: `none`, `thinking`, `reading`, `writing`, `executing`, `straining`, `sleeping`, `waking`, `attracting_attention`. |
| `POST /api/raw/verb/clear` | `{}` | Clear active verb. |
| `POST /api/raw/verb/overlay` | `{ "verb": "waking", "duration_ms": 1000 }` | Transient one-shot overlay. |
| `POST /api/raw/config/display-mode` | `{ "display_mode": "face" }` | `face` or `text`. |
| `POST /api/raw/config/set-color` | `{ "color": <NamedColor idx>, "r": 0, "g": 0, "b": 0 }` | Push a palette slot. |
| `POST /api/raw/broadcast` | any JSON object | Verbatim broadcast to every connected WS client (incl. firmware). Use sparingly; the firmware only handles known message types. |
| `GET  /api/raw/capabilities` | — | Catalogue of the above (the bridge serves its own docs). |

### WebSocket

`ws://localhost:8787` (same port as HTTP). After auth the bridge
sends JSON envelopes: `agent_event` (lifecycle hooks from any agent
CLI), `permission_request`, `permission_resolved`, etc. The editor
**probably does not need any of these** for v1 — they're agent-side
events, not face-side. Listed here so the future agent doesn't get
distracted: ignore unless you discover a reason to consume them.

The same WS channel also receives the broadcasts from
`POST /api/raw/broadcast` if the editor wants to subscribe to its
own commands (useful for "did the message reach the firmware"
diagnostics — but the firmware itself doesn't ack).

## What the bridge does NOT currently do

- **Push tentative `FaceParams` to firmware.** There is no endpoint
  for "render this face row right now". Live preview on device is
  therefore limited to "set V/A and watch the firmware blend". If we
  want true authoring-on-hardware (drag a slider → eye widens
  immediately on the robot), we'd need:
  - a new firmware-side handler for a raw-broadcast message like
    `{ type: "preview_face_params", params: {...} }`,
  - a matching helper endpoint or the editor can use
    `POST /api/raw/broadcast` directly.
  - This is **out of scope for v1**; flagged as a future capability.

- **Trigger a build / upload to the device.** The bridge does not
  flash firmware. The user still opens the Arduino IDE and presses
  Build → Upload after exporting `FACE_CONFIG.h`.

- **Read or mutate `FACE_CONFIG.h`.** That's the editor's Next.js
  route handlers, not the bridge.

## Recommended editor → bridge UX

1. On boot, `GET /api/health`. If 200 → show "bridge connected, vN".
   If unreachable → show "bridge offline" badge; do not error.
2. A **"Preview on device"** toggle in the editor header. When on:
   - Whenever the user moves the V/A cursor in Blend Mode, throttle
     to ~10 Hz and POST `/api/raw/emotion/set-both`.
   - When the user opens a verb's timeline, POST
     `/api/raw/verb/start` with that verb. When they leave or
     toggle it off, POST `/api/raw/verb/clear`.
   - When the toggle goes off, restore an idle state (`set-both
     v=0, a=0` and `verb/clear`).
3. Show a tiny "last sent" log line in the corner so the user can
   tell the editor is talking to the bridge.

## Settings the editor needs to persist

| Key | Example | Source |
|---|---|---|
| Bridge base URL | `http://localhost:8787` | Default; rarely needs override. |
| Bridge auth token | `dev_xxx…` | User-provided; stored in localStorage or `.env.local`. |
| Firmware repo path | `C:/Users/me/Dev/claude-code-wrapper` | User-provided via Next.js env (`FIRMWARE_REPO_PATH`); used by `app/api/export/route.ts` to locate `robot_v3/src/face/FACE_CONFIG.h` and `scripts/gen_emotion_triangulation.py`. |
| Last loaded `FACE_CONFIG.h` mtime | unix ms | For "file changed on disk, reload?" detection. |
