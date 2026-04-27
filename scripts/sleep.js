import { context, endSession } from './robot-state-utils.js'

async function main() {
  console.log(`session=${context.sessionId}`)
  await endSession()
}

main().catch((err) => {
  console.error(err)
  process.exit(1)
})
