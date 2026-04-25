#!/usr/bin/env node
// Cursor → agent-bridge hook forwarder.
//
// Wire this in ~/.cursor/hooks.json or <project>/.cursor/hooks.json as
// the `command` for any hook event. Cursor sends a JSON payload on stdin
// (with `hook_event_name`); this script POSTs it to /hooks/cursor and
// prints `{}` to stdout so Cursor sees a valid (no-op) hook response.
//
// Env vars:
//   BRIDGE_URL    default http://127.0.0.1:8787
//   BRIDGE_TOKEN  required
//   BRIDGE_HOOK_TIMEOUT_MS  default 500
//
// Always exits 0 — never block the agent on bridge failures.

import { request as httpRequest } from 'node:http'
import { request as httpsRequest } from 'node:https'
import { URL } from 'node:url'

const BRIDGE_URL = process.env.BRIDGE_URL || 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN
const TIMEOUT_MS = Number.parseInt(process.env.BRIDGE_HOOK_TIMEOUT_MS || '500', 10)

function bail(msg) {
  process.stderr.write(`[bridge:cursor] ${msg}\n`)
  process.stdout.write('{}')
  process.exit(0)
}

if (!BRIDGE_TOKEN) bail('BRIDGE_TOKEN not set; event dropped')

async function readStdin() {
  if (process.stdin.isTTY) return ''
  const chunks = []
  return new Promise((resolve) => {
    process.stdin.on('data', (c) => chunks.push(Buffer.isBuffer(c) ? c : Buffer.from(c)))
    process.stdin.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    process.stdin.on('error', () => resolve(''))
  })
}

function post(url, body, token) {
  return new Promise((resolve) => {
    const mod = url.protocol === 'https:' ? httpsRequest : httpRequest
    const req = mod(
      {
        protocol: url.protocol,
        hostname: url.hostname,
        port: url.port || (url.protocol === 'https:' ? 443 : 80),
        method: 'POST',
        path: url.pathname,
        headers: {
          'Content-Type': 'application/json; charset=utf-8',
          'Content-Length': Buffer.byteLength(body),
          Authorization: `Bearer ${token}`,
        },
        timeout: TIMEOUT_MS,
      },
      (res) => { res.resume(); res.on('end', resolve) },
    )
    req.on('error', (err) => { process.stderr.write(`[bridge:cursor] post failed: ${err.message}\n`); resolve() })
    req.on('timeout', () => { process.stderr.write(`[bridge:cursor] post timeout ${TIMEOUT_MS}ms\n`); req.destroy(); resolve() })
    req.write(body)
    req.end()
  })
}

const raw = await readStdin()
let payload = null
try { payload = JSON.parse(raw) } catch { payload = { raw } }
const hookType =
  (payload && typeof payload.hook_event_name === 'string' && payload.hook_event_name) ||
  process.argv[2] ||
  'unknown'

const body = JSON.stringify({ hook_type: hookType, payload })
await post(new URL('/hooks/cursor', BRIDGE_URL), body, BRIDGE_TOKEN)
process.stdout.write('{}')
process.exit(0)
