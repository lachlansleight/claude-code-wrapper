#!/usr/bin/env node
// Record a bridge WebSocket session as JSONL, one line per received frame:
//   {"t": <ms since connect>, "frame": {...}}
// (or {"t", "text"} for a frame that isn't JSON). If the socket closes, a
// final {"t", "connected": false} line is written. This is the input format
// of robot_v3/host's sultana-replay and Sultana Dock's replay mode.
//
// Usage:
//   BRIDGE_TOKEN=... node scripts/record-ws.mjs OUT.jsonl [--url ws://127.0.0.1:8787/ws] [--duration SECONDS]
//
// Stops on Ctrl-C, after --duration, or when the bridge closes the socket.
// Needs Node 22+ (global WebSocket).

import { createWriteStream } from 'node:fs'

const args = process.argv.slice(2)
let out = null
let url = process.env.BRIDGE_WS_URL ?? 'ws://127.0.0.1:8787/ws'
let durationS = 0
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--url') url = args[++i]
  else if (args[i] === '--duration') durationS = Number(args[++i])
  else if (!out) out = args[i]
  else {
    console.error(`unexpected argument ${args[i]}`)
    process.exit(2)
  }
}
const token = process.env.BRIDGE_TOKEN
if (!out || !token) {
  console.error('usage: BRIDGE_TOKEN=... node scripts/record-ws.mjs OUT.jsonl [--url ws://host:port/ws] [--duration SECONDS]')
  process.exit(2)
}
if (typeof WebSocket !== 'function') {
  console.error('This needs Node 22 or newer (global WebSocket).')
  process.exit(2)
}

const file = createWriteStream(out, { flags: 'w' })
const target = new URL(url)
target.searchParams.set('token', token)
const ws = new WebSocket(target)
let t0 = 0
let count = 0
const now = () => Math.round(performance.now() - t0)

function finish(code = 0) {
  file.end(() => {
    console.error(`recorded ${count} frames to ${out}`)
    process.exit(code)
  })
}

ws.addEventListener('open', () => {
  t0 = performance.now()
  console.error(`connected to ${url}; recording (Ctrl-C to stop)`)
  if (durationS > 0) setTimeout(() => ws.close(), durationS * 1000)
})
ws.addEventListener('message', (ev) => {
  const text = typeof ev.data === 'string' ? ev.data : String(ev.data)
  let line
  try {
    line = JSON.stringify({ t: now(), frame: JSON.parse(text) })
  } catch {
    line = JSON.stringify({ t: now(), text })
  }
  file.write(line + '\n')
  count++
})
ws.addEventListener('close', () => {
  if (t0) file.write(JSON.stringify({ t: now(), connected: false }) + '\n')
  finish(0)
})
ws.addEventListener('error', () => {
  if (!t0) {
    console.error(`could not connect to ${url}`)
    finish(1)
  }
})
process.on('SIGINT', () => ws.close())
process.on('SIGTERM', () => ws.close())
