# Getting started — Cursor

Wire Cursor 1.7+ agent hooks into a running `agent-bridge`. Cursor has
no plugin/marketplace concept for hooks — you drop a `hooks.json` file
that points at a forwarder script, and Cursor picks it up.

## Prerequisites

- Node.js 20+
- Cursor **1.7 or newer** (older versions don't support hooks at all)

## 1. Build + start the bridge

If you haven't already (see [CLAUDE_CODE.md](CLAUDE_CODE.md)
for the long version):

```bash
cd claude-code-wrapper/plugin
npm install
npm run build
export BRIDGE_TOKEN="$(openssl rand -hex 32)"
node dist/index.js
```

Sanity check: `curl http://127.0.0.1:8787/api/health`.

## 2. Drop in `hooks.json`

Cursor reads from any of these (highest priority first):

- **Project**: `<repo>/.cursor/hooks.json`
- **User**: `~/.cursor/hooks.json`

User-level catches everything you do in Cursor. Project-level scopes to
one repo. Pick whichever fits.

Save this as `~/.cursor/hooks.json` (replace `<absolute-path>` with the
real path to your clone — forward slashes work on Windows too):

```json
{
  "version": 1,
  "hooks": {
    "sessionStart":         [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "sessionEnd":           [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "beforeSubmitPrompt":   [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "preToolUse":           [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "postToolUse":          [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "beforeShellExecution": [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "afterShellExecution":  [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "beforeMCPExecution":   [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "afterMCPExecution":    [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "beforeReadFile":       [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "afterFileEdit":        [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }],
    "stop":                 [{ "command": "node <absolute-path>/helpers/cursor-hook-forward.mjs" }]
  }
}
```

The forwarder reads Cursor's stdin payload, derives the event name
from `hook_event_name`, and POSTs `{ hook_type, payload }` to
`/hooks/cursor`. It always prints `{}` and exits 0, so it never blocks
Cursor's agent loop.

## 3. Tell the forwarder about your token

The script reads `BRIDGE_TOKEN` from its environment. Two options:

**A. Export it in your shell profile** (`.zshrc` / `.bashrc` /
PowerShell `$PROFILE`):

```bash
export BRIDGE_TOKEN="paste-the-same-token-here"
export BRIDGE_URL="http://127.0.0.1:8787"   # only if not the default
```

Then restart Cursor so it inherits the env.

**B. Wrap the command** in `hooks.json`:

```json
{ "command": "BRIDGE_TOKEN=xxx node <absolute-path>/helpers/cursor-hook-forward.mjs" }
```

(Windows `cmd.exe` users: use `set BRIDGE_TOKEN=xxx && node ...` instead.)

## 4. Verify

Open `examples/ws-client.html`, paste your token, click **Connect**.
Open Cursor's chat (Cmd+L / Ctrl+L) and ask it to do anything. You
should see `agent_event` frames with `agent: "cursor"` and a sequence
of `event.kind` values: `turn.started` → `message.user` →
`activity.started` / `activity.finished` for each tool call →
`message.assistant` → `turn.ended`.

The narrow Cursor hooks (`beforeShellExecution`, `afterFileEdit`, etc.)
fire alongside `preToolUse`/`postToolUse` — the bridge does **not**
double-emit activities for them. They feed a per-`tool_use_id` summary
cache that gets attached to the next matching `activity.started`.

If nothing shows up:

- Open Cursor's developer console (Help → Toggle Developer Tools) and
  filter for `hook` — Cursor logs hook execution there.
- The forwarder writes errors to stderr; Cursor surfaces those in the
  same console.
- `curl -X POST http://127.0.0.1:8787/hooks/cursor -H "Authorization: Bearer $BRIDGE_TOKEN" -H "Content-Type: application/json" -d '{"hook_type":"sessionStart","payload":{"conversation_id":"test"}}'`
  fires the path manually so you can isolate whether the bridge or the
  hook wiring is broken.

## Notes

- The forwarder is **observe-only**. It never blocks tool calls or
  rewrites prompts. If you want guardrails (deny shell commands, etc.),
  write a separate hook that returns a `permission` JSON response —
  don't try to extend this forwarder.
- Cursor caps hook timeout at the value in your config (default 30s);
  the forwarder times out at 500ms (`BRIDGE_HOOK_TIMEOUT_MS`) so a
  down bridge never holds up the agent.
- `loop_limit` on `stop`/`subagentStop` defaults to 5 in Cursor. Leave
  it; the forwarder doesn't return a `followup_message` so the loop
  exits normally regardless.
