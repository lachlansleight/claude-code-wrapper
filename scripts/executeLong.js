import {
  buildShellActivity,
  context,
  doTimedToolCall,
  endTurn,
  postEvents,
  shellFinishedItem,
  shellStartedItem,
  sleep,
  startSessionAndTurn,
} from './robot-state-utils.js'

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Long running shell command')
  await sleep(2000)

  await doTimedToolCall("npm run build", 40);

  await sleep(2000)
  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
