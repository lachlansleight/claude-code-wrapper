// Common shapes the bridge speaks internally, regardless of which agentic
// coding tool produced the event. Adapters live next door and convert raw
// agent payloads into one of these.

export type PersonalityState =
  | 'idle'
  | 'thinking'
  | 'reading'
  | 'writing'
  | 'finished'
  | 'excited'
  | 'ready'
  | 'waking'
  | 'sleep'
  | 'blocked'

export type ToolAccess = 'read' | 'write' | 'unknown'

export type NormalizedHook =
  | { kind: 'session_start'; session_id?: string }
  | { kind: 'session_end'; session_id?: string }
  | { kind: 'user_prompt'; session_id?: string }
  | { kind: 'pre_tool';  tool: string; access: ToolAccess; session_id?: string }
  | { kind: 'post_tool'; tool: string; access: ToolAccess; session_id?: string }
  | { kind: 'permission_request'; request_id: string; tool: string; description?: string; input_preview?: string }
  | { kind: 'permission_resolved'; request_id: string; behavior: 'allow' | 'deny' }
  | { kind: 'stop'; session_id?: string }
  | { kind: 'notification'; session_id?: string }
  | { kind: 'unknown' }

export interface AdapterInput {
  hook_type?: string  // explicit hook name when the agent uses one
  payload: unknown    // raw agent payload
}

export interface Adapter {
  name: string
  // Returns null when the adapter cannot interpret the event (the caller
  // should still emit a raw hook_event for observability, just no state
  // transition).
  normalize(input: AdapterInput): NormalizedHook | null
}

// Tool name → read/write classification. Mirrors robot_v2/ToolFormat.cpp::access().
const WRITE_TOOLS = new Set([
  // Claude
  'Write', 'Edit', 'MultiEdit', 'NotebookEdit', 'TodoWrite', 'Bash',
  // Codex / Cursor
  'ApplyPatch', 'Delete', 'EditNotebook', 'GenerateImage',
  'Shell', 'CallMcpTool', 'Subagent',
])

export function toolAccess(tool: string | undefined | null): ToolAccess {
  if (!tool) return 'unknown'
  if (WRITE_TOOLS.has(tool)) return 'write'
  // mcp__-prefixed tools are unknown side-effect; treat as read so the
  // robot's "reading" state covers them — they're often searches/queries.
  return 'read'
}
