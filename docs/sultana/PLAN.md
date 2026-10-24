# Sultana as a self-contained Claude Code plugin

**Status:** agreed 2026-10-24. Phase 1 done (`feat/sultana-plugin`); phase 2 next.
**Builds on:** [`docs/mac-app/PLAN.md`](../mac-app/PLAN.md) (SultanaCore and
Sultana Dock, phases 1–4 done).

## Goal

A friend installs Sultana and it just works with Claude Code:

1. Download Sultana Dock, drag it to Applications, open it.
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
| How the app hosts it | A bundled single-executable build of the bridge that the app launches as a background process (sidecar) and connects to over WebSocket, as it does today | Barely changes the Mac app; the same approach works for an Electron or Tauri frontend later |
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
                                       WebSocket :8787 ──► Sultana Dock (Mac; later others)
                                                       ──► robot over WiFi (token)
                                                       ──► face editor (token)
```

On the Mac the bridge is a sidecar inside Sultana Dock. Elsewhere it runs on
its own (`node plugin/dist/index.js`) or inside a future frontend.

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

### Phase 2: the bridge inside Sultana Dock

1. Build the bridge as a single portable executable (Bun `--compile` or
   Node's single executable applications; measure size and compatibility,
   then pick).
2. Configuration by arguments rather than `.env`: port, bind address, token,
   data directory; Firebase and turn logs off unless asked for.
3. The app launches and supervises it, generates and keeps the token
   (Keychain), and connects as now. Settings: built-in bridge (default) or an
   external one.
4. Signing: hardened runtime entitlements for a bundled JavaScript runtime.

### Phase 3: first-run setup

1. Find the `claude` CLI (Apple apps don't inherit the shell's PATH; check the
   usual install locations) and install the plugin; fall back to showing the
   commands.
2. Live check: wait for the first hook and say so.
3. Open at login, show the robot, and a robot pairing page that shows the
   token to enter in the robot's setup portal.

### Phase 4: shipping

Developer ID signing, notarization, a DMG on GitHub Releases, open-source
credits (TFT_eSPI, ArduinoJson, Bun or Node), and an install page.

### Later

Linux and Windows frontends; HTTP-hook plugins for other agents where they
support it; robot pairing without typing a token; mDNS instead of the subnet
sweep; listing in Anthropic's plugin directory.

## Open questions

| Question | Default |
|---|---|
| Bun or Node single executable for the sidecar | Measure in phase 2 |
| Plugin `version`: pin releases, or update on every commit to `sultana-plugin/` | No version while developing; pin when shipping |
| Turn logs in the built-in bridge | Off, with a setting |
| Codex, Cursor, OpenCode | Keep their existing forwarders for now |
