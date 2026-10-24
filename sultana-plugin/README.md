# Sultana plugin for Claude Code

Sends Claude Code's lifecycle hooks to the Sultana bridge on this machine, so
Sultana (the robot, and the Sultana Dock app) can show what Claude is doing.

It contains no code: every hook is a Claude Code HTTP hook that POSTs the
hook's JSON to `http://127.0.0.1:8787/hooks/claude`. `SessionStart` can't be
an HTTP hook, so it runs `curl` in the background instead. The bridge accepts
these from this machine without a token, so there's nothing to set in your
shell. If the bridge isn't running, the connection is refused instantly and
Claude carries on.

## Install

With Sultana Dock: Settings ▸ Claude Code ▸ **Install Plugin**. The app
installs the copy it carries, so the hooks always match its bridge.

Otherwise, in Claude Code:

```
/plugin marketplace add lachlansleight/claude-code-wrapper
/plugin install sultana@sultana
```

or from a terminal:

```bash
claude plugin marketplace add lachlansleight/claude-code-wrapper --sparse .claude-plugin sultana-plugin
claude plugin install sultana@sultana
```

Plugins load when a session starts (or on `/reload-plugins`).

Then run a bridge on port 8787: on a Mac, [Sultana Dock](../sultana-dock/README.md)
does this for you. Elsewhere, run the standalone bridge
(`node plugin/dist/index.js`, see the top-level README).

Don't also enable the older `claude-code-bridge` plugin from the same
marketplace: every hook would arrive twice.

## Hooks

`SessionStart`, `SessionEnd`, `UserPromptSubmit`, `PreToolUse`,
`PermissionRequest`, `PermissionDenied`, `PostToolUse`, `PostToolUseFailure`,
`Notification`, `SubagentStop`, `PreCompact`, `Stop`, `StopFailure`: the ones
the bridge's Claude adapter understands
([`docs/bridge/HOOK_MAPPING.md`](../docs/bridge/HOOK_MAPPING.md)). Each has a
2 second timeout.
