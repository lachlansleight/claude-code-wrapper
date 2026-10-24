# Fixtures

Each `NAME.jsonl` is a real recording of the WebSocket frames a bridge sent
while `scenarios.mjs NAME` posted a scripted Claude Code hook sequence to it.
`NAME.trace.txt` is the golden `sultana-replay --trace --seed 1` output.

| Fixture | Covers |
|---|---|
| `claude-turn` | One session through Waking, Thinking, Reading (a burst bridged by the 1 s linger), Writing, Executing then Straining, AttractingAttention, Celebrating, Sleeping |
| `two-sessions` | The single-session latch: B's turn takes the face from A, A's later events and turn end are dropped |
| `error-turn` | A failing tool (`PostToolUseFailure` → `activity.failed`) and a turn that dies on an API error (`StopFailure` → `turn.ended` with cause `error`): no Celebrating, a dip in mood |
| `permission` | Claude permission prompts (`PermissionRequest` → `permission.requested`): pending set, Claude's 6 s notification fires AttractingAttention, approval clears it, an unanswered prompt clears when the turn ends |
| `disconnect` | The bridge going away mid-command: the face drops back to idle instead of staying in Straining |

## Recording

Use a throwaway bridge on a spare port, with Firebase off. Run it from a
temporary directory: the bridge writes per-turn logs into `logs/` under its
working directory.

```bash
# from the repo root, with the bridge built (cd plugin && npm run build)
REPO=$PWD
TOKEN=$(openssl rand -hex 16)
cd "$(mktemp -d)"
BRIDGE_TOKEN=$TOKEN BRIDGE_PORT=8797 BRIDGE_DATABASE_URL= BRIDGE_AGENT_ID= \
  node "$REPO/plugin/dist/index.js" &
BRIDGE_TOKEN=$TOKEN node "$REPO/scripts/record-ws.mjs" \
  "$REPO/robot_v3/host/fixtures/claude-turn.jsonl" --url ws://127.0.0.1:8797/ws &
BRIDGE_TOKEN=$TOKEN BRIDGE_URL=http://127.0.0.1:8797 \
  node "$REPO/robot_v3/host/fixtures/scenarios.mjs" claude-turn
kill -INT %2; kill %1
# for `disconnect`, kill the bridge (%1) about 10.5 s into the scenario instead
cd "$REPO/robot_v3/host" && make golden
```

Recording from a real Claude Code session works the same way: point
`record-ws.mjs` at your normal bridge and use Claude Code as usual.

A recording made against a changed bridge (a new adapter mapping, say) is a
new fixture, not a replacement: keep the old one so the firmware is still
tested against what older bridges send.
