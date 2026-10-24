# Sultana Dock

Sultana's face, live in the macOS Dock. The app runs the agent bridge
itself (or connects to one you run), listens to it exactly as the robot
does, runs the robot_v3 firmware's own behaviour and face code in-process
(SultanaCore, from [`robot_v3/host/`](../robot_v3/host/README.md)), and draws
the screen the firmware renders into its Dock tile. It bounces the icon when
a turn ends, and can put the whole robot, arms and all, on your desktop.

Status: phases 1–4 of [`docs/mac-app/PLAN.md`](../docs/mac-app/PLAN.md) and
phases 1–2 of [`docs/sultana/PLAN.md`](../docs/sultana/PLAN.md) (the built-in
bridge) are done, and phase 3 (setup) has started: the app installs its
Claude Code plugin. The robot is drawn from Lachlan's Figma design (file
SCRATCHPAD, frame "SultanaExample", node 3189:9).

## Build and run

Needs Xcode 16 or later (the project uses synchronized folders), macOS 14+,
and Node.js 20+ with the bridge's packages installed, because the build
embeds the bridge:

```bash
cd plugin && npm install
```

```bash
open sultana-dock/SultanaDock.xcodeproj    # then Run the "Sultana Dock" scheme
```

or from the command line:

```bash
xcodebuild -project sultana-dock/SultanaDock.xcodeproj -scheme "Sultana Dock" -configuration Release build
```

The firmware's per-machine `robot_v3/src/config.h` must exist, as for the
host build.

The "Embed Bridge and Plugin" build phase (`bridge/embed-bridge.sh`) bundles the bridge
from `plugin/src/` and copies in the build machine's Node: `SULTANA_NODE` if
set, else the first Node 20+ on PATH, through nvm, or in Homebrew's usual
places. That Node's architectures become the bridge's. Release builds of the
app are universal, so with an arm64-only Node the build warns that the
built-in bridge won't run on Intel Macs. Point `SULTANA_NODE` at a universal
Node (`lipo -create` of the arm64 and x64 builds) to fix that. Bridge
changes reach the app when it's rebuilt.

### Installing it as a normal app

To run it without Xcode open, build a Release copy and install it:

```bash
sultana-dock/install.sh
```

That builds into `sultana-dock/build/` (gitignored), copies
`Sultana Dock.app` into /Applications (replacing and quitting an older
copy there) and opens it. `--dest DIR` installs elsewhere; `--no-open`
skips the launch. Re-run it to update after pulling changes. Then:

- Quit any copy you started from Xcode, or you'll have two Dock icons.
- Turn on Settings ▸ Open at login from the installed copy, so the login
  item points at /Applications.
- On first launch macOS asks for the bridge token in the Keychain; choose
  Always Allow. Because the build is signed "to run locally", it asks again
  after each reinstall. Choosing your team under Signing & Capabilities in
  Xcode makes the signature stable, and `install.sh` uses that setting too.

The project is signed "to run locally" (ad hoc), so it builds on any Mac
without an Apple account. The catch: macOS ties the Keychain item holding
the bridge token to the exact build, so after a rebuild it asks once for
permission to read it. Choose your own team under Signing & Capabilities
to make that go away, or use the `SULTANA_BRIDGE_TOKEN` environment
variable while developing (below).

## Setting it up

The first launch opens Settings. For Claude Code, all you need is Settings ▸
Claude Code ▸ **Install Plugin**: the app runs the bridge itself on port
8787, where the plugin's hooks go.

### Claude Code

The app carries its own copy of the `sultana` plugin
([`sultana-plugin/`](../sultana-plugin/README.md)) in
`Contents/Resources/ClaudePlugin/`, a marketplace holding just that plugin,
and installs it with your `claude` command (`ClaudePlugin.swift`):

```
claude plugin validate <ClaudePlugin>            stop here if the copy is broken
claude plugin uninstall sultana@sultana          (may fail: not installed)
claude plugin marketplace remove sultana         (may fail: not added)
claude plugin marketplace add <ClaudePlugin>
claude plugin install sultana@sultana
```

- **One button, whatever the state:** Install Plugin, Update Plugin (installed
  from another build of the app), Turn On (turned off in Claude Code) and
  Reinstall all run those steps. Remove uninstalls it and removes the
  marketplace.
- **Versions:** the build stamps the bundled plugin's version with the app's
  version and a hash of the plugin's files (`0.1.0+502dd526`), so the app
  can tell when the installed copy is from another build.
- **Finding `claude`:** apps don't get your shell's PATH, so it looks in
  `~/.local/bin`, `~/.claude/local`, Homebrew, `~/.npm-global`, `~/.bun`,
  `~/.volta` and nvm's versions, then asks your login shell
  (`command -v claude`). If it isn't found, or a step fails, Settings shows
  the commands to run yourself.
- **State** is read from Claude Code's own files (`~/.claude`, or
  `CLAUDE_CONFIG_DIR`): `plugins/installed_plugins.json` and `settings.json`.
  Settings re-reads them whenever its window comes forward.
- **Live check:** "Last hook from Claude Code 3 secs ago", from the events
  the bridge passes on. Plugins load when a Claude Code session starts, so
  after installing, start a new session or run `/reload-plugins`.
- **Duplicates:** if the older `claude-code-bridge` plugin is on, or hooks in
  `settings.json` still run `claude-hook-forward.mjs` or post to
  `/hooks/claude`, Settings warns that every hook will arrive twice.

### Bridge

Settings ▸ Bridge:

- **Use:** *Built-in bridge* (the default) or *External bridge*. Choose
  External to run a bridge yourself (`node plugin/dist/index.js`), here or on
  another computer. If you had set the app up for a bridge at another
  address or port before the built-in one existed, it stays on External.
- **Built in ▸ Let robots on my network connect:** the bridge listens on
  every interface instead of only this Mac. Robots on the same network find
  it by themselves (they sweep the subnet on port 8787) and need the token.
- **Built in ▸ Token:** made on first run and kept in the login Keychain.
  Copy it into a robot's setup, or paste your robot's token here and Save
  Token. The bridge restarts with it.
- **Built in ▸ Show Log:** opens the bridge's log in Console.
- **External ▸ Host, Port, Token:** where your bridge is. "Import from
  .env…" reads `BRIDGE_TOKEN`, `BRIDGE_PORT` and `BRIDGE_HOST` from the
  bridge's `.env` (for example `plugin/src/.env`); a `0.0.0.0` bind address
  becomes `127.0.0.1`. The token is stored in the login Keychain.

The rest:

- **When a turn ends:** bounce once (default), bounce until you switch to
  the app, or don't bounce.
- **Bounce when an agent needs permission:** Claude's "Claude needs your
  permission…" notification, or `permission.requested` from Codex/OpenCode.
- **Dock icon frame rate:** 30 (default), 15 or 10 fps. See *Cost* below.
- **Open at login** (General). This registers the app where it is now, so
  copy it to /Applications first rather than registering a build in
  DerivedData.
- **Show the robot window**, **Keep the robot window on top**, **Arm
  colour** (General). The arms default to the design's green, #8EE270.

Sultana Dock behaves like any other app when it comes to focus. Clicking
the Dock icon brings it forward (its menu bar has Settings) and shows the
robot, or opens Settings if the connection needs fixing. macOS won't bounce
the icon of the frontmost app, so turn-end bounces only show once you've
switched to something else, which is where you'll usually be. The Dock
menu shows the connection status and has Show Robot, Keep Robot on Top,
Replay Log and Settings.

## The built-in bridge

The app embeds the bridge (`Contents/Resources/bridge/bridge.mjs`, the
TypeScript bridge from `plugin/` bundled into one file) and a Node runtime
to run it (`Contents/Helpers/sultana-bridge`), and starts it as a child
process (`BuiltInBridge.swift`):

```
sultana-bridge bridge.mjs --managed --host 127.0.0.1 --port 8787
```

- **Settings travel as arguments, the token as `BRIDGE_TOKEN`.** The child
  gets a clean environment, so nothing from the shell the app started in
  (`BRIDGE_LOG_FILE`, Firebase settings) leaks in.
- **Ready line.** With `--managed` the bridge prints
  `{"event":"listening",…}` on stdout once it's listening, and the app
  connects straight away.
- **It dies with the app.** The app holds the bridge's stdin open and never
  writes to it. However the app ends (quit, crash, force quit), the pipe
  closes and the bridge exits, freeing the port. Quitting also sends
  SIGTERM and waits up to a second.
- **Restarts.** If the bridge exits, it restarts after 1 s, then 2, 4… up to
  30 s for repeated failures. Running a minute resets that.
- **Port 8787 taken.** The bridge exits with status 3 if something already
  answers on the port. The app retries every 10 s, and at once when its
  connection to whatever holds the port drops. Meanwhile it connects to
  127.0.0.1:8787 anyway, so a bridge you started yourself works if it has
  the same token: Settings says "Connected to the bridge already running on
  port 8787". To run your own bridge on 8787 for good, choose External, or
  the app takes the port back whenever it's free.
- **Logs.** The bridge's stderr goes to `~/Library/Logs/Sultana
  Dock/bridge.log`, moved to `bridge.log.1` at 2 MB. Turn logs are off; to
  keep them (for making replay fixtures), run
  `defaults write com.lachlansleight.sultana.dock bridgeTurnLogs -bool YES`
  and they go to `~/Library/Application Support/Sultana Dock/Bridge/logs/`.
- **Signing.** The runtime is signed with hardened runtime and
  `bridge/sultana-bridge.entitlements`. V8 compiles JavaScript at run time,
  so it needs `allow-jit` and `allow-unsigned-executable-memory`; without
  them Node dies at launch.
- **Robot access** binds `0.0.0.0`. With the macOS firewall on, expect a
  prompt to allow incoming connections.

## The robot on your desktop

Just the robot, floating on the desktop: no window frame, title bar or
buttons. It follows the Figma design. Back to front:

- a white body disc whose radial gradient fades to grey at the rim (the
  slight ring around the face);
- two tapered arms, the design's exact outline, in the arm colour setting;
- the face: the firmware's live screen (crisp panel pixels) inside a thin
  #ECECEC border. The border sits in front of the arms' roots, so the arms
  look like they come out of a thin body.

The arms follow the servo angle the firmware commands. The real robot has
one servo and a gear that mirrors it to both arms, so they move as a
mirrored pair, lagging the commanded angle at about 500°/s as the SG92R
servo does. They hinge where they emerge from the face border; the design
only shows the rest pose, so that hinge point is a choice (`armPivot`).
A soft shadow, not in the design, keeps the white body visible on light
desktops.

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

All of it lives in `RobotLayout` in `SultanaDock/RobotWindow.swift`, in
the Figma frame's own units, so values copy straight from the design: body
and face diameters, the gradient stops, the border, the arm outline (from
the design's SVG), its position, hinge and rest angle, which way a positive
offset moves the arms, the servo rate and the shadow. In the design the left
arm sits 2 points further out than the right; the app draws them mirrored.

## Sleep and wake

Before the Mac sleeps, the app stops drawing and closes the socket, so the
firmware sees an ordinary disconnect. On wake it reconnects straight away
instead of waiting for the heartbeat to notice the old socket is dead. The
firmware's clock pauses while the Mac sleeps.

## What it does

```
built-in bridge (child process) or an external one
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
- **The Dock tile** is the app icon, live: the robot's body as a standard
  macOS squircle, white fading to light grey toward its edges, with the
  firmware's screen set into it as large as fits, inside the same thin
  #ECECEC border as the desktop robot. It is redrawn only when the
  firmware's pixels change, at most at the frame-rate cap; the body is
  rendered once and cached.

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
| `SULTANA_BRIDGE_MODE` | `builtIn` or `external`, overriding Settings ▸ Use. |
| `SULTANA_BRIDGE_PORT` / `_TOKEN` | Override the port and token for either mode (the built-in bridge runs on that port). With a token here, the Keychain is never touched. |
| `SULTANA_BRIDGE_HOST` | Override the external bridge's host. |
| `SULTANA_REPLAY=/path/log.jsonl` | Replay a recording at launch. |
| `SULTANA_REPLAY_CLOCK=log` | Replay on the log's clock and report the frame hash (see above). |
| `SULTANA_DEBUG_DIR=/some/dir` | Once a second, write `dock.png` (the tile as drawn), `robot.png` (the robot's layers on a transparent background, while shown), `settings.png` (while Settings is open) and `state.json` (connection status, Dock push rate, the firmware's verb/expression/emotion/latch/arm); append connections, built-in bridge states, bounces, replays and sleep/wake to `events.log`. |
| `SULTANA_DEBUG_SETTINGS=1` | Open Settings at launch. |

Any setting can also be overridden for one run with a launch argument, which
UserDefaults reads without storing it, for example
`"Sultana Dock" -bridgeMode external -allowNetworkRobots YES`.

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
- `bridge/` holds the Embed Bridge and Plugin build phase's script and the
  runtime's entitlements. The phase reads `plugin/` and Node from outside the project,
  so the app target has user script sandboxing off.
- The icon art (squircle on Apple's 1024 grid, body shading, face size and
  border, shadow) lives in `SultanaDock/IconArt.swift`, shared by the live
  tile and the static icon. `tools/make_app_icon.swift` renders the static
  icon set with it, around a frame the firmware drew. The current icon is
  the thinking face, 3.5 s into the `claude-turn` fixture:

  ```bash
  robot_v3/host/build/sultana-replay robot_v3/host/fixtures/claude-turn.jsonl --frames /tmp/f --frame-every 100
  swiftc -O -parse-as-library -o /tmp/make_app_icon sultana-dock/tools/make_app_icon.swift sultana-dock/SultanaDock/IconArt.swift
  /tmp/make_app_icon /tmp/f/frame_0003500.png sultana-dock/SultanaDock/Assets.xcassets/AppIcon.appiconset
  ```

  Because the icon is already a squircle on the standard grid, it should
  look like any other Mac icon when the app isn't running. An Icon Composer
  `.icon` would add Tahoe's light/dark/tinted variants on top of that.
- `NSApp.requestUserAttention` returns 0-based ids that get reused, so 0 is
  a valid request, not a failure. The app doesn't track ids: macOS ends a
  critical bounce itself when the app is activated.
