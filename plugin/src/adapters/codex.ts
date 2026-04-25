import type { Adapter, AdapterInput, NormalizedHook } from './types.js'
import { toolAccess } from './types.js'

// Codex CLI uses Claude-compatible hook names: PreToolUse, PostToolUse,
// UserPromptSubmit, Stop, plus a PermissionRequest hook.
//   https://developers.openai.com/codex/hooks
//
// Tool names differ slightly (Shell, ApplyPatch, CallMcpTool) — handled
// in toolAccess().

export const codexAdapter: Adapter = {
  name: 'codex',
  normalize(input: AdapterInput): NormalizedHook | null {
    const hook = input.hook_type ?? ''
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id = typeof p.session_id === 'string' ? p.session_id : undefined
    const tool_name = typeof p.tool_name === 'string' ? p.tool_name : ''

    switch (hook) {
      case 'SessionStart':
        return { kind: 'session_start', session_id }
      case 'SessionEnd':
        return { kind: 'session_end', session_id }
      case 'UserPromptSubmit':
        return { kind: 'user_prompt', session_id }
      case 'PreToolUse':
        return { kind: 'pre_tool', tool: tool_name, access: toolAccess(tool_name), session_id }
      case 'PostToolUse':
        return { kind: 'post_tool', tool: tool_name, access: toolAccess(tool_name), session_id }
      case 'PermissionRequest': {
        const request_id = typeof p.request_id === 'string' ? p.request_id : ''
        if (!request_id) return null
        return {
          kind: 'permission_request',
          request_id,
          tool: tool_name,
          description: typeof p.description === 'string' ? p.description : undefined,
          input_preview: typeof p.input_preview === 'string' ? p.input_preview : undefined,
        }
      }
      case 'Stop':
        return { kind: 'stop', session_id }
      default:
        return null
    }
  },
}
