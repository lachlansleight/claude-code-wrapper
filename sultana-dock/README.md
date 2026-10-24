# Sultana Dock

Sultana's face, live in the macOS Dock. The app connects to the agent
bridge exactly as the robot does, runs the robot_v3 firmware's own
behaviour and face code in-process (SultanaCore, from
[`robot_v3/host/`](../robot_v3/host/README.md)), and draws the screen the
firmware renders into its Dock tile. It bounces the icon when a turn ends,
and can put the whole robot, arms and all, on your desktop.

Status: Phases 1–3 of [`docs/mac-app/PLAN.md`](../docs/mac-app/PLAN.md) are
done. The robot's body and arms are placeholders until the Phase 5 art.

## Build and run

Needs Xcode 16 or later (the project uses synchronized folders) and macOS 14+.

```bash
open sultana-dock/SultanaDock.xcodeproj    # then Run the "Sultana Dock" scheme
```

or from the command line:

```bash
xcodebuild -project sultana-dock/SultanaDock.xcodeproj -scheme "Sultana Dock" -configuration Release build
```

The firmware's per-machine `robot_v3/src/config.h` must exist, as for the
host build.

The project is signed "to run locally" (ad hoc), so it builds on any Mac
without an Apple account. The catch: macOS ties the Keychain item holding
the bridge token to the exact build, so after a rebuild it asks once for
permission to read it. Choose your own team under Signing & Capabilities
to make that go away, or use the `SULTANA_BRIDGE_TOKEN` environment
variable while developing (below).

## Setting it up

Start the bridge as usual (`node plugin/dist/index.js`). On first launch the
app opens Settings:

- **Host, Port, Token:** where the bridge is. "Import from .env…" reads
  `BRIDGE_TOKEN`, `BRIDGE_PORT` and `BRIDGE_HOST` from the bridge's `.env`
  (for example `plugin/src/.env`); a `0.0.0.0` bind address becomes
  `127.0.0.1`. The token is stored in the login Keychain.
- **When a turn ends:** bounce once (default), bounce until you switch to
  the app, or don't bounce.
- **Bounce when an agent needs permission:** Claude's "Claude needs your
  permission…" notification, or `permission.requested` from Codex/OpenCode.
- **Dock icon frame rate:** 30 (default), 15 or 10 fps. See *Cost* below.
- **Open at login** (General). This registers the app where it is now, so
  copy it to /Applications first rather than registering a build in
  DerivedData.
- **Show the robot window**, **Keep the robot window on top** (General).

Once it is connected, the app hands focus back to whatever you were using:
macOS won't bounce the icon of the frontmost app. Clicking the Dock icon
shows the robot window (or Settings, if the connection needs fixing). The
Dock menu shows the connection status and has Show Robot, Keep Robot on
Top, Replay Log and Settings.

## The robot on your desktop

Just the robot, floating on the desktop: no window frame, title bar or
buttons, and a soft shadow that follows its shape. Body, the round screen
(the firmware's frame, with crisp panel pixels) and two arms driven by the
servo angle the firmware commands. The real robot has one servo and a gear
that mirrors it to both arms, so the arms always move as a mirrored pair,
and the drawn arms lag the commanded angle at about 500°/s as the SG92R
servo does.

- **Move it** by dragging the robot. **Resize it** by pinching on it with a
  trackpad, or right-click ▸ Size (Small, Medium, Large).
- **Right-click** it for Keep on Top, Size, Hide Robot and Settings.
- Only the robot itself catches the mouse. Clicks anywhere else in its
  (square, invisible) window, shadow included, go to whatever is behind.
- Touching it never activates the app, so turn-end bounces keep working.
- **Keep on Top** (⌥⌘T) floats it above other windows, on every Space and
  over full-screen apps.
- Show or hide it from the Dock menu, Window ▸ Show Robot (⌘1), Settings,
  or by clicking the Dock icon. Its size and position are remembered; if
  its display has gone, it comes back centred.
- It stops redrawing while it's covered or hidden; the Dock tile carries on.

The geometry lives in `RobotLayout` in `SultanaDock/RobotWindow.swift`:
screen and body size, each arm's pivot, length and rest angle, which way a
positive offset moves the arms, and the servo rate. The placeholder follows
the face editor's arm overlay: two palms orbit the screen, at 3 and 9
o'clock when the offset is 0, rising for positive offsets. That struct is
where the Phase 5 art plugs in.

## Sleep and wake

Before the Mac sleeps, the app stops drawing and closes the socket, so the
firmware sees an ordinary disconnect. On wake it reconnects straight away
instead of waiting for the heartbeat to notice the old socket is dead. The
firmware's clock pauses while the Mac sleeps.

## What it does

```
bridge ──text frame──► BridgeSignal ──► bounce (any session, before the latch)
                  └──► SultanaEngine (the firmware: latch, verbs, emotions, face)
display link ──► engine.tick ──pixels changed──► Dock tile (≤ frame-rate cap)
```

- **Behaviour is the firmware's.** Every frame goes to the compiled
  firmware in arrival order, and the face is whatever the firmware
  renders, quirks included. The connection policy copies the robot's: a
  WebSocket ping every 15 s, a 3 s pong timeout, two misses and the socket
  is dropped, reconnect after 2 s (15 s after a rejected token).
- **Bounces see every session.** The firmware only follows one session at a
  time, so the app reads `turn.ended` and attention signals from the raw
  frames before the firmware's latch drops them.
- **The Dock tile** is the round screen in a thin bezel, sized to Apple's
  icon grid so it matches its neighbours. It is redrawn only when the
  firmware's pixels change, at most at the frame-rate cap.

## Replaying a recording

Dock menu or app menu ▸ **Replay Log…** plays a bridge recording (JSONL from
`scripts/record-ws.mjs`, such as `robot_v3/host/fixtures/claude-turn.jsonl`)
in real time instead of the live bridge, bounces included. When it ends, the
app reconnects to the bridge.

With `SULTANA_REPLAY_CLOCK=log` (below), the replay drives the firmware's
clock from the log in 1 ms steps, seeded like `sultana-replay --seed 1`, and
hashes every frame the firmware pushes. The hash lands in `events.log` and
the system log when the replay ends, and must equal the CLI's:

```bash
robot_v3/host/build/sultana-replay robot_v3/host/fixtures/claude-turn.jsonl --seed 1 --frame-hash
# frame-hash de9628a6b59d2e17 (1107 frames)
```

That proves the app runs the same firmware, fed the same way, to the
pixel.

## Developer aids

Set these in the scheme (Edit Scheme ▸ Run ▸ Arguments; they are there,
disabled) or in the shell:

| Variable | Effect |
|---|---|
| `SULTANA_BRIDGE_HOST` / `_PORT` / `_TOKEN` | Override the stored connection settings without changing them. With a token here, the Keychain is never touched. |
| `SULTANA_REPLAY=/path/log.jsonl` | Replay a recording at launch. |
| `SULTANA_REPLAY_CLOCK=log` | Replay on the log's clock and report the frame hash (see above). |
| `SULTANA_DEBUG_DIR=/some/dir` | Once a second, write `dock.png` (the tile as drawn), `robot.png` (the robot's layers on a transparent background, while shown) and `state.json` (connection status, Dock push rate, the firmware's verb/expression/emotion/latch/arm); append connections, bounces, replays and sleep/wake to `events.log`. |

Logs:

```bash
log stream --predicate 'subsystem == "com.lachlansleight.sultana.dock"' --level debug
```

The `core` category carries the firmware's own `LOG_*` output.

## Cost

Measured on this Mac (Release build, single 5 s samples with `top`, so
treat them as rough):

| Dock cap | App, code stream | Dock, code stream | App, asleep | Dock, asleep |
|---|---|---|---|---|
| 30 fps | 7.0% | 4.9% | 1.5% | 0.2% |
| 15 fps | 5.0% | 2.4% | 1.4% | 0.2% |
| 10 fps | 2.5% | 2.4% | 1.1% | 0.3% |

The Dock process idled at about 2.5% with the app closed, so its column is
noisy. The code streams are the worst case: the firmware renders them at
60 fps.

## Project notes

- Two targets: **SultanaCore** (static library: the firmware files listed in
  `FIRMWARE_SRCS` in `robot_v3/host/Makefile`, plus the host HAL and C API)
  and **Sultana Dock** (the app). The Makefile is the canonical file list;
  if a firmware source file is added, add it to both.
- The app's Swift sources live in `SultanaDock/`, a synchronized folder:
  new files there are picked up without editing the project.
- `tools/make_app_icon.py` renders the static app icon from a frame the
  firmware drew. The current icon is the thinking face, 3.5 s into the
  `claude-turn` fixture:

  ```bash
  robot_v3/host/build/sultana-replay robot_v3/host/fixtures/claude-turn.jsonl --frames /tmp/f --frame-every 100
  python3 sultana-dock/tools/make_app_icon.py /tmp/f/frame_0003500.png sultana-dock/SultanaDock/Assets.xcassets/AppIcon.appiconset
  ```

  It's a placeholder until the Phase 5 design; on Tahoe a plain icon gets
  placed in a squircle when the app isn't running.
- `NSApp.requestUserAttention` returns 0-based ids that get reused, so 0 is
  a valid request, not a failure. The app doesn't track ids: macOS ends a
  critical bounce itself when the app is activated.
