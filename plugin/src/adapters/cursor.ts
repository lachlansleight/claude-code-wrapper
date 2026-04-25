import type { Adapter, AdapterInput, NormalizedHook } from './types.js'
import { toolAccess } from './types.js'

// Cursor 1.7+ hook event names. Docs: https://cursor.com/docs/hooks
//
// Cursor passes a JSON payload on stdin that includes `hook_event_name`
// plus `conversation_id`, `generation_id`, etc. Per-event extras are
// merged in (`tool_name`, `command`, `file_path`, ...).
//
// We assume the user wires `helpers/cursor-hook-forward.mjs` (or
// equivalent) to forward those payloads as
//   { hook_type: <hook_event_name>, payload: <full stdin JSON> }
// to POST /hooks/cursor.

export const cursorAdapter: Adapter = {
  name: 'cursor',
  normalize(input: AdapterInput): NormalizedHook | null {
    const hook = input.hook_type ?? ''
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id =
      typeof p.conversation_id === 'string' ? p.conversation_id :
      typeof p.session_id === 'string' ? p.session_id : undefined

    switch (hook) {
      case 'sessionStart':
        return { kind: 'session_start', session_id }
      case 'sessionEnd':
        return { kind: 'session_end', session_id }

      case 'beforeSubmitPrompt':
        return { kind: 'user_prompt', session_id }

      case 'preToolUse': {
        const tool = typeof p.tool_name === 'string' ? p.tool_name : ''
        return { kind: 'pre_tool', tool, access: toolAccess(tool), session_id }
      }
      case 'postToolUse':
      case 'postToolUseFailure': {
        const tool = typeof p.tool_name === 'string' ? p.tool_name : ''
        return { kind: 'post_tool', tool, access: toolAccess(tool), session_id }
      }

      case 'beforeShellExecution':
        return { kind: 'pre_tool', tool: 'Shell', access: 'write', session_id }
      case 'afterShellExecution':
        return { kind: 'post_tool', tool: 'Shell', access: 'write', session_id }

      case 'beforeMCPExecution': {
        const tool = typeof p.tool_name === 'string' ? p.tool_name : 'CallMcpTool'
        return { kind: 'pre_tool', tool, access: toolAccess(tool), session_id }
      }
      case 'afterMCPExecution': {
        const tool = typeof p.tool_name === 'string' ? p.tool_name : 'CallMcpTool'
        return { kind: 'post_tool', tool, access: toolAccess(tool), session_id }
      }

      case 'beforeReadFile':
      case 'beforeTabFileRead':
        return { kind: 'pre_tool', tool: 'Read', access: 'read', session_id }
      case 'afterFileEdit':
      case 'afterTabFileEdit':
        return { kind: 'post_tool', tool: 'Edit', access: 'write', session_id }

      case 'stop':
      case 'subagentStop':
        return { kind: 'stop', session_id }

      case 'preCompact':
      case 'subagentStart':
      case 'afterAgentResponse':
      case 'afterAgentThought':
        return { kind: 'unknown' }

      default:
        return null
    }
  },
}
