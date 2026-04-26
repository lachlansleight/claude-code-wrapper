# Getting started — Codex CLI

Wire OpenAI Codex CLI's lifecycle hooks into a running `agent-bridge`.

## Prerequisites

- Node.js 20+
- Codex CLI with hooks support (configurable via `~/.codex/hooks.json` —
  hooks are stable as of recent Codex builds; older builds may need the
  `[features] codex_hooks = true` flag in `~/.codex/config.toml`)

## 1. Build + start the bridge

If you haven't already (see [GETTING_STARTED_CLAUDE.md](GETTING_STARTED_CLAUDE.md)
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

Codex reads from any of these (highest priority first):

- **Project**: `<repo>/.codex/hooks.json`
- **User**: `~/.codex/hooks.json`

User-level catches every Codex session. Save this as `~/.codex/hooks.json`
(replace `<absolute-path>`):

```json
{
  "hooks": {
    "SessionStart": [
      { "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ],
    "UserPromptSubmit": [
      { "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ],
    "PreToolUse": [
      { "matcher": ".*", "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ],
    "PostToolUse": [
      { "matcher": ".*", "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ],
    "PermissionRequest": [
      { "matcher": ".*", "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ],
    "Stop": [
      { "hooks": [{ "type": "command", "command": "node <absolute-path>/helpers/codex-hook-forward.mjs", "timeout": 5 }] }
    ]
  }
}
```

The forwarder reads Codex's stdin payload, pulls `hook_event_name`, and
POSTs `{ hook_type, payload }` to `/hooks/codex`. It exits 0 with no
stdout, which Codex treats as "no opinion, proceed normally."

If your Codex build needs the feature flag, also create
`~/.codex/config.toml`:

```toml
[features]
codex_hooks = true
```

## 3. Tell the forwarder about your token

The script reads `BRIDGE_TOKEN` from its environment. Two options:

**A. Export it in your shell profile** (`.zshrc` / `.bashrc` /
PowerShell `$PROFILE`):

```bash
export BRIDGE_TOKEN="paste-the-same-token-here"
export BRIDGE_URL="http://127.0.0.1:8787"   # only if not the default
```

Restart your terminal so Codex inherits the env.

**B. Wrap the command** in `hooks.json`:

```json
{ "type": "command", "command": "BRIDGE_TOKEN=xxx node <absolute-path>/helpers/codex-hook-forward.mjs" }
```

## 4. Verify

Open `examples/ws-client.html`, paste your token, click **Connect**.
Run `codex` and ask it to do anything. You should see `agent_event`
frames with `agent: "codex"` and a sequence of `event.kind` values:
`turn.started` → `message.user` → `activity.started` /
`activity.finished` for each tool call → `message.assistant` →
`turn.ended`.

If nothing arrives:

- Codex prints hook execution logs to its TUI debug panel — check there
  first.
- The forwarder's stderr surfaces as "hook output" in Codex; a missing
  `BRIDGE_TOKEN` shows as `[bridge:codex] BRIDGE_TOKEN not set`.
- Manual test:
  ```bash
  curl -X POST http://127.0.0.1:8787/hooks/codex \
    -H "Authorization: Bearer $BRIDGE_TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"hook_type":"PreToolUse","payload":{"session_id":"test","tool_name":"Bash"}}'
  ```

## Notes

- The forwarder is **observe-only**. It returns no `hookSpecificOutput`,
  so Codex will not block, redirect, or mutate any tool call. If you
  want guardrails (deny destructive commands, etc.), write a separate
  hook that returns a Codex-compatible decision JSON — don't try to
  extend this forwarder.
- Default Codex hook timeout is 600s. The forwarder caps itself at
  500ms via `BRIDGE_HOOK_TIMEOUT_MS`, so a down bridge never holds up
  the agent. The `"timeout": 5` in the JSON is a defensive cap on top
  of that.
- Codex's `tool_name` values (`Bash`, `apply_patch`, `Subagent`,
  `mcp__*`) are classified into `ActivityKind` by
  `plugin/src/activity-classify.ts` — add new ids there if Codex
  introduces a tool the bridge can't classify yet.
