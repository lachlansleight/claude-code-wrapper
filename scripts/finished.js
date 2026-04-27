import { context, endTurn, sleep, startSessionAndTurn } from './robot-state-utils.js'

async function main() {
  console.log(`session=${context.sessionId} turn=${context.turnId}`)
  await startSessionAndTurn('Turn completed quickly')
  await sleep(2000)
  await endTurn()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
