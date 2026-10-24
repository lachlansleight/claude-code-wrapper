import fs from 'node:fs'

// Mid-turn assistant text. Claude Code's hooks don't carry the text the
// assistant writes between tool calls, but every payload has
// transcript_path. On PostToolUse and Stop, read whatever assistant text was
// appended to the transcript since the last read and attach it as
// `assistant_text`, which the Claude adapter turns into `thinking` (and the
// final `message.assistant`). The first hook seen for a session, or a
// SessionStart / UserPromptSubmit, only records where the transcript ends.
//
// This is what plugin/helpers/claude-hook-forward.mjs did for the legacy
// plugin, moved into the bridge so the sultana plugin's HTTP hooks need no
// script. It only works when the bridge runs on the same machine as Claude
// Code; payloads the forwarder already enriched are left alone.

const READ_HOOKS = new Set(['PostToolUse', 'Stop'])
const SEED_HOOKS = new Set(['SessionStart', 'UserPromptSubmit'])
const MAX_READ = 4 * 1024 * 1024

/** session_id -> byte offset of the first transcript line not yet read. */
const offsets = new Map<string, number>()

function fileSize(path: string): number | undefined {
  try {
    return fs.statSync(path).size
  } catch {
    return undefined
  }
}

function assistantTexts(chunk: string): string[] {
  const texts: string[] = []
  for (const line of chunk.split('\n')) {
    if (!line.trim()) continue
    try {
      const msg = JSON.parse(line) as { message?: { role?: unknown; content?: unknown } }
      if (msg?.message?.role !== 'assistant' || !Array.isArray(msg.message.content)) continue
      for (const block of msg.message.content as Array<{ type?: unknown; text?: unknown }>) {
        if (block?.type === 'text' && typeof block.text === 'string' && block.text.length > 0) texts.push(block.text)
      }
    } catch {
      // Not JSON (shouldn't happen for complete lines): skip it.
    }
  }
  return texts
}

export function enrichWithTranscriptText(hookType: string, payload: unknown): unknown {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) return payload
  const p = payload as Record<string, unknown>
  const sessionId = p.session_id
  const transcriptPath = p.transcript_path
  if (typeof sessionId !== 'string' || typeof transcriptPath !== 'string') return payload

  if (hookType === 'SessionEnd') {
    offsets.delete(sessionId)
    return payload
  }
  if (p.assistant_text !== undefined) return payload // the legacy forwarder already did this
  if (!READ_HOOKS.has(hookType) && !SEED_HOOKS.has(hookType)) return payload

  const size = fileSize(transcriptPath)
  if (size === undefined) return payload // not on this machine, or not readable
  const previous = offsets.get(sessionId)
  if (previous === undefined || previous > size) {
    offsets.set(sessionId, size)
    return payload
  }
  if (!READ_HOOKS.has(hookType) || size === previous) return payload

  let chunk: Buffer
  try {
    const length = Math.min(size - previous, MAX_READ)
    chunk = Buffer.alloc(length)
    const fd = fs.openSync(transcriptPath, 'r')
    try {
      fs.readSync(fd, chunk, 0, length, previous)
    } finally {
      fs.closeSync(fd)
    }
  } catch {
    return payload
  }
  // Only consume complete lines: Claude may still be writing the last one.
  const lastNewline = chunk.lastIndexOf(0x0a)
  if (lastNewline < 0) return payload
  offsets.set(sessionId, previous + lastNewline + 1)
  const texts = assistantTexts(chunk.subarray(0, lastNewline + 1).toString('utf8'))
  return texts.length > 0 ? { ...p, assistant_text: texts } : payload
}
