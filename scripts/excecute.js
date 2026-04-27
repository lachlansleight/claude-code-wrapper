import {
  buildShellActivity,
  context,
  endTurn,
  pick,
  postEvents,
  randomInt,
  shellFinishedItem,
  shellStartedItem,
  sleep,
  startSessionAndTurn,
} from './robot-state-utils.js'

const COMMANDS = [
  'cd plugin && npm test 2>&1',
  'grep -n activity src/http.ts',
  'node --check scripts/read.js',
]

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Execute several timed actions')
  await sleep(2000)

  const count = randomInt(3, 5)
  for (let i = 0; i < count; i += 1) {
    const durationSec = randomInt(1, 10)
    const activity = buildShellActivity(pick(COMMANDS))
    await postEvents([shellStartedItem(activity)], 'shell.exec start')
    await sleep(durationSec * 1000)
    await postEvents([shellFinishedItem(activity, durationSec * 1000)], 'shell.exec end')
    await sleep(1000)
  }

  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
