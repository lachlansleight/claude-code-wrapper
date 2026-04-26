import { randomUUID } from 'node:crypto'
import type { Parser, ParsedEvent } from './types.js'
import type { ActivityRef, ParserInput } from '../agent-event.js'
import { classifyTool } from '../activity-classify.js'
import { summarize } from '../activity-summary.js'

// Cursor 1.7+ hooks. Reference: https://cursor.com/docs/hooks
//
// Canonical activity hooks: preToolUse / postToolUse / postToolUseFailure.
// Narrower hooks (beforeShellExecution / afterShellExecution / beforeMCPExecution
// / afterMCPExecution / beforeReadFile / afterFileEdit / beforeTabFileRead /
// afterTabFileEdit) fire alongside; we use them only as a summary-enrichment
// side-channel keyed by tool_use_id, never as activity sources.

function asString(v: unknown): string {
  return typeof v === 'string' ? v : ''
}

interface SummaryHint {
  command?: string
  file_path?: string
  url?: string
  ts: number
}

const summaryHints = new Map<string, SummaryHint>()
const SUMMARY_HINT_TTL_MS = 5 * 60 * 1000

function gcHints(): void {
  const cutoff = Date.now() - SUMMARY_HINT_TTL_MS
  for (const [id, h] of summaryHints) {
    if (h.ts < cutoff) summaryHints.delete(id)
  }
}

function recordHint(id: string, patch: Partial<SummaryHint>): void {
  if (!id) return
  const cur = summaryHints.get(id) ?? { ts: Date.now() }
  summaryHints.set(id, { ...cur, ...patch, ts: Date.now() })
}

function takeHint(id: string): SummaryHint | undefined {
  if (!id) return undefined
  const h = summaryHints.get(id)
  if (h) summaryHints.delete(id)
  gcHints()
  return h
}

function buildActivity(toolName: string, toolInput: unknown, id: string | undefined, hint?: SummaryHint): ActivityRef {
  let summary = summarize(toolName, toolInput)
  if (!summary && hint) {
    summary = hint.command || hint.file_path || hint.url || ''
    if (summary.length > 80) summary = summary.slice(0, 79) + '…'
  }
  return {
    id: id || `act_${randomUUID()}`,
    kind: classifyTool('cursor', toolName),
    tool: toolName,
    summary,
  }
}

function pickToolUseId(p: Record<string, unknown>): string | undefined {
  return (
    asString(p.tool_use_id) ||
    asString(p.toolUseId) ||
    asString(p.tool_call_id) ||
    asString(p.toolCallId) ||
    undefined
  )
}

export const cursorParser: Parser = {
  name: 'cursor',
  parse(input: ParserInput): ParsedEvent[] {
    const hook = input.hook_type
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id =
      asString(p.conversation_id) || asString(p.session_id) || undefined
    const turn_id = asString(p.generation_id) || undefined
    const tool_name = asString(p.tool_name)
    const tool_input = p.tool_input
    const tool_use_id = pickToolUseId(p)

    switch (hook) {
      case 'sessionStart':
        return [{ event: { kind: 'session.started', cause: 'startup' }, session_id }]
      case 'sessionEnd':
        return [{ event: { kind: 'session.ended', cause: 'unknown' }, session_id }]

      case 'beforeSubmitPrompt': {
        const prompt = asString(p.prompt)
        return [
          { event: { kind: 'turn.started', prompt: prompt || undefined }, session_id, turn_id },
          { event: { kind: 'message.user', text: prompt }, session_id, turn_id },
        ]
      }

      case 'stop':
        return [{ event: { kind: 'turn.ended', cause: 'completed' }, session_id, turn_id }]

      case 'afterAgentResponse': {
        const text = asString(p.text) || asString(p.response) || asString(p.content)
        return [{ event: { kind: 'message.assistant', text, final: true }, session_id, turn_id }]
      }

      case 'afterAgentThought': {
        const text = asString(p.text) || asString(p.thought) || asString(p.content)
        return [{ event: { kind: 'thinking', text }, session_id, turn_id }]
      }

      case 'preToolUse': {
        if (tool_name === 'fetch_rules') return []
        if (tool_name === 'ask_question') {
          const ti = (tool_input ?? {}) as Record<string, unknown>
          const text = asString(ti.question) || asString(ti.prompt) || asString(ti.text)
          const opts = ti.options ?? ti.suggestions
          // Best guess: Cursor may expose suggested answers as an array of
          // strings or as objects with a `text`/`label` field.
          let options: string[] | undefined
          if (Array.isArray(opts)) {
            const arr = opts
              .map((o) => {
                if (typeof o === 'string') return o
                if (o && typeof o === 'object') {
                  const r = o as Record<string, unknown>
                  return asString(r.text) || asString(r.label) || asString(r.value)
                }
                return ''
              })
              .filter((s) => s.length > 0)
            if (arr.length > 0) options = arr
          }
          return [{ event: { kind: 'agent.question', text, options }, session_id, turn_id }]
        }
        const hint = takeHint(tool_use_id ?? '')
        const activity = buildActivity(tool_name, tool_input, tool_use_id, hint)
        return [{ event: { kind: 'activity.started', activity }, session_id, turn_id }]
      }

      case 'postToolUse': {
        if (tool_name === 'fetch_rules' || tool_name === 'ask_question') return []
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        return [{ event: { kind: 'activity.finished', activity }, session_id, turn_id }]
      }

      case 'postToolUseFailure': {
        if (tool_name === 'fetch_rules' || tool_name === 'ask_question') return []
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        const errorText = asString(p.error) || asString(p.error_message) || asString(p.message)
        return [
          {
            event: {
              kind: 'activity.failed',
              activity,
              error_message: errorText || undefined,
              failure_kind: 'error',
            },
            session_id,
            turn_id,
          },
        ]
      }

      // Narrow hooks — used only for summary enrichment, no event emission.
      case 'beforeShellExecution':
      case 'afterShellExecution':
        recordHint(tool_use_id ?? '', { command: asString(p.command) })
        return []

      case 'beforeMCPExecution':
      case 'afterMCPExecution':
        recordHint(tool_use_id ?? '', { url: asString(p.server_url) || asString(p.url) })
        return []

      case 'beforeReadFile':
      case 'beforeTabFileRead':
        recordHint(tool_use_id ?? '', { file_path: asString(p.file_path) || asString(p.path) })
        return []

      case 'afterFileEdit':
      case 'afterTabFileEdit':
        recordHint(tool_use_id ?? '', { file_path: asString(p.file_path) || asString(p.path) })
        return []

      case 'subagentStart': {
        const subagent_id = asString(p.subagent_id) || asString(p.id) || `sub_${randomUUID()}`
        return [
          {
            event: {
              kind: 'subagent.started',
              subagent_id,
              subagent_type: asString(p.subagent_type) || undefined,
              task: asString(p.task) || asString(p.prompt) || undefined,
            },
            session_id,
          },
        ]
      }
      case 'subagentStop': {
        const subagent_id = asString(p.subagent_id) || asString(p.id) || ''
        return [
          { event: { kind: 'subagent.finished', subagent_id, cause: 'completed' }, session_id },
        ]
      }

      case 'preCompact':
        return [{ event: { kind: 'context.compacting' }, session_id }]

      default:
        return [{ event: { kind: 'unknown' }, session_id }]
    }
  },
}
