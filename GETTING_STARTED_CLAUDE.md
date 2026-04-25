# Getting started — Claude Code

Wire Claude Code's lifecycle hooks into a running `agent-bridge` so the
robot (or any WS subscriber) can react to Claude's activity.

## Prerequisites

- Node.js 20+
- Claude Code 2.0+ with hooks enabled (any recent version)

## 1. Build the bridge + plugin

```bash
git clone <this-repo> claude-code-wrapper
cd claude-code-wrapper/plugin
npm install
npm run build
```

## 2. Start the bridge

In its own terminal (or as a background service / startup item):

```bash
export BRIDGE_TOKEN="$(openssl rand -hex 32)"
node dist/index.js
```

Sanity check from another terminal:

```bash
curl http://127.0.0.1:8787/api/health
# {"ok":true,"version":"0.2.0",...,"agents":["claude","codex","cursor","opencode"]}
```

## 3. Install the plugin (auto-wires every hook)

In Claude Code:

```
/plugin marketplace add C:/absolute/path/to/claude-code-wrapper
/plugin install claude-code-bridge@claude-code-bridge-local
/reload-plugins
```

Use a Windows-style absolute path with forward slashes
(`C:/Users/...`). Git-Bash paths (`/c/...`) are interpreted as git
remotes and fail.

## 4. Tell the plugin about your token

`~/.claude/settings.json`:

```json
{
  "env": {
    "BRIDGE_TOKEN": "paste-the-same-token-here",
    "BRIDGE_URL": "http://127.0.0.1:8787"
  }
}
```

Restart Claude Code so the env block propagates to the hook subprocesses.

## 5. Verify

Open `examples/ws-client.html` in a browser, paste your token, click
**Connect**. Type anything in Claude. You should see:

- `hook_event` with `agent: "claude"` and `hook_type: "UserPromptSubmit"`
- `state_event` with `state: "thinking"` (or `waking` first if the
  bridge just started)

If hooks aren't arriving, check `~/.claude/debug/*.txt` — `hook-forward.js`
logs failures to stderr and Claude Code captures them there. The most
common cause is `BRIDGE_TOKEN` not being inherited; double-check the
`env` block above and restart.

## Refreshing after pulling new bridge code

The Claude Code plugin marketplace is pinned by commit hash. After
pulling or editing the plugin source (`cd plugin && npm run build`),
re-snapshot:

```
/plugin marketplace remove claude-code-bridge-local
/plugin marketplace add C:/absolute/path/to/claude-code-wrapper
/reload-plugins
```

That's it — the standalone bridge does **not** restart automatically;
re-run `node dist/index.js` if you changed bridge code (the hook
forwarder script is bundled into the plugin so the plugin install
covers it).
