#!/usr/bin/env node
// Claude Code -> agent-bridge hook forwarder.
//
// Usage (from Claude Code hooks.json):
//   node ${CLAUDE_PLUGIN_ROOT}/helpers/claude-hook-forward.mjs <hook_type>
//
// Reads hook payload JSON from stdin, wraps as:
//   { hook_type, payload }
// and POSTs to:
//   $BRIDGE_URL/hooks/claude
//
// Always exits 0 so bridge issues never block the agent session.

import { request as httpRequest } from 'node:http'
import { request as httpsRequest } from 'node:https'
import { URL } from 'node:url'
import { openSync, fstatSync, readSync, closeSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const HOOK_TYPE = process.argv[2]
const BRIDGE_URL = process.env.BRIDGE_URL || 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN
const TIMEOUT_MS = Number.parseInt(process.env.BRIDGE_HOOK_TIMEOUT_MS || '500', 10)

function bail(msg) {
  process.stderr.write(`[bridge:hook] ${msg}\n`)
  process.exit(0)
}

if (!HOOK_TYPE) bail('missing hook_type argument')
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
      (res) => {
        res.resume()
        res.on('end', resolve)
      },
    )
    req.on('error', (err) => {
      process.stderr.write(`[bridge:hook:${HOOK_TYPE}] post failed: ${err.message}\n`)
      resolve()
    })
    req.on('timeout', () => {
      process.stderr.write(`[bridge:hook:${HOOK_TYPE}] post timed out after ${TIMEOUT_MS}ms\n`)
      req.destroy()
      resolve()
    })
    req.write(body)
    req.end()
  })
}

const STATE_FILE = join(tmpdir(), 'claude-code-bridge-hook-state.json')

function extractNewAssistantText(payload) {
  const isReadHook = HOOK_TYPE === 'PostToolUse' || HOOK_TYPE === 'Stop'
  const isSeedHook = HOOK_TYPE === 'SessionStart' || HOOK_TYPE === 'UserPromptSubmit'
  if (!isReadHook && !isSeedHook) return []
  if (!payload || typeof payload !== 'object') return []
  const p = payload
  if (typeof p.transcript_path !== 'string' || typeof p.session_id !== 'string') return []

  let state = {}
  try { state = JSON.parse(readFileSync(STATE_FILE, 'utf8')) } catch {}
  let prevOffset = state[p.session_id]?.offset
  if (typeof prevOffset !== 'number') {
    try {
      const fd = openSync(p.transcript_path, 'r')
      prevOffset = fstatSync(fd).size
      closeSync(fd)
    } catch { prevOffset = 0 }
    state[p.session_id] = { offset: prevOffset }
    try { writeFileSync(STATE_FILE, JSON.stringify(state)) } catch {}
    return []
  }
  if (!isReadHook) return []

  let newText = ''
  let newEnd = prevOffset
  try {
    const fd = openSync(p.transcript_path, 'r')
    const size = fstatSync(fd).size
    if (size <= prevOffset) { closeSync(fd); return [] }
    const toRead = size - prevOffset
    const buf = Buffer.alloc(toRead)
    readSync(fd, buf, 0, toRead, prevOffset)
    closeSync(fd)
    newText = buf.toString('utf8')
    newEnd = size
  } catch (err) {
    process.stderr.write(`[bridge:hook:${HOOK_TYPE}] transcript read failed: ${err.message}\n`)
    return []
  }

  const texts = []
  for (const line of newText.split('\n')) {
    if (!line.trim()) continue
    try {
      const msg = JSON.parse(line)
      if (msg?.message?.role !== 'assistant') continue
      const blocks = msg.message.content
      if (!Array.isArray(blocks)) continue
      for (const b of blocks) {
        if (b && typeof b === 'object' && b.type === 'text') {
          const t = b.text
          if (typeof t === 'string' && t.length > 0) texts.push(t)
        }
      }
    } catch {}
  }

  state[p.session_id] = { offset: newEnd }
  try { writeFileSync(STATE_FILE, JSON.stringify(state)) } catch {}
  return texts
}

const raw = await readStdin()
let payload = null
if (raw.trim().length > 0) {
  try {
    payload = JSON.parse(raw)
  } catch {
    payload = { raw }
  }
}
const assistant_text = extractNewAssistantText(payload)
const enrichedPayload = assistant_text.length > 0 && payload && typeof payload === 'object'
  ? { ...payload, assistant_text }
  : payload
const body = JSON.stringify({ hook_type: HOOK_TYPE, payload: enrichedPayload })
await post(new URL('/hooks/claude', BRIDGE_URL), body, BRIDGE_TOKEN)
process.exit(0)
