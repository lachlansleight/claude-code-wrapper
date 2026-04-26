import { randomUUID } from 'node:crypto'
import type { Parser, ParsedEvent } from './types.js'
import type { ActivityRef, TodoItem, ParserInput } from '../agent-event.js'
import { classifyTool } from '../activity-classify.js'
import { summarize } from '../activity-summary.js'

// OpenCode events. Reference: https://opencode.ai/docs/plugins

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
    kind: classifyTool('opencode', toolName),
    tool: toolName,
    summary: summarize(toolName, toolInput),
  }
}

// Track which sessions we've already emitted a turn.started for, so a
// stream of message.updated payloads doesn't fire repeatedly.
const seenUserTurn = new Set<string>()

export const opencodeParser: Parser = {
  name: 'opencode',
  parse(input: ParserInput): ParsedEvent[] {
    const hook = input.hook_type
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id =
      asString(p.sessionID) || asString(p.sessionId) || asString(p.session_id) || undefined
    const tool =
      asString(p.tool) || asString(p.tool_name) || asString((p as { name?: unknown }).name) || ''
    const tool_input = p.input ?? p.tool_input
    const tool_use_id = asString(p.callID) || asString(p.tool_use_id) || asString(p.id) || undefined

    switch (hook) {
      case 'session.created':
        return [{ event: { kind: 'session.started', cause: 'startup' }, session_id }]
      case 'session.deleted':
        return [{ event: { kind: 'session.ended', cause: 'unknown' }, session_id }]
      case 'session.idle':
        if (session_id) seenUserTurn.delete(session_id)
        return [{ event: { kind: 'turn.ended', cause: 'completed' }, session_id }]
      case 'session.compacted':
        return [{ event: { kind: 'context.compacting' }, session_id }]

      case 'tool.execute.before': {
        const activity = buildActivity(tool, tool_input, tool_use_id)
        return [{ event: { kind: 'activity.started', activity }, session_id }]
      }
      case 'tool.execute.after': {
        const activity = buildActivity(tool, tool_input, tool_use_id)
        const errorText = asString(p.error) || asString(p.error_message)
        if (errorText) {
          return [
            {
              event: {
                kind: 'activity.failed',
                activity,
                error_message: errorText,
                failure_kind: 'error',
              },
              session_id,
            },
          ]
        }
        const out: ParsedEvent[] = [{ event: { kind: 'activity.finished', activity }, session_id }]
        if (tool === 'todo') {
          const todos = asTodos((tool_input as Record<string, unknown> | null)?.todos)
          if (todos) out.push({ event: { kind: 'todo.updated', items: todos }, session_id })
        }
        return out
      }

      case 'todo.updated': {
        const todos = asTodos(p.todos) ?? asTodos(p.items)
        if (!todos) return [{ event: { kind: 'unknown' }, session_id }]
        return [{ event: { kind: 'todo.updated', items: todos }, session_id }]
      }

      case 'permission.asked': {
        const request_id = asString(p.permissionID) || asString(p.id)
        if (!request_id) return [{ event: { kind: 'unknown' }, session_id }]
        const activity = tool ? buildActivity(tool, tool_input, tool_use_id) : undefined
        return [
          {
            event: {
              kind: 'permission.requested',
              request_id,
              activity,
              description: asString(p.title) || undefined,
            },
            session_id,
          },
        ]
      }
      case 'permission.replied': {
        const request_id = asString(p.permissionID) || asString(p.id)
        if (!request_id) return [{ event: { kind: 'unknown' }, session_id }]
        const response = asString(p.response)
        const decision: 'allow' | 'deny' =
          response === 'once' || response === 'always' ? 'allow' : 'deny'
        const scope: 'once' | 'session' | 'always' | 'unknown' | undefined =
          response === 'once' || response === 'always'
            ? response
            : response === 'reject'
              ? undefined
              : 'unknown'
        return [
          {
            event: { kind: 'permission.resolved', request_id, decision, scope },
            session_id,
          },
        ]
      }

      case 'message.updated':
      case 'message.part.updated': {
        const role =
          asString((p as { role?: unknown }).role) ||
          asString(((p as { message?: { role?: unknown } }).message ?? {}).role)
        const partType = asString((p as { type?: unknown }).type) || asString((p as { partType?: unknown }).partType)
        const text =
          asString((p as { text?: unknown }).text) ||
          asString((p as { content?: unknown }).content)

        if (partType === 'thinking') {
          return [{ event: { kind: 'thinking', text }, session_id }]
        }
        if (role === 'user') {
          if (session_id && seenUserTurn.has(session_id)) {
            // Edits to an already-started turn — emit just the message.
            return [{ event: { kind: 'message.user', text }, session_id }]
          }
          if (session_id) seenUserTurn.add(session_id)
          return [
            { event: { kind: 'turn.started', prompt: text || undefined }, session_id },
            { event: { kind: 'message.user', text }, session_id },
          ]
        }
        if (role === 'assistant') {
          return [{ event: { kind: 'message.assistant', text, final: true }, session_id }]
        }
        return []
      }

      default:
        return [{ event: { kind: 'unknown' }, session_id }]
    }
  },
}
