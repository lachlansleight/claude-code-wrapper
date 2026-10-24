# Sultana as a self-contained Claude Code plugin

**Status:** agreed 2026-10-24. Phases 1–3 done (`feat/sultana-plugin`); phase 4 next.
**Builds on:** [`docs/mac-app/PLAN.md`](../mac-app/PLAN.md) (SultanaCore and
the Sultana app, phases 1–4 done).

## Goal

A friend installs Sultana and it just works with Claude Code:

1. Download Sultana, drag it to Applications, open it.
2. Click "Connect Claude Code". The app installs the Sultana plugin.
3. Start or restart a Claude Code session. Sultana reacts to the first hook,
   confirming it works.
4. Optionally: open at login, show Sultana on the desktop, pair a robot.

No `.env` files, no shell configuration, no Node to install, and no token
unless they own a robot.

Linux and Windows come later. Everything except each platform's interface
must be portable: the bridge, the plugin and the firmware core.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Bridge language | **TypeScript**: the existing bridge, refactored where needed | Already runs on all three platforms; one copy of the parsing and server logic, shared by the standalone bridge and the app |
| How the app hosts it | The app embeds a **Node runtime and the bridge bundled into one JavaScript file**, launches it as a background process (sidecar) and connects over WebSocket, as it does today | Barely changes the Mac app; the same approach works for an Electron or Tauri frontend later. Why not a single executable: see phase 2 |
| How hooks reach the bridge | Claude Code **HTTP hooks** POST each hook's JSON straight to `http://127.0.0.1:8787/hooks/claude`. `SessionStart` can't be an HTTP hook, so it runs `curl` in the background | No Node per hook, no forwarder script, no shell setup |
| Auth for hooks | The bridge accepts hook POSTs **from this machine without a token**, if they are JSON and carry no `Origin` header (so web pages can't forge them). Everything from the network still needs the token | HTTP hook headers can only use allow-listed environment variables, which would bring the shell setup back |
| Mid-turn text | The **bridge** reads new assistant text from the transcript (`transcript_path` is in every payload), as the forwarder did | Claude's hooks don't carry it; the receiving end runs on the same machine |
| Port | Fixed at **8787** | The plugin's URLs can't be configured per user; the robot and face editor already use 8787 |
| Plugin home | A new tiny `sultana-plugin/` folder, listed in this repo's marketplace (`.claude-plugin/marketplace.json`, marketplace name `sultana`) | The old plugin pointed at all of `plugin/`, 512 MB with logs; installing copies the folder |
| Installing the plugin | The app runs `claude plugin marketplace add` and `claude plugin install`; copyable commands as a fallback | Both run without prompts |
| Standalone bridge | Stays, for Linux/Windows, robot-only setups and development. It accepts both the new raw hooks and the old forwarder's wrapped ones | Same code as the bundled one |

## Architecture

```
Claude Code ── HTTP hooks (+ curl for SessionStart) ──► bridge (TypeScript)
                                                        │  parse → AgentEvent
                                                        │  transcript → mid-turn text
                                                        ▼
                                       WebSocket :8787 ──► the Sultana app (Mac; later others)
                                                       ──► robot over WiFi (token)
                                                       ──► face editor (token)
```

On the Mac the bridge is a sidecar inside the Sultana app. Elsewhere it runs on
its own (`node plugin/dist/index.js`) or inside a future frontend.

```
Sultana.app/Contents/
  MacOS/Sultana                     the app
  Helpers/sultana-bridge            Node, stripped, hardened runtime + JIT entitlements
  Resources/bridge/bridge.mjs       the bridge and ws, one file (plugin/scripts/bundle.mjs)
  Resources/bridge/licenses/        Node's and ws's licences
```

The app runs `sultana-bridge bridge.mjs --managed --host 127.0.0.1 --port
8787` with `BRIDGE_TOKEN` as its only setting and holds the child's stdin
open; see [`sultana-dock/README.md`](../../sultana-dock/README.md#the-built-in-bridge).

## Phases

### Phase 1: plugin and hook path (this branch)

1. `sultana-plugin/`: `plugin.json` and `hooks/hooks.json` with HTTP hooks for
   the hooks the Claude adapter handles, a short timeout, and a background
   `curl` command for `SessionStart` (no quotes, so it works in Windows'
   shell too).
2. Marketplace: rename to `sultana`, list `sultana` (new) and keep
   `claude-code-bridge` (old forwarder) as legacy. Never enable both: every
   hook would arrive twice.
3. Bridge: `/hooks/claude` accepts Claude's raw payload (has
   `hook_event_name`) as well as `{hook_type, payload}`; the loopback rule
   above; transcript reading for mid-turn text; `Stop` falls back to Claude's
   `last_assistant_message`.
4. Verify with the plugin validator and a real `claude -p` session using
   `--plugin-dir` against a throwaway bridge.

**Done when:** a Claude Code session with only the new plugin drives the
bridge exactly as the old forwarder did, mid-turn text included, with no
`BRIDGE_TOKEN` in the shell.

*Status: done.* Checked:
- Both manifests pass `claude plugin validate` (Claude Code 2.1.116).
- A real `claude -p` session loading the plugin with `--plugin-dir` against a
  throwaway bridge delivered `SessionStart` (via `curl`),
  `UserPromptSubmit`, `StopFailure` and `SessionEnd` as raw HTTP hooks with
  no token, and the bridge emitted the right events. The CLI on this Mac
  isn't logged in, so that session ended on an auth error before any tool
  call.
- Tool calls and mid-turn text were checked by posting Claude-shaped hooks
  with a real transcript file: the text written before a tool call arrives
  as `thinking`, a half-written transcript line is left until it's
  complete, and `Stop` falls back to `last_assistant_message`.
- Hooks with a browser `Origin` header, or not JSON, are refused without a
  token; the legacy wrapped format still works.
- Not yet checked: a logged-in session with tool calls end to end.

### Phase 2: the bridge inside the Sultana app

1. Build the bridge as a single portable executable (Bun `--compile` or
   Node's single executable applications; measure size and compatibility,
   then pick).
2. Configuration by arguments rather than `.env`: port, bind address, token,
   data directory; Firebase and turn logs off unless asked for.
3. The app launches and supervises it, generates and keeps the token
   (Keychain), and connects as now. Settings: built-in bridge (default) or an
   external one.
4. Signing: hardened runtime entitlements for a bundled JavaScript runtime.

*Status: done.* What was built:

- **Runtime: Node plus a bundled file, not a single executable.** esbuild
  bundles the bridge and `ws` into one 206 KB ES module
  (`npm run bundle` in `plugin/`). The Xcode build phase "Embed Bridge and Plugin"
  (`sultana-dock/bridge/embed-bridge.sh`) puts it in the app with a copy of
  the build machine's Node, stripped of local symbols, hardened, and signed
  with `allow-jit` and `allow-unsigned-executable-memory`. Without those two
  entitlements Node dies at launch under hardened runtime (checked: exit
  133). Inside an app bundle a single executable buys nothing: the `.app` is
  already the single thing you ship. Node single executables would also need
  `postject` and re-signing per build. Bun would be about half the size, but
  it's a second runtime the bridge has never run on, and another toolchain
  on the build machine. Measured: Node 23.11 is 114 MB, 90 MB stripped,
  33 MB for the whole zipped Release app (89 MB on disk). The app's contract
  with the bridge (executable, arguments, `BRIDGE_TOKEN`, a ready line) is
  the same whichever runtime, so switching later is local to the build
  phase.
- **Arguments:** `--host`, `--port`, `--data-dir`, `--turn-logs` and
  `--managed`. The token stays in `BRIDGE_TOKEN` so it never shows in `ps`.
  `--managed` ignores `.env`, turns turn logs off, prints
  `{"event":"listening",…}` on stdout once listening, and exits when stdin
  closes. Exit status 3 means the port is taken. The app passes a clean
  environment, so a shell's `BRIDGE_LOG_FILE` or Firebase settings never
  reach it.
- **Port check:** before listening, the bridge (standalone too) checks
  whether something already answers on loopback. macOS lets a `127.0.0.1`
  listener bind over another process's `0.0.0.0` one, and the newer one then
  takes all local traffic, so without the check a second bridge would
  silently capture the first one's hooks.
- **Supervision** (`BuiltInBridge.swift`): exit status 3 is retried every
  10 s, and at once when the app's connection to whatever holds the port
  drops. Any other exit restarts after 1, 2, 4… up to 30 s. Quitting sends
  SIGTERM and waits up to 1 s. A crash or force quit closes the stdin pipe
  and the bridge follows. While another bridge holds 8787 the app connects
  to it with its token, so a standalone bridge with the same token keeps
  working.
- **Settings:** Use ▸ Built-in bridge (default) or External bridge. Built
  in: Let robots on my network connect (binds `0.0.0.0`), the token (made on
  first run, kept in the Keychain, with Copy) and Show Log. Users who had an
  external bridge at another address or port stay on External.
- **Logs:** stderr goes to `~/Library/Logs/Sultana/bridge.log` (rotated
  at 2 MB). Turn logs are off; the hidden setting `bridgeTurnLogs` writes
  them under `~/Library/Application Support/Sultana/Bridge/logs/`.

Checked with test builds on a spare port:
- Startup: listening and connected about 110 ms after launch.
- Token-less hooks drive the face.
- The child's environment holds only `PATH`, `TMPDIR`, `HOME` and
  `BRIDGE_TOKEN`.
- Crashes: `kill -9` of the bridge three times running restarted it after
  1, 2 and 4 s.
- Shutdown: a normal quit (SIGTERM) and a force quit of the app (stdin
  closed) both free the port.
- Another bridge on the port: the app used it, then took over 55 ms after
  it stopped.
- Robot access binds `*:port`. External mode starts no child.
- Release builds are universal and pass `codesign --verify --deep --strict`.

Not checked:
- A real Claude Code session against the built-in bridge on 8787 (a running
  bridge held that port on the development Mac).
- Hardened runtime on the app itself (phase 4).
- An Intel Mac.

### Phase 3: first-run setup

1. Find the `claude` CLI (Apple apps don't inherit the shell's PATH; check the
   usual install locations) and install the plugin; fall back to showing the
   commands.
2. Live check: wait for the first hook and say so.
3. Open at login, show the robot, and a robot pairing page that shows the
   token to enter in the robot's setup portal.

*Status: done.*
- **Plugin source:** the app carries its own copy of the plugin, as a
  one-plugin marketplace in `Contents/Resources/ClaudePlugin/`, rather than
  using the GitHub marketplace. That keeps the hooks in step with the
  bundled bridge, works offline, and doesn't depend on what's on `main`.
  The build stamps the plugin's version (`<app version>+<hash>`) so the app
  can offer an update.
- **Settings ▸ Claude Code:**
  - Install Plugin, Update Plugin, Turn On or Reinstall: one idempotent
    sequence, validating first so a broken copy removes nothing.
  - Remove.
  - A live "last hook" line.
  - Warnings when something else also forwards hooks.
  - The commands to run yourself when `claude` can't be found or a step
    fails.
- **First launch:** Settings opens on its new Setup tab (Claude Code, Robot,
  Start-up). Bridge and General are the other tabs; a broken connection
  opens Bridge.
- **Checked:**
  - Install, reinstall, remove, outdated, turned off, duplicate warnings and
    a broken copy, all against throwaway `CLAUDE_CONFIG_DIR`s, including one
    holding a copy of a real config's stale marketplace entry.
  - Finding `claude` both ways (known paths and the login shell).
  - Each state's Settings rendering.
- **Not checked:** the button against a real `~/.claude`, or a machine
  without Claude Code.
- **Robot pairing** (Setup ▸ Robot):
  - the robot-access switch;
  - the portal steps (BOOT at power-on, join `robot-…`, open
    192.168.4.1);
  - the values to type there: this Mac's IPv4 address, the port and the
    token, with Copy;
  - "Connected at 192.168.1.42" once a robot connects.

  For that, the bridge now records each WebSocket client's address and
  User-Agent and serves them at `GET /api/clients`. The robot's WebSocket
  library always sends `arduino-WebSocket-Client`, so no firmware change was
  needed. Checked with a fake robot client (that User-Agent, on a test
  bridge): Settings showed it connected within one poll, and folded the steps
  away. Not checked: a real robot pairing through its portal (no hardware
  here).

### Phase 4: shipping

Developer ID signing, notarization, a DMG on GitHub Releases, open-source
credits (TFT_eSPI, ArduinoJson, Node, ws; the bridge's are already in
`Resources/bridge/licenses/`), and an install page. Build with a pinned
Node LTS made universal (`lipo` of the official arm64 and x64 builds, via
`SULTANA_NODE`): the build machine's Node is arm64-only here, and the build
warns that the bridge won't run on Intel Macs. Turn on hardened runtime for
the app.

### Later

Linux and Windows frontends; HTTP-hook plugins for other agents where they
support it; robot pairing without typing a token; mDNS instead of the subnet
sweep; listing in Anthropic's plugin directory.

## Open questions

| Question | Default |
|---|---|
| Plugin `version`: pin releases, or update on every commit to `sultana-plugin/` | No version while developing; pin when shipping |
| Turn logs in the built-in bridge | Off; hidden `bridgeTurnLogs` default (phase 2) |
| Codex, Cursor, OpenCode | Keep their existing forwarders for now |
