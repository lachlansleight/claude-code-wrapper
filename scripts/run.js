import {
    context,
    endTurn,
    pick,
    postEvents,
    randomInt,
    random,
    sleep,
    startSessionAndTurn,
    writeActivityPair,
    readActivityPair,
    shellFinishedItem,
    shellStartedItem,
    buildShellActivity,
  } from './robot-state-utils.js'
  
  const WRITE_FILES = [
    'robot_v2/Face.cpp',
    'robot_v2/AgentEvents.cpp',
    'robot_v2/AmbientMotion.cpp',
    'plugin/src/http.ts',
  ]

  const READ_FILES = [
    'README.md',
    'plugin/src/http.ts',
    'robot_v2/Face.cpp',
    'robot_v2/AgentEvents.cpp',
    'helpers/cursor-hook-forward.mjs',
  ]

  const COMMANDS = [
    'cd plugin && npm test 2>&1',
    'grep -n activity src/http.ts',
    'node --check scripts/read.js',
  ]

async function simulateReading() {
    let count = randomInt(3, 5)
    for (let i = 0; i < count; i += 1) {
        const file = pick(READ_FILES)
        const pair = readActivityPair(file);
        await postEvents([pair[0]], "file.read");
        await sleep(random(500, 1000));
        await postEvents([pair[1]], "file.read");
    }
    await sleep(3000)

    count = randomInt(2, 5)
    for (let i = 0; i < count; i += 1) {
        const file = pick(READ_FILES)
        const pair = readActivityPair(file);
        await postEvents([pair[0]], "file.read");
        await sleep(random(500, 1000));
        await postEvents([pair[1]], "file.read");
    }
    await sleep(3000)
}

async function simulateWriting() {
    let count = randomInt(1, 2)
    for (let i = 0; i < count; i += 1) {
        const file = pick(WRITE_FILES)
        const pair = writeActivityPair(file);
        await postEvents([pair[0]], "file.write");
        await sleep(random(1000, 2000));
        await postEvents([pair[1]], "file.write");
    }
    await sleep(3000)

    count = randomInt(1, 3)
    for (let i = 0; i < count; i += 1) {
        const file = pick(WRITE_FILES)
        const pair = writeActivityPair(file);
        await postEvents([pair[0]], "file.write");
        await sleep(random(1000, 2000));
        await postEvents([pair[1]], "file.write");
    }
    await sleep(3000)
}

async function simulateExecuting() {
    const activity = buildShellActivity(pick(COMMANDS))
    await postEvents([shellStartedItem(activity)], 'shell.exec start')
    await sleep(1500)
    await postEvents([shellFinishedItem(activity, 1500)], 'shell.exec end')
    await sleep(2000)

    await postEvents([shellStartedItem(activity)], 'shell.exec start')
    await sleep(800)
    await postEvents([shellFinishedItem(activity, 800)], 'shell.exec end')
    await sleep(2000)

    await postEvents([shellStartedItem(activity)], 'shell.exec start')
    await sleep(7000)
    await postEvents([shellFinishedItem(activity, 7000)], 'shell.exec end')
    await sleep(2000)
}
  
  async function main() {
    console.log(`session=${context.sessionId} turn=${context.turnId}`)
    await startSessionAndTurn('Write-heavy implementation')
    await sleep(2000)

    await simulateReading();
    await simulateWriting();
    await simulateExecuting();

    // const toolCount = 2 + Math.floor(Math.random() * 2);
    // for(let i = 0; i < toolCount; i++) {
    //     await sleep((Math.random() * 4000) + 2000);
    //     const roll = Math.random();
    //     if(roll < 0.4) {
    //         await simulateReading();
    //     } else if(roll < 0.8) {
    //         await simulateWriting();
    //     } else {
    //         await simulateExecuting();
    //     }
    // }
  
    
  
    await endTurn()
  }
  
  main().catch((err) => {
    console.error(err)
    process.exit(1)
  })
  