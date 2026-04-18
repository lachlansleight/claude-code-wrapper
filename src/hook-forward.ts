#!/usr/bin/env node
// Tiny stdin-reader that POSTs a Claude Code hook event to the bridge.
//
// Usage (from a Claude Code hook command):
//   node hook-forward.js <hook_type>
//
// Reads the hook's JSON payload from stdin, wraps it as
//   { hook_type, payload }
// and POSTs it to $BRIDGE_URL/api/hook-event with $BRIDGE_TOKEN.
//
// Exits 0 on success and on ANY failure — this process is wired into
// Claude Code's hook pipeline, and a non-zero exit would interfere with
// the session. Failures are logged to stderr so they surface in Claude
// Code's debug output but never block the turn.

import { request } from 'node:http'
import { URL } from 'node:url'
import { openSync, fstatSync, readSync, closeSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const HOOK_TYPE = process.argv[2]
const BRIDGE_URL = process.env.BRIDGE_URL || 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN
const TIMEOUT_MS = Number.parseInt(process.env.BRIDGE_HOOK_TIMEOUT_MS || '500', 10)

if (!HOOK_TYPE) {
  process.stderr.write('[bridge:hook] missing hook_type argument\n')
  process.exit(0)
}
if (!BRIDGE_TOKEN) {
  process.stderr.write('[bridge:hook] BRIDGE_TOKEN not set; event dropped\n')
  process.exit(0)
}

async function readStdin(): Promise<string> {
  if (process.stdin.isTTY) return ''
  const chunks: Buffer[] = []
  return new Promise((resolve) => {
    process.stdin.on('data', (c) => chunks.push(Buffer.isBuffer(c) ? c : Buffer.from(c)))
    process.stdin.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    process.stdin.on('error', () => resolve(''))
  })
}

function post(url: URL, body: string, token: string): Promise<void> {
  return new Promise((resolve) => {
    const req = request(
      {
        protocol: url.protocol,
        hostname: url.hostname,
        port: url.port,
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
        res.on('end', () => resolve())
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

function extractNewThinkingBlocks(payload: unknown): string[] {
  if (HOOK_TYPE !== 'PostToolUse' && HOOK_TYPE !== 'Stop') return []
  const p = payload as { transcript_path?: string; session_id?: string } | null
  if (!p || typeof p.transcript_path !== 'string' || typeof p.session_id !== 'string') return []

  let state: Record<string, { offset: number }> = {}
  try { state = JSON.parse(readFileSync(STATE_FILE, 'utf8')) } catch {}
  const prevOffset = state[p.session_id]?.offset ?? 0

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
    process.stderr.write(`[bridge:hook:${HOOK_TYPE}] transcript read failed: ${(err as Error).message}\n`)
    return []
  }

  const thoughts: string[] = []
  for (const line of newText.split('\n')) {
    if (!line.trim()) continue
    try {
      const msg: unknown = JSON.parse(line)
      const blocks = (msg as { message?: { content?: unknown }; content?: unknown })?.message?.content ??
        (msg as { content?: unknown })?.content
      if (!Array.isArray(blocks)) continue
      for (const b of blocks) {
        if (b && typeof b === 'object' && (b as { type?: string }).type === 'thinking') {
          const t = (b as { thinking?: unknown }).thinking
          if (typeof t === 'string' && t.length > 0) thoughts.push(t)
        }
      }
    } catch {}
  }

  state[p.session_id] = { offset: newEnd }
  try { writeFileSync(STATE_FILE, JSON.stringify(state)) } catch {}

  return thoughts
}

async function main(): Promise<void> {
  const raw = await readStdin()
  let payload: unknown = null
  if (raw.trim().length > 0) {
    try {
      payload = JSON.parse(raw)
    } catch {
      payload = { raw }
    }
  }
  const thinking_blocks = extractNewThinkingBlocks(payload)
  const enrichedPayload = thinking_blocks.length > 0 && payload && typeof payload === 'object'
    ? { ...(payload as Record<string, unknown>), thinking_blocks }
    : payload
  const body = JSON.stringify({ hook_type: HOOK_TYPE, payload: enrichedPayload })
  const url = new URL('/api/hook-event', BRIDGE_URL)
  await post(url, body, BRIDGE_TOKEN!)
}

main().finally(() => process.exit(0))
