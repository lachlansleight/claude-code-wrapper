# Sultana for Mac

Sultana's face, live in the macOS Dock. The app runs the agent bridge
itself (or connects to one you run), listens to it exactly as the robot
does, runs the robot_v3 firmware's own behaviour and face code in-process
(SultanaCore, from [`robot_v3/host/`](../robot_v3/host/README.md)), and draws
the screen the firmware renders into its Dock tile. It bounces the icon when
a turn ends, and can put the whole robot, arms and all, on your desktop.

Status: phases 1–4 of [`docs/mac-app/PLAN.md`](../docs/mac-app/PLAN.md) and
phases 1–3 of [`docs/sultana/PLAN.md`](../docs/sultana/PLAN.md) (the built-in
bridge, installing the Claude Code plugin, pairing a robot) are done. The robot is drawn from Lachlan's Figma design (file
SCRATCHPAD, frame "SultanaExample", node 3189:9).

To install the app rather than build it, see
[`docs/INSTALL.md`](../docs/INSTALL.md).

## Build and run

Needs an Apple silicon Mac (the app is arm64 only, like the Node it ships),
Xcode 16 or later (the project uses synchronized folders), macOS 14+, and
Node.js 20+ with the bridge's packages installed, because the build embeds
the bridge:

```bash
cd plugin && npm install
```

```bash
open sultana-dock/SultanaDock.xcodeproj    # then Run the "Sultana" scheme
```

or from the command line:

```bash
xcodebuild -project sultana-dock/SultanaDock.xcodeproj -scheme Sultana -configuration Release build
```

The firmware's per-machine `robot_v3/src/config.h` must exist, as for the
host build.

The "Embed Bridge and Plugin" build phase (`bridge/embed-bridge.sh`) bundles
the bridge from `plugin/src/` and copies in a Node runtime to run it. That
runtime is the Node release pinned in `bridge/node.env` (24.22.1 LTS, with
its SHA-256), which `bridge/fetch-node.sh` downloads from nodejs.org and
verifies; `install.sh` and `release.sh` run it. Before the first download,
Xcode builds fall back to the Node on your machine. `SULTANA_NODE` overrides
both. Bridge changes reach the app when it's rebuilt.

### Installing it as a normal app

To run it without Xcode open, build a Release copy and install it:

```bash
sultana-dock/install.sh
```

That builds into `sultana-dock/build/` (gitignored), copies
`Sultana.app` into /Applications (replacing and quitting an older
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
without an Apple account, with hardened runtime on, as releases have. The
catch: macOS ties the Keychain item holding the bridge token to the exact
build, so after a rebuild it asks once for permission to read it. Choose
your own team under Signing & Capabilities to make that go away, or use the
`SULTANA_BRIDGE_TOKEN` environment variable while developing (below).

### Making a release

```bash
sultana-dock/release.sh
```

That builds `sultana-dock/build/release/Sultana-<version>.dmg` (and a
`.sha256` beside it) for other people's Macs:

1. A Release build signed with your **Developer ID Application**
   certificate, with hardened runtime and secure timestamps. The bridge's Node
   runtime is signed the same way, with the JIT entitlements V8 needs
   (`bridge/sultana-bridge.entitlements`).
2. The app is sent to Apple's notary service, and the ticket stapled to it
   so it opens offline.
3. A disk image holding the app and an Applications shortcut, itself signed,
   notarised and stapled. The script finishes by asking Gatekeeper to assess
   both.

The version is `MARKETING_VERSION` in the Xcode project (bump it there); the
build number is the commit count, so it always goes up. The script refuses
to build with uncommitted changes unless you pass `--allow-dirty`.

It needs, once:

- a Developer ID Application certificate in the login keychain (Xcode ▸
  Settings ▸ Accounts ▸ your team ▸ Manage Certificates ▸ + ▸ Developer ID
  Application; only the account holder can make one);
- notarisation credentials stored in the keychain, using an app-specific
  password from account.apple.com:

  ```bash
  xcrun notarytool store-credentials sultana-notary --apple-id <your Apple ID email> --team-id <your team ID>
  ```

`SULTANA_IDENTITY` picks a certificate by name if you have several (`-`
signs ad hoc to try the script out, without notarising), and
`SULTANA_NOTARY_PROFILE` names other stored credentials. `--no-notarize`
does everything except talk to Apple.

Then attach the DMG to a GitHub release; [`docs/INSTALL.md`](../docs/INSTALL.md)
is the page to point people at.

## Setting it up

Settings has three tabs: **General** (Start-up, Robot window, Dock), **Setup**
(Claude Code, Robot) and **Bridge**. It opens on General, except on the
first launch, which opens Setup. For Claude Code, all you need
is Setup ▸ Claude Code ▸ **Install Plugin**: the app runs the bridge itself
on port 8787, where the plugin's hooks go. When the connection needs fixing,
Settings opens on Bridge instead.

### Setup ▸ Claude Code

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

### Setup ▸ Robot

Pairing a robot means typing four things into its setup portal: your WiFi,
the bridge host, the port and the token. This section has the steps and the
values, and says when a robot connects.

- **Let robots on my network connect** (built-in bridge only): the bridge
  listens on every interface instead of only this Mac.
- **Robot:** "Connected at 192.168.1.42", from the bridge's
  `GET /api/clients` (polled every 3 s while the tab shows). A robot is a
  client whose User-Agent is `arduino-WebSocket-Client`, which the
  firmware's WebSocket library always sends.
- **How to set up a robot** (folded until you open it, and folded again
  each time Settings closes):
  1. Hold the robot's BOOT button while you plug it in, until its screen
     says CONFIG MODE.
  2. Join the open `robot-…` network it shows, and open the address under
     it (usually http://192.168.4.1).
  3. Enter your WiFi, and the bridge host, port and token shown here, and
     save.

  The robot restarts, probes that host, then sweeps the subnet if the Mac's
  address has changed
  ([`docs/firmware/OVERVIEW.md`](../docs/firmware/OVERVIEW.md#provisioning-portal)).
- **Bridge host** is this Mac's IPv4 address (Wi-Fi or Ethernet), or an
  external bridge's host if it's on another computer.
- **Token:** made on first run and kept in the login Keychain, with Show and
  Copy. To keep a robot's existing token, paste it here and Save Token; the
  bridge restarts with it.

### Bridge

- **Use:** *Built-in bridge* (the default) or *External bridge*. Choose
  External to run a bridge yourself (`node plugin/dist/index.js`), here or on
  another computer. If you had set the app up for a bridge at another
  address or port before the built-in one existed, it stays on External.
- **Bridge URL** (built in): where the bridge listens and the plugin's hooks
  go, `http://127.0.0.1:8787`.
- **Status:** Connected, Starting…, Port in use, Restarting…, Token
  rejected and so on, with a sentence more when there's something to
  explain. The Dock menu shows the same.
- **Built in ▸ Show Log:** opens the bridge's log in Console.
- **External ▸ Host, Port, Token:** where your bridge is. "Import from
  .env…" reads `BRIDGE_TOKEN`, `BRIDGE_PORT` and `BRIDGE_HOST` from the
  bridge's `.env` (for example `plugin/src/.env`); a `0.0.0.0` bind address
  becomes `127.0.0.1`. The token is stored in the login Keychain.

### The rest

- **Open at login** (General ▸ Start-up). This registers the app where it
  is now, so copy it to /Applications first rather than registering a build
  in DerivedData. Apps registered as Sultana Dock before the rename need it
  turned off and on again.
- **Show the robot on the desktop**, **Keep the robot on top of other
  windows**, **Body colour** and **Arm colour** (General ▸ Robot window).
  The body defaults to white and the arms to the design's green, #8EE270.
  The body's rim and the face's border are always shaded darker than the
  body colour, as the design's white body fades to grey.
- **When a turn ends** (General): bounce once (default), bounce until you
  switch to the app, or don't bounce.
- **Bounce when an agent needs permission** (General): Claude's "Claude needs
  your permission…" notification, or `permission.requested` from
  Codex/OpenCode.
- **Dock icon frame rate** (General): 30 (default), 15 or 10 fps. See *Cost*
  below.

The Sultana app behaves like any other app when it comes to focus. Clicking
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
  and they go to `~/Library/Application Support/Sultana/Bridge/logs/`.
- **Signing.** The runtime is signed with hardened runtime and
  `bridge/sultana-bridge.entitlements`. V8 compiles JavaScript at run time,
  so it needs `allow-jit` and `allow-unsigned-executable-memory`; without
  them Node dies at launch.
- **Robot access** binds `0.0.0.0`. With the macOS firewall on, expect a
  prompt to allow incoming connections.

## The robot on your desktop

Just the robot, floating on the desktop: no window frame, title bar or
buttons. It follows the Figma design. Back to front:

- a contact shadow on the "ground" under the robot (frame 3190:15's
  ellipse): black fading to clear, blurred, behind everything else;
- a body disc in the body colour (white by default) whose radial gradient
  darkens toward the rim (the slight ring around the face);
- two tapered arms, the design's exact outline, in the arm colour setting;
- the face: the firmware's live screen (crisp panel pixels) inside a thin
  border in the body colour, slightly darker (#ECECEC on a white body). The
  border sits in front of the arms' roots, so the arms look like they come
  out of a thin body.

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
- **The Dock tile** is the app icon, live: the firmware's screen filling a
  standard macOS squircle. The screen is drawn 1.285 times the squircle's
  side and cropped by it, so its black reaches every edge and corner and
  all that's lost is the rim (the mood ring, except for slivers in the
  corners). It is redrawn only when the firmware's pixels change, at most
  at the frame-rate cap; the squircle and its shadow are rendered once and
  cached.

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
| `SULTANA_DEBUG_SETTINGS=setup` | Open Settings at launch, on that tab (`setup`, `bridge` or `general`). |

Any setting can also be overridden for one run with a launch argument, which
UserDefaults reads without storing it, for example
`Sultana -bridgeMode external -allowNetworkRobots YES`.

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
  and **Sultana** (the app). The Makefile is the canonical file list;
  if a firmware source file is added, add it to both.
- The app's Swift sources live in `SultanaDock/`, a synchronized folder:
  new files there are picked up without editing the project.
- `bridge/` holds the Embed Bridge and Plugin build phase's script and the
  runtime's entitlements. The phase reads `plugin/` and Node from outside the project,
  so the app target has user script sandboxing off.
- The icon art (squircle on Apple's 1024 grid, the screen's size and crop,
  shadow) lives in `SultanaDock/IconArt.swift`, shared by the live
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
