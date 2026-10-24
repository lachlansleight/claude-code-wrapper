# Getting started — Claude Code

Wire Claude Code's lifecycle hooks into the bridge so Sultana (the robot,
Sultana Dock, or any WebSocket subscriber) can react to Claude's activity.

The **sultana plugin** does this with Claude Code's HTTP hooks: no Node
script per hook and nothing to set in your shell. It needs the bridge on the
**same machine** as Claude Code, on port **8787**.

On a Mac, [Sultana Dock](../../sultana-dock/README.md) runs the bridge for
you: open the app and skip to step 2. Otherwise, run the standalone bridge.

## Prerequisites

- Sultana Dock, or Node.js 20+ for the standalone bridge
- Claude Code with HTTP hooks (2.1.116 is known to work)

## 1. Build and start the bridge (without Sultana Dock)

```bash
git clone https://github.com/lachlansleight/claude-code-wrapper
cd claude-code-wrapper/plugin
npm install
npm run build
BRIDGE_TOKEN="$(openssl rand -hex 32)" node dist/index.js
```

`BRIDGE_TOKEN` protects everything that comes over the network (the robot,
the face editor, other machines). Hooks from Claude Code on this machine
don't need it. Set `BRIDGE_HOST=0.0.0.0` if a robot on your WiFi needs to
reach the bridge.

Sanity check from another terminal:

```bash
curl http://127.0.0.1:8787/api/health
# {"ok":true,"version":"0.3.0",...,"agents":["claude","codex","cursor","opencode"]}
```

## 2. Install the plugin

With Sultana Dock, open Settings ▸ Claude Code and click **Install Plugin**
(Settings opens by itself the first time you launch the app). Then start a
new Claude Code session, or run `/reload-plugins` in one that's open.

Without it, in Claude Code:

```
/plugin marketplace add lachlansleight/claude-code-wrapper
/plugin install sultana@sultana
/reload-plugins
```

or from a terminal:

```bash
claude plugin marketplace add lachlansleight/claude-code-wrapper --sparse .claude-plugin sultana-plugin
claude plugin install sultana@sultana
```

Working from a local checkout instead, add the marketplace by path. On
Windows use a Windows-style absolute path with forward slashes
(`C:/Users/...`); Git-Bash paths (`/c/...`) are treated as git remotes and
fail.

Don't also enable the legacy `claude-code-bridge` plugin: every hook would
arrive twice. If you had it installed, uninstall it
(`/plugin uninstall claude-code-bridge@claude-code-bridge-local`) and remove
its marketplace.

## 3. Verify

With Sultana Dock, type anything in Claude: the face in the Dock starts
working. Otherwise, open `examples/ws-client.html` in a browser, paste your
token, click **Connect**. Type anything in Claude. You should see:

- `agent_event` with `agent: "claude"` and `event.kind: "turn.started"`
- followed by `event.kind: "message.user"` carrying the prompt text
- as Claude works, `activity.started` / `activity.finished` for each tool
  call, and `thinking` for text Claude writes between tool calls (the bridge
  reads it from the session transcript)
- on completion, `event.kind: "turn.ended"` (with optional
  `last_assistant_text`)

If nothing arrives: check the bridge is listening on 8787 on the same
machine (Sultana Dock's Settings show the bridge's status, with Show Log),
and that the plugin loaded (`/plugin`, or `claude plugin list`). If the
bridge isn't running, Claude carries on; each hook fails instantly.

## Updating

Marketplaces Anthropic doesn't run don't auto-update by default. Update with
`/plugin marketplace update sultana` (or turn on auto-update for it in
`/plugin` ▸ Marketplaces), then `/reload-plugins`.

## Legacy: the forwarder plugin

The older `claude-code-bridge` plugin forwards each hook through
`plugin/helpers/claude-hook-forward.mjs`, a Node script, and needs
`BRIDGE_TOKEN` (and optionally `BRIDGE_URL`) in Claude Code's environment,
e.g. in `~/.claude/settings.json`:

```json
{ "env": { "BRIDGE_TOKEN": "paste-the-same-token-here", "BRIDGE_URL": "http://127.0.0.1:8787" } }
```

Use it only if the bridge runs on a different machine from Claude Code: the
forwarder reads the transcript on Claude's side, which the bridge can't do
remotely. Install with `/plugin install claude-code-bridge@sultana`.
