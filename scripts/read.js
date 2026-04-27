import {
  context,
  endTurn,
  pick,
  postEvents,
  randomInt,
  readActivityPair,
  sleep,
  startSessionAndTurn,
} from './robot-state-utils.js'

const READ_FILES = [
  'README.md',
  'plugin/src/http.ts',
  'robot_v2/Face.cpp',
  'robot_v2/AgentEvents.cpp',
  'helpers/cursor-hook-forward.mjs',
]

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Read-only investigation')
  await sleep(2000)

  const count = randomInt(5, 50)
  for (let i = 0; i < count; i += 1) {
    const file = pick(READ_FILES)
    await postEvents(readActivityPair(file), 'file.read')
    await sleep(1000)
  }

  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
