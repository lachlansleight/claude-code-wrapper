// Fires a fake Claude Code hook sequence at the bridge so you can test
// firmware reactions without running a real Claude session.
//
// Run:
//   BRIDGE_TOKEN=... npx tsx src/simulate-hooks.ts
//   (or compile + node)
//
// Optional env:
//   BRIDGE_URL  default http://127.0.0.1:8787
//   SESSION_ID  default sim_<random>
//
// Sequence:
//   UserPromptSubmit
//   5x (PreToolUse Read → 250ms → PostToolUse Read)
//   2s pause
//   2x (PreToolUse Write → 400ms → PostToolUse Write)
//   1.5s pause
//   Notification ("done thinking")
//   Stop

const BRIDGE_URL = process.env.BRIDGE_URL ?? 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN ?? "e0112a5b1f05"
const SESSION_ID = process.env.SESSION_ID ?? `sim_${Math.random().toString(36).slice(2, 10)}`

if (!BRIDGE_TOKEN) {
  console.error('BRIDGE_TOKEN env var is required')
  process.exit(1)
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

async function postHook(hook_type, payload = {}) {
  const body = {
    hook_type,
    payload: { session_id: SESSION_ID, ...payload },
  }
  const res = await fetch(`${BRIDGE_URL}/api/hook-event`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${BRIDGE_TOKEN}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify(body),
  })
  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`hook ${hook_type} → ${res.status} ${text}`)
  }
  console.log(`→ ${hook_type}${payload.tool_name ? ` (${payload.tool_name})` : ''}`)
}

const READ_FILES = [
  'src/index.ts',
  'package.json',
  'README.md',
  'plugin/src/mcp.ts',
  'robot_v2/Personality.cpp',
]

const WRITE_FILES = ['robot_v2/Face.cpp', 'robot_v2/Personality.cpp']

async function preTool(tool_name, tool_input) {
  await postHook('PreToolUse', { tool_name, tool_input })
}

async function postTool(tool_name, tool_input) {
  await postHook('PostToolUse', { tool_name, tool_input })
}

async function main() {
  console.log(`bridge=${BRIDGE_URL}  session=${SESSION_ID}`)

  await postHook('UserPromptSubmit', {
    prompt: 'Pretend prompt for the simulator.',
  })
  await sleep(8000)

  for (const file of READ_FILES) {
    await preTool('Read', { file_path: file })
    await sleep(250)
    await postTool('Read', { file_path: file })
    await sleep(400)
  }

  console.log('-- thinking pause --')
  await sleep(10000)

  for (const file of WRITE_FILES) {
    await preTool('Write', { file_path: file })
    await sleep(400)
    await postTool('Write', { file_path: file })
    await sleep(500)
  }

  console.log('-- pre-stop pause --')
  await sleep(5000)

  await postHook('Notification', {
    message: 'All done — wrote two files based on five reads.',
  })
  await sleep(400)

  await postHook('Stop', {
    assistant_text: ['All done — wrote two files based on five reads.'],
  })

  console.log('done')
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
