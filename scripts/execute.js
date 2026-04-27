import {
  context,
  endTurn,
  pick,
  randomInt,
  sleep,
  startSessionAndTurn,
  doTimedToolCall,
} from './robot-state-utils.js'

const TOOL_NAMES = ['run_terminal_cmd', 'web_search', 'grep_search', 'search_files']

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Execute several timed actions')
  await sleep(2000)

  const count = randomInt(3, 5)
  for (let i = 0; i < count; i += 1) {
    const toolName = pick(TOOL_NAMES);
    const durationSeconds = randomInt(1, 10);
    await doTimedToolCall(toolName, durationSeconds);
    await sleep(1000)
  }

  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
