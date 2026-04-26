// Canonical agent event vocabulary the bridge emits over WebSocket.
// See OBJECT_INTERFACE.md for the full spec.

export type AgentName = 'claude' | 'cursor' | 'codex' | 'opencode'

export type ActivityKind =
  | 'file.read'
  | 'file.write'
  | 'file.delete'
  | 'shell.exec'
  | 'shell.background'
  | 'search.code'
  | 'search.files'
  | 'web.fetch'
  | 'web.search'
  | 'mcp.call'
  | 'todo.write'
  | 'subagent.spawn'
  | 'notebook.edit'
  | 'image.generate'
  | 'plan.exit'
  | 'tool.unknown'

export interface ActivityRef {
  id: string
  kind: ActivityKind
  tool: string
  summary: string
}

export interface SessionStartedEvent {
  kind: 'session.started'
  cause?: 'startup' | 'resume' | 'clear' | 'unknown'
}

export interface SessionEndedEvent {
  kind: 'session.ended'
  cause?: 'completed' | 'aborted' | 'error' | 'window_close' | 'user_close' | 'unknown'
  duration_ms?: number
}

export interface TurnStartedEvent {
  kind: 'turn.started'
  prompt?: string
}

export interface TurnEndedEvent {
  kind: 'turn.ended'
  cause?: 'completed' | 'aborted' | 'error' | 'unknown'
  last_assistant_text?: string
}

export interface UserMessageEvent {
  kind: 'message.user'
  text: string
}

export interface AssistantMessageEvent {
  kind: 'message.assistant'
  text: string
  final?: boolean
}

export interface ThinkingEvent {
  kind: 'thinking'
  text: string
}

export interface ActivityStartedEvent {
  kind: 'activity.started'
  activity: ActivityRef
}

export interface ActivityFinishedEvent {
  kind: 'activity.finished'
  activity: ActivityRef
  duration_ms?: number
  output_preview?: string
}

export interface ActivityFailedEvent {
  kind: 'activity.failed'
  activity: ActivityRef
  duration_ms?: number
  error_message?: string
  failure_kind?: 'timeout' | 'error' | 'permission_denied' | 'unknown'
}

export interface PermissionRequestedEvent {
  kind: 'permission.requested'
  request_id: string
  activity?: ActivityRef
  description?: string
}

export interface PermissionResolvedEvent {
  kind: 'permission.resolved'
  request_id: string
  decision: 'allow' | 'deny'
  scope?: 'once' | 'session' | 'always' | 'unknown'
}

export interface TodoItem {
  text: string
  status: 'pending' | 'in_progress' | 'completed' | 'cancelled'
}

export interface TodoUpdatedEvent {
  kind: 'todo.updated'
  items: TodoItem[]
}

export interface SubagentStartedEvent {
  kind: 'subagent.started'
  subagent_id: string
  subagent_type?: string
  task?: string
}

export interface SubagentFinishedEvent {
  kind: 'subagent.finished'
  subagent_id: string
  cause?: 'completed' | 'aborted' | 'error' | 'unknown'
}

export interface AgentQuestionEvent {
  kind: 'agent.question'
  text: string
  options?: string[]
}

export interface ContextCompactingEvent {
  kind: 'context.compacting'
  trigger?: 'auto' | 'manual'
  context_usage_percent?: number
}

export interface NotificationEvent {
  kind: 'notification'
  text?: string
}

export interface UnknownEvent {
  kind: 'unknown'
}

export type AgentEvent =
  | SessionStartedEvent
  | SessionEndedEvent
  | TurnStartedEvent
  | TurnEndedEvent
  | UserMessageEvent
  | AssistantMessageEvent
  | ThinkingEvent
  | ActivityStartedEvent
  | ActivityFinishedEvent
  | ActivityFailedEvent
  | PermissionRequestedEvent
  | PermissionResolvedEvent
  | TodoUpdatedEvent
  | SubagentStartedEvent
  | SubagentFinishedEvent
  | AgentQuestionEvent
  | ContextCompactingEvent
  | NotificationEvent
  | UnknownEvent

export interface AgentEventEnvelope {
  type: 'agent_event'
  agent: AgentName
  ts: number
  session_id?: string
  turn_id?: string
  event: AgentEvent
}

export interface ParserInput {
  hook_type: string
  payload: unknown
}
