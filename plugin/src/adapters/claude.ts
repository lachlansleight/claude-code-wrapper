import { randomUUID } from 'node:crypto'
import type { Parser, ParsedEvent } from './types.js'
import type { ActivityRef, TodoItem } from '../agent-event.js'
import type { ParserInput } from '../agent-event.js'
import { classifyTool } from '../activity-classify.js'
import { summarize } from '../activity-summary.js'

// Claude Code hook payloads. Reference:
//   https://docs.claude.com/en/docs/claude-code/hooks

function asString(v: unknown): string {
  return typeof v === 'string' ? v : ''
}

function asTodos(v: unknown): TodoItem[] | null {
  if (!Array.isArray(v)) return null
  const out: TodoItem[] = []
  for (const item of v) {
    if (!item || typeof item !== 'object') continue
    const text = asString((item as Record<string, unknown>).content) || asString((item as Record<string, unknown>).text)
    const rawStatus = asString((item as Record<string, unknown>).status)
    const status: TodoItem['status'] =
      rawStatus === 'in_progress' || rawStatus === 'completed' || rawStatus === 'cancelled' || rawStatus === 'pending'
        ? rawStatus
        : 'pending'
    out.push({ text, status })
  }
  return out
}

function buildActivity(toolName: string, toolInput: unknown, toolUseId: string | undefined): ActivityRef {
  return {
    id: toolUseId || `act_${randomUUID()}`,
    kind: classifyTool('claude', toolName),
    tool: toolName,
    summary: summarize(toolName, toolInput),
  }
}

export const claudeParser: Parser = {
  name: 'claude',
  parse(input: ParserInput): ParsedEvent[] {
    const hook = input.hook_type
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id = asString(p.session_id) || undefined
    const tool_name = asString(p.tool_name)
    const tool_input = p.tool_input
    const tool_use_id = asString(p.tool_use_id) || undefined

    switch (hook) {
      case 'SessionStart': {
        const source = asString(p.source)
        const cause: 'startup' | 'resume' | 'clear' | 'unknown' =
          source === 'startup' || source === 'resume' || source === 'clear' ? source : 'unknown'
        return [{ event: { kind: 'session.started', cause }, session_id }]
      }

      case 'SessionEnd':
        return [{ event: { kind: 'session.ended', cause: 'unknown' }, session_id }]

      case 'UserPromptSubmit': {
        const prompt = asString(p.prompt)
        return [
          { event: { kind: 'turn.started', prompt: prompt || undefined }, session_id },
          { event: { kind: 'message.user', text: prompt }, session_id },
        ]
      }

      case 'PreToolUse': {
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        return [{ event: { kind: 'activity.started', activity }, session_id }]
      }

      case 'PostToolUse': {
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        const out: ParsedEvent[] = [{ event: { kind: 'activity.finished', activity }, session_id }]

        if (tool_name === 'TodoWrite') {
          const todos = asTodos((tool_input as Record<string, unknown> | null)?.todos)
          if (todos) out.push({ event: { kind: 'todo.updated', items: todos }, session_id })
        }
        return out
      }

      case 'Stop': {
        // claude-hook-forward.mjs enriches the payload with assistant_text scraped
        // from the transcript. Last block is the final reply.
        const blocks = Array.isArray(p.assistant_text)
          ? (p.assistant_text as unknown[]).filter((t): t is string => typeof t === 'string')
          : []
        const last = blocks[blocks.length - 1]
        const out: ParsedEvent[] = []
        if (last) {
          out.push({ event: { kind: 'message.assistant', text: last, final: true }, session_id })
        }
        out.push({
          event: { kind: 'turn.ended', cause: 'completed', last_assistant_text: last || undefined },
          session_id,
        })
        return out
      }

      case 'SubagentStop': {
        return [{ event: { kind: 'subagent.finished', subagent_id: session_id ?? '', cause: 'completed' }, session_id }]
      }

      case 'PreCompact': {
        const trigger = asString(p.trigger)
        const t: 'auto' | 'manual' | undefined =
          trigger === 'auto' || trigger === 'manual' ? trigger : undefined
        return [{ event: { kind: 'context.compacting', trigger: t }, session_id }]
      }

      case 'Notification': {
        // Intentionally no permission.requested here. Claude Code fires a
        // Notification on permission prompts but has no hook for the user's
        // verdict, so a pending_permission can never be cleared from this
        // signal alone — leaving consumers stuck in BLOCKED. Instead, treat
        // a long-running tool (EXECUTING → EXECUTING_LONG → BLOCKED via
        // timeouts) as the blocked indicator, and PostToolUse arrival as
        // the implicit "approved" signal.
        const message = asString(p.message)
        return [{ event: { kind: 'notification', text: message || undefined }, session_id }]
      }

      default:
        return [{ event: { kind: 'unknown' }, session_id }]
    }
  },
}
