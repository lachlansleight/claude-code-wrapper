// OpenCode → agent-bridge plugin.
//
// Drop this file at one of:
//   ~/.config/opencode/plugins/agent-bridge.js   (global, recommended)
//   <project>/.opencode/plugins/agent-bridge.js  (project-local)
//
// OpenCode auto-loads .js / .ts files from those paths at startup.
//
// Env vars (read at plugin init):
//   BRIDGE_URL    default http://127.0.0.1:8787
//   BRIDGE_TOKEN  required (plugin no-ops if unset)
//   BRIDGE_HOOK_TIMEOUT_MS  default 500
//
// All POSTs are fire-and-forget with a short timeout; the plugin never
// blocks tool execution.

const BRIDGE_URL = process.env.BRIDGE_URL || 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN
const TIMEOUT_MS = Number.parseInt(process.env.BRIDGE_HOOK_TIMEOUT_MS || '500', 10)

async function send(hook_type, payload) {
  if (!BRIDGE_TOKEN) return
  const ctrl = new AbortController()
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS)
  try {
    await fetch(`${BRIDGE_URL}/hooks/opencode`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: `Bearer ${BRIDGE_TOKEN}`,
      },
      body: JSON.stringify({ hook_type, payload }),
      signal: ctrl.signal,
    })
  } catch {
    // Swallow: a down bridge must never break the agent.
  } finally {
    clearTimeout(timer)
  }
}

export const AgentBridgePlugin = async () => {
  if (!BRIDGE_TOKEN) {
    console.warn('[agent-bridge] BRIDGE_TOKEN not set; plugin disabled')
    return {}
  }
  console.log(`[agent-bridge] forwarding to ${BRIDGE_URL}`)

  const seenUserMessages = new Set()

  return {
    'session.created':  async (input) => send('session.created',  input),
    'session.deleted':  async (input) => send('session.deleted',  input),
    'session.idle':     async (input) => send('session.idle',     input),

    'tool.execute.before': async (input) => send('tool.execute.before', input),
    'tool.execute.after':  async (input) => send('tool.execute.after',  input),

    'permission.asked':   async (input) => send('permission.asked',   input),
    'permission.replied': async (input) => send('permission.replied', input),

    // Fire user_prompt-style hook on first sighting of a user-role
    // message. message.updated fires often; dedupe by message id.
    'message.updated': async (input) => {
      const msg = input?.info ?? input?.message ?? input
      const role = msg?.role
      const id = msg?.id
      if (role !== 'user' || !id) return
      if (seenUserMessages.has(id)) return
      seenUserMessages.add(id)
      await send('message.updated', input)
    },
  }
}
