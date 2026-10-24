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

// Claude's PermissionRequest hook has no tool_use_id, and no hook reports
// the user's verdict. Track one pending request per session and resolve it
// at the next sign of the outcome: the same tool's PostToolUse or
// PostToolUseFailure (it ran, so it was allowed), PermissionDenied, or the
// turn or session ending (it never ran: denied). Matching on the tool name
// keeps parallel tool calls in a batch from resolving each other.
const pendingPermissions = new Map<string, { request_id: string; tool: string }>()

function resolvePendingPermission(
  session_id: string | undefined,
  decision: 'allow' | 'deny',
  toolName?: string,
): ParsedEvent[] {
  if (!session_id) return []
  const pending = pendingPermissions.get(session_id)
  if (!pending || (toolName !== undefined && pending.tool !== toolName)) return []
  pendingPermissions.delete(session_id)
  return [{ event: { kind: 'permission.resolved', request_id: pending.request_id, decision, scope: 'unknown' }, session_id }]
}

// Tool errors can be whole tracebacks; keep frames small for the robot.
const MAX_ERROR_MESSAGE = 300

function truncate(text: string, max: number): string {
  return text.length > max ? text.slice(0, max - 1) + '\u2026' : text
}

function extractAssistantTextBlocks(v: unknown): string[] {
  if (!Array.isArray(v)) return []
  return v
    .filter((t): t is string => typeof t === 'string')
    .map((t) => t.trim())
    .filter((t) => t.length > 0)
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
    // Claude sometimes includes assistant_text on non-Stop hooks.
    // Treat these blocks as "thinking" so downstream robot UI can render
    // them exactly like Cursor afterAgentThought events.
    const thoughtEvents: ParsedEvent[] =
      hook === 'Stop'
        ? []
        : extractAssistantTextBlocks(p.assistant_text).map((text) => ({
            event: { kind: 'thinking', text } as const,
            session_id,
          }))

    switch (hook) {
      case 'SessionStart': {
        const source = asString(p.source)
        const cause: 'startup' | 'resume' | 'clear' | 'unknown' =
          source === 'startup' || source === 'resume' || source === 'clear' ? source : 'unknown'
        return [...thoughtEvents, { event: { kind: 'session.started', cause }, session_id }]
      }

      case 'SessionEnd':
        return [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'deny'),
          { event: { kind: 'session.ended', cause: 'unknown' }, session_id },
        ]

      case 'UserPromptSubmit': {
        const prompt = asString(p.prompt)
        return [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'deny'),
          { event: { kind: 'turn.started', prompt: prompt || undefined }, session_id },
          { event: { kind: 'message.user', text: prompt }, session_id },
        ]
      }

      case 'PreToolUse': {
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        return [...thoughtEvents, { event: { kind: 'activity.started', activity }, session_id }]
      }

      case 'PostToolUse': {
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        const out: ParsedEvent[] = [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'allow', tool_name),
          { event: { kind: 'activity.finished', activity }, session_id },
        ]

        if (tool_name === 'TodoWrite') {
          const todos = asTodos((tool_input as Record<string, unknown> | null)?.todos)
          if (todos) out.push({ event: { kind: 'todo.updated', items: todos }, session_id })
        }
        return out
      }

      case 'PostToolUseFailure': {
        // The tool ran and failed (non-zero exit, exception, ...). Without
        // this the activity never ends and the face sits in Executing.
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        const error = asString(p.error)
        const duration = typeof p.duration_ms === 'number' ? p.duration_ms : undefined
        return [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'allow', tool_name),
          {
            event: {
              kind: 'activity.failed',
              activity,
              duration_ms: duration,
              error_message: error ? truncate(error, MAX_ERROR_MESSAGE) : undefined,
              failure_kind: p.is_interrupt === true ? 'unknown' : 'error',
            },
            session_id,
          },
        ]
      }

      case 'PermissionRequest': {
        // Claude is about to show a permission prompt for this tool.
        const activity = buildActivity(tool_name, tool_input, undefined)
        const request_id = `perm_${randomUUID()}`
        const out: ParsedEvent[] = [...thoughtEvents, ...resolvePendingPermission(session_id, 'deny')]
        if (session_id) pendingPermissions.set(session_id, { request_id, tool: tool_name })
        out.push({
          event: { kind: 'permission.requested', request_id, activity, description: activity.summary || undefined },
          session_id,
        })
        return out
      }

      case 'PermissionDenied': {
        // Auto mode's classifier refused the tool. It never runs, and no
        // PostToolUse* follows, so end the activity here.
        const activity = buildActivity(tool_name, tool_input, tool_use_id)
        const reason = asString(p.reason)
        return [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'deny', tool_name),
          {
            event: {
              kind: 'activity.failed',
              activity,
              error_message: reason ? truncate(reason, MAX_ERROR_MESSAGE) : undefined,
              failure_kind: 'permission_denied',
            },
            session_id,
          },
        ]
      }

      case 'StopFailure': {
        // The turn ended on an API error (rate limit, overload, ...). Claude
        // sends this instead of Stop.
        const error = asString(p.error)
        const last = asString(p.last_assistant_message)
        return [
          ...thoughtEvents,
          ...resolvePendingPermission(session_id, 'deny'),
          {
            event: {
              kind: 'turn.ended',
              cause: 'error',
              error: error || undefined,
              last_assistant_text: last || undefined,
            },
            session_id,
          },
        ]
      }

      case 'Stop': {
        // assistant_text is scraped from the transcript (by the bridge, or by the
        // legacy forwarder); its last block is the final reply. Claude also sends
        // the final reply itself as last_assistant_message.
        const blocks = Array.isArray(p.assistant_text)
          ? (p.assistant_text as unknown[]).filter((t): t is string => typeof t === 'string')
          : []
        const last = blocks[blocks.length - 1] || asString(p.last_assistant_message) || undefined
        const out: ParsedEvent[] = [...resolvePendingPermission(session_id, 'deny')]
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
        return [
          ...thoughtEvents,
          { event: { kind: 'subagent.finished', subagent_id: session_id ?? '', cause: 'completed' }, session_id },
        ]
      }

      case 'PreCompact': {
        const trigger = asString(p.trigger)
        const t: 'auto' | 'manual' | undefined =
          trigger === 'auto' || trigger === 'manual' ? trigger : undefined
        return [...thoughtEvents, { event: { kind: 'context.compacting', trigger: t }, session_id }]
      }

      case 'Notification': {
        // Permission prompts are reported by the PermissionRequest hook
        // (above). Claude also sends "Claude needs your permission to use
        // ..." here about 6 s into a waiting prompt; it passes through as a
        // plain notification, which the robot uses for AttractingAttention.
        const message = asString(p.message)
        if (message === 'Claude is waiting for your input') {
          return thoughtEvents
        }
        return [...thoughtEvents, { event: { kind: 'notification', text: message || undefined }, session_id }]
      }

      default:
        return [...thoughtEvents, { event: { kind: 'unknown' }, session_id }]
    }
  },
}
