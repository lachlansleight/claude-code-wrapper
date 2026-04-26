import { randomUUID } from 'node:crypto'
import type { Parser, ParsedEvent } from './types.js'
import type { ActivityRef, TodoItem, ParserInput } from '../agent-event.js'
import { classifyTool } from '../activity-classify.js'
import { summarize } from '../activity-summary.js'

// Codex CLI shares Claude's hook names: PreToolUse, PostToolUse,
// UserPromptSubmit, Stop. Adds PermissionRequest. Tool names: Bash,
// apply_patch, Subagent, EditNotebook, GenerateImage, mcp__*.

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
    kind: classifyTool('codex', toolName),
    tool: toolName,
    summary: summarize(toolName, toolInput),
  }
}

export const codexParser: Parser = {
  name: 'codex',
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
        if (tool_name === 'TodoWrite') {
          const todos = asTodos((tool_input as Record<string, unknown> | null)?.todos)
          if (todos) out.push({ event: { kind: 'todo.updated', items: todos }, session_id })
        }
        return out
      }

      case 'PermissionRequest': {
        const request_id = asString(p.request_id)
        if (!request_id) return [{ event: { kind: 'unknown' }, session_id }]
        const activity = tool_name ? buildActivity(tool_name, tool_input, tool_use_id) : undefined
        return [
          {
            event: {
              kind: 'permission.requested',
              request_id,
              activity,
              description: asString(p.description) || undefined,
            },
            session_id,
          },
        ]
      }

      case 'Stop': {
        const last = asString(p.last_assistant_message)
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

      default:
        return [{ event: { kind: 'unknown' }, session_id }]
    }
  },
}
