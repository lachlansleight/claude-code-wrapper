import {
  context,
  endTurn,
  pick,
  postEvents,
  randomInt,
  sleep,
  startSessionAndTurn,
  writeActivityPair,
} from './robot-state-utils.js'

const WRITE_FILES = [
  'robot_v2/Face.cpp',
  'robot_v2/AgentEvents.cpp',
  'robot_v2/AmbientMotion.cpp',
  'plugin/src/http.ts',
]

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Write-heavy implementation')
  await sleep(2000)

  const count = randomInt(5, 10)
  for (let i = 0; i < count; i += 1) {
    const file = pick(WRITE_FILES)
    await postEvents(writeActivityPair(file), 'file.write')
    await sleep(1000)
  }

  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
