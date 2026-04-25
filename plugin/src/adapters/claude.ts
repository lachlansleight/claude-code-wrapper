import type { Adapter, AdapterInput, NormalizedHook } from './types.js'
import { toolAccess } from './types.js'

// Claude Code hook payloads. Shape reference:
//   https://docs.claude.com/en/docs/claude-code/hooks
//
// Every payload includes session_id. PreToolUse/PostToolUse include
// tool_name. Notification carries permission-related text in `message`.

export const claudeAdapter: Adapter = {
  name: 'claude',
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
      case 'Stop':
      case 'SubagentStop':
        return { kind: 'stop', session_id }
      case 'Notification':
        return { kind: 'notification', session_id }
      case 'PreCompact':
        return { kind: 'unknown' }
      default:
        return null
    }
  },
}
