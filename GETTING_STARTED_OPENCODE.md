# Getting started — OpenCode

Wire OpenCode's plugin event system into a running `agent-bridge`.
Unlike Claude / Codex / Cursor, OpenCode hooks are **TypeScript/JS
plugins** — not stdin commands — so the wiring is "drop a `.js` file in
the right directory."

## Prerequisites

- Node.js 20+ (for the bridge itself)
- OpenCode installed; runs on Bun under the hood, which means `fetch`
  is available globally inside the plugin
- An OpenCode version with the plugin system (`hooks` returned from a
  plugin's async function — current default behaviour)

## 1. Build + start the bridge

If you haven't already:

```bash
cd claude-code-wrapper/plugin
npm install
npm run build
export BRIDGE_TOKEN="$(openssl rand -hex 32)"
node dist/index.js
```

Sanity check: `curl http://127.0.0.1:8787/api/health`.

## 2. Install the bridge plugin

OpenCode auto-loads any `.js` / `.ts` file in:

- **Global**: `~/.config/opencode/plugins/`
- **Project**: `<repo>/.opencode/plugins/`

Copy (or symlink) the helper into the global directory:

**macOS / Linux**

```bash
mkdir -p ~/.config/opencode/plugins
cp helpers/opencode-bridge-plugin.js ~/.config/opencode/plugins/agent-bridge.js
# or symlink so future updates auto-apply:
# ln -s "$PWD/helpers/opencode-bridge-plugin.js" ~/.config/opencode/plugins/agent-bridge.js
```

**Windows (PowerShell)**

```powershell
$dest = "$HOME\.config\opencode\plugins"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item helpers\opencode-bridge-plugin.js "$dest\agent-bridge.js"
```

## 3. Set the env vars OpenCode will inherit

Export these before starting OpenCode (shell profile is the easiest):

```bash
export BRIDGE_TOKEN="paste-the-same-token-here"
export BRIDGE_URL="http://127.0.0.1:8787"   # only if not the default
```

OpenCode passes the host environment to plugins, so the plugin reads
these at load time. Without `BRIDGE_TOKEN` the plugin loads but no-ops
(and prints a one-line warning to OpenCode's log).

## 4. Verify

Start OpenCode normally. You should see in its log:

```
[agent-bridge] forwarding to http://127.0.0.1:8787
```

Open `examples/ws-client.html`, paste your token, click **Connect**.
Send any prompt in OpenCode — you should see `agent_event` frames with
`agent: "opencode"` and a sequence of `event.kind` values:
`turn.started` → `message.user` → `activity.started` /
`activity.finished` for each tool call → `message.assistant` →
`turn.ended`.

If nothing arrives:

- OpenCode logs plugin errors via its built-in logger; check the TUI
  log panel or the OpenCode log file.
- Manual test:
  ```bash
  curl -X POST http://127.0.0.1:8787/hooks/opencode \
    -H "Authorization: Bearer $BRIDGE_TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"hook_type":"session.created","payload":{"sessionID":"test"}}'
  ```

## What the plugin subscribes to

| OpenCode event         | Emitted as                                      |
|------------------------|-------------------------------------------------|
| `session.created`      | `session.started`                               |
| `session.deleted`      | `session.ended`                                 |
| `session.idle`         | `turn.ended`                                    |
| `session.compacted`    | `context.compacting`                            |
| `tool.execute.before`  | `activity.started`                              |
| `tool.execute.after`   | `activity.finished` (or `activity.failed`)      |
| `permission.asked`     | `permission.requested`                          |
| `permission.replied`   | `permission.resolved`                           |
| `todo.updated`         | `todo.updated`                                  |
| `message.updated` (user)      | `turn.started` + `message.user` (first sighting per session) |
| `message.updated` (assistant) | `message.assistant { final: true }`      |
| `message.part.updated` (thinking) | `thinking`                          |

The bridge dedupes user-role `message.updated` per session so repeated
updates to the same user message don't re-emit `turn.started`.

## Notes

- The plugin is **observe-only** — none of the hooks return a value, so
  OpenCode treats them as listeners rather than gates. It will never
  block tool execution or rewrite messages.
- `fetch` is fire-and-forget with a 500ms abort. A down bridge never
  holds up the agent loop.
- If you also use the npm-package style of plugin loading (the `plugin`
  array in `opencode.json`), this drop-in file approach is independent
  and they coexist fine.
