import type { Adapter, AdapterInput, NormalizedHook } from './types.js'
import { toolAccess } from './types.js'

// OpenCode event names. Reference: https://opencode.ai/docs/plugins
//
// OpenCode plugins are TS/JS files. The bundled helper at
// helpers/opencode-bridge-plugin.ts subscribes to the relevant events
// and forwards them as
//   { hook_type: <event>, payload: <input or merged input/output> }
// to POST /hooks/opencode.

export const opencodeAdapter: Adapter = {
  name: 'opencode',
  normalize(input: AdapterInput): NormalizedHook | null {
    const hook = input.hook_type ?? ''
    const p = (input.payload ?? {}) as Record<string, unknown>
    const session_id =
      typeof p.sessionID === 'string' ? p.sessionID :
      typeof p.sessionId === 'string' ? p.sessionId :
      typeof p.session_id === 'string' ? p.session_id : undefined
    const tool =
      typeof p.tool === 'string' ? p.tool :
      typeof p.tool_name === 'string' ? p.tool_name : ''

    switch (hook) {
      case 'session.created':
        return { kind: 'session_start', session_id }
      case 'session.deleted':
        return { kind: 'session_end', session_id }
      case 'session.idle':
        return { kind: 'stop', session_id }

      case 'message.updated':
      case 'tui.prompt.append':
        // Heuristic: a freshly-updated user message means a prompt was
        // submitted. The plugin filters this so we only forward on
        // user-role messages, but we stay defensive here.
        return { kind: 'user_prompt', session_id }

      case 'tool.execute.before':
        return { kind: 'pre_tool', tool, access: toolAccess(tool), session_id }
      case 'tool.execute.after':
        return { kind: 'post_tool', tool, access: toolAccess(tool), session_id }

      case 'permission.asked': {
        const request_id =
          typeof p.permissionID === 'string' ? p.permissionID :
          typeof p.id === 'string' ? p.id : ''
        if (!request_id) return null
        return {
          kind: 'permission_request',
          request_id,
          tool,
          description: typeof p.title === 'string' ? p.title : undefined,
        }
      }
      case 'permission.replied': {
        const request_id =
          typeof p.permissionID === 'string' ? p.permissionID :
          typeof p.id === 'string' ? p.id : ''
        const response = p.response
        const behavior: 'allow' | 'deny' =
          response === 'once' || response === 'always' ? 'allow' : 'deny'
        if (!request_id) return null
        return { kind: 'permission_resolved', request_id, behavior }
      }

      default:
        return null
    }
  },
}
