#!/usr/bin/env node
// Deterministic Claude Code hook sequences for recording fixtures. Posts to
// a bridge's /hooks/claude exactly like plugin/helpers/claude-hook-forward.mjs
// does, so the recorded WebSocket frames are what the bridge really emits.
//
// Usage:
//   BRIDGE_TOKEN=... BRIDGE_URL=http://127.0.0.1:8787 node scenarios.mjs <claude-turn|two-sessions>
//
// See README.md in this folder for the full recording recipe.

const BRIDGE_URL = process.env.BRIDGE_URL ?? 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

async function hook(session_id, hook_type, payload = {}) {
  const res = await fetch(`${BRIDGE_URL}/hooks/claude`, {
    method: 'POST',
    headers: { Authorization: `Bearer ${BRIDGE_TOKEN}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ hook_type, payload: { session_id, ...payload } }),
  })
  if (!res.ok) throw new Error(`${hook_type} -> ${res.status} ${await res.text().catch(() => '')}`)
  console.error(`${session_id}: ${hook_type}${payload.tool_name ? ` (${payload.tool_name})` : ''}`)
}

const tool = async (sid, tool_name, tool_input, holdMs) => {
  await hook(sid, 'PreToolUse', { tool_name, tool_input, tool_use_id: `tu_${Math.random().toString(36).slice(2, 8)}` })
  await sleep(holdMs)
  await hook(sid, 'PostToolUse', { tool_name, tool_input })
}

const scenarios = {
  // One session through every face state the router produces:
  // Waking, Thinking, Reading (with a linger-bridged burst), Writing,
  // Executing -> Straining, AttractingAttention, Celebrating, Sleeping.
  async 'claude-turn'() {
    const a = 'fixture-a'
    await hook(a, 'SessionStart', { source: 'startup' })
    await sleep(2000)
    await hook(a, 'UserPromptSubmit', { prompt: 'Fix the failing test in src/index.ts' })
    await sleep(1500)
    await tool(a, 'Read', { file_path: 'src/index.ts' }, 800)
    await sleep(300)
    await tool(a, 'Grep', { pattern: 'TODO', path: 'src' }, 400)
    await sleep(1500)
    await tool(a, 'Edit', { file_path: 'src/index.ts', old_string: 'a', new_string: 'b' }, 600)
    await sleep(1500)
    await tool(a, 'Bash', { command: 'npm test' }, 6500)
    await sleep(1500)
    await hook(a, 'Notification', { message: 'Claude needs your permission to use Bash' })
    await sleep(2000)
    await tool(a, 'Bash', { command: 'git status' }, 500)
    await sleep(1500)
    await hook(a, 'Stop', { assistant_text: ['Fixed the test; npm test passes now.'] })
    await sleep(5000)
    await hook(a, 'SessionEnd', { reason: 'exit' })
  },

  // Two sessions competing for the robot's single-session latch. B's
  // turn.started takes the latch from A; A's later events (including its
  // Stop) are dropped by the face but still matter to a host that bounces
  // on any session's turn end.
  async 'two-sessions'() {
    const a = 'fixture-a'
    const b = 'fixture-b'
    await hook(a, 'SessionStart', { source: 'startup' })
    await sleep(1000)
    await hook(a, 'UserPromptSubmit', { prompt: 'Session A prompt' })
    await sleep(500)
    await tool(a, 'Read', { file_path: 'a.ts' }, 700)
    await sleep(500)
    await hook(b, 'UserPromptSubmit', { prompt: 'Session B prompt' })
    await sleep(500)
    await hook(b, 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run build' } })
    await sleep(1000)
    await tool(a, 'Read', { file_path: 'b.ts' }, 300)
    await sleep(500)
    await hook(a, 'Stop', { assistant_text: ['Session A done.'] })
    await sleep(1500)
    await hook(b, 'PostToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run build' } })
    await sleep(1500)
    await hook(b, 'Stop', { assistant_text: ['Session B done.'] })
    await sleep(4000)
    await hook(a, 'SessionEnd', { reason: 'exit' })
    await sleep(1000)
    await hook(b, 'SessionEnd', { reason: 'exit' })
  },

  // A tool fails (PostToolUseFailure), then the turn dies on an API error
  // (StopFailure): no Celebrating, a small dip in mood instead.
  async 'error-turn'() {
    const a = 'fixture-a'
    await hook(a, 'UserPromptSubmit', { prompt: 'Run the tests' })
    await sleep(1500)
    await tool(a, 'Read', { file_path: 'src/index.test.ts' }, 600)
    await sleep(1000)
    await hook(a, 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm test' }, tool_use_id: 'toolu_err1' })
    await sleep(2000)
    await hook(a, 'PostToolUseFailure', {
      tool_name: 'Bash',
      tool_input: { command: 'npm test' },
      tool_use_id: 'toolu_err1',
      error: 'Exit code 1\nFAIL src/index.test.ts\n  2 failing',
      is_interrupt: false,
      duration_ms: 2000,
    })
    await sleep(1500)
    await hook(a, 'StopFailure', { error: 'rate_limit', last_assistant_message: 'Two tests are failing; looking at' })
  },

  // Claude's permission prompts: PermissionRequest sets the robot's pending
  // permission (its "blocked" mood driver); Claude's own notification 6 s in
  // fires AttractingAttention; approving (the tool's PostToolUse) clears it.
  // The second prompt is never answered: the turn ending clears it.
  async permission() {
    const a = 'fixture-a'
    await hook(a, 'UserPromptSubmit', { prompt: 'Clean out the build folder' })
    await sleep(1500)
    const rm = { command: 'rm -rf build', description: 'Remove build output' }
    await hook(a, 'PreToolUse', { tool_name: 'Bash', tool_input: rm, tool_use_id: 'toolu_perm1' })
    await sleep(100)
    await hook(a, 'PermissionRequest', { tool_name: 'Bash', tool_input: rm, permission_suggestions: [] })
    await sleep(6000)
    await hook(a, 'Notification', { message: 'Claude needs your permission to use Bash' })
    await sleep(2500)
    await hook(a, 'PostToolUse', { tool_name: 'Bash', tool_input: rm, tool_use_id: 'toolu_perm1' })
    await sleep(1500)
    const write = { file_path: 'NOTES.md', content: 'cleaned' }
    await hook(a, 'PreToolUse', { tool_name: 'Write', tool_input: write, tool_use_id: 'toolu_perm2' })
    await sleep(100)
    await hook(a, 'PermissionRequest', { tool_name: 'Write', tool_input: write, permission_suggestions: [] })
    await sleep(3000)
    await hook(a, 'Stop', { assistant_text: ['Removed build/. Skipped writing NOTES.md.'] })
  },

  // The bridge goes away while a long command runs. The recording script
  // kills the bridge about 7 s after this scenario's Bash call starts.
  async disconnect() {
    const a = 'fixture-a'
    await hook(a, 'SessionStart', { source: 'startup' })
    await sleep(2000)
    await hook(a, 'UserPromptSubmit', { prompt: 'Build it' })
    await sleep(1500)
    await hook(a, 'PreToolUse', { tool_name: 'Bash', tool_input: { command: 'npm run build' }, tool_use_id: 'toolu_dc1' })
    await sleep(30000)  // killed before this ends
  },
}

const name = process.argv[2]
if (!BRIDGE_TOKEN || !scenarios[name]) {
  console.error(`usage: BRIDGE_TOKEN=... BRIDGE_URL=... node scenarios.mjs <${Object.keys(scenarios).join('|')}>`)
  process.exit(2)
}
scenarios[name]().catch((err) => {
  console.error(err)
  process.exit(1)
})
