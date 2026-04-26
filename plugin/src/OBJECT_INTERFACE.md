# Agent event interface

Canonical event vocabulary the bridge emits over WebSocket. Each event
is a translation of one or more native hook payloads from a specific
agentic coding platform (Claude Code, Cursor, Codex, OpenCode, …) into
a shape that any consumer can react to without knowing which platform
produced it.

**The bridge has no opinion about presentation.** It does not know what
"thinking" looks like, when a robot should sleep, or how long to linger
on a tool. Those decisions live entirely in the consumer (e.g. the ESP32
firmware). The bridge's job is: receive a vendor-specific payload,
classify it, emit an `AgentEvent`.

## Design principles

1. **Lossless when possible.** Every event carries a `raw` field with
   the original payload, so consumers that need vendor-specific detail
   can reach for it. The classified fields are for consumers that
   don't.
2. **Lifecycle, not state.** Events describe *things that happened*
   (prompt submitted, tool started, turn ended). They never describe
   *what state the agent is in* — that's the consumer's interpretation.
3. **Activities, not tools.** Tool names like `Bash`, `apply_patch`,
   `Shell`, `tool.execute.before` get collapsed into a small
   `ActivityKind` enum so the consumer doesn't have to maintain a
   per-vendor lookup table. Unmapped tools fall through to
   `tool.unknown` with the original name preserved.
4. **No personality.** No `idle` / `thinking` / `excited` / `blocked`.
   Those are derived states; the consumer derives them.

## Top-level envelope

Every event the bridge emits over WebSocket is wrapped:

```ts
interface AgentEventEnvelope {
  type: 'agent_event'
  agent: AgentName            // 'claude' | 'cursor' | 'codex' | 'opencode'
  ts: number                  // bridge wall-clock at emit, ms since epoch
  session_id?: string         // agent's own session/conversation id, if known
  turn_id?: string            // per-turn id when the platform exposes one
  event: AgentEvent           // the classified event (below)
  raw: {                      // unmodified vendor payload
    hook_type: string         // vendor-native event/hook name
    payload: unknown          // exact stdin JSON or plugin args
  }
}
```

`agent`, `session_id`, `turn_id`, and `ts` are duplicated outside
`event` deliberately so consumers can route/filter without needing to
discriminate the union first.

## AgentEvent — the classified event

```ts
type AgentEvent =
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
```

### Session

```ts
interface SessionStartedEvent {
  kind: 'session.started'
  // Why this session began. 'startup' = fresh launch, 'resume' = picking
  // up an existing transcript, 'clear' = `/clear`-like reset.
  cause?: 'startup' | 'resume' | 'clear' | 'unknown'
}

interface SessionEndedEvent {
  kind: 'session.ended'
  cause?: 'completed' | 'aborted' | 'error' | 'window_close' | 'user_close' | 'unknown'
  duration_ms?: number
}
```

### Turn (one user→agent→user round-trip)

A turn opens on a user prompt and closes on agent stop. Cursor + Codex
expose `turn_id`s explicitly; Claude implicitly via `UserPromptSubmit`
→ `Stop`; OpenCode via `message.updated` (user role) → `session.idle`.

```ts
interface TurnStartedEvent {
  kind: 'turn.started'
  prompt?: string             // user prompt text if available
}

interface TurnEndedEvent {
  kind: 'turn.ended'
  cause?: 'completed' | 'aborted' | 'error' | 'unknown'
  last_assistant_text?: string  // best-effort summary of agent's final reply
}
```

### Messages

Most platforms expose user/assistant content somewhere. Capture it when
it's available; consumers shouldn't rely on it being present.

```ts
interface UserMessageEvent {
  kind: 'message.user'
  text: string
}

interface AssistantMessageEvent {
  kind: 'message.assistant'
  text: string
  // True when this is the final reply of a turn (last assistant message
  // before TurnEnded). False/undefined when streaming partials.
  final?: boolean
}

interface ThinkingEvent {
  kind: 'thinking'
  text: string
}
```

### Activities

The unified replacement for "tool use." An activity has a *kind*
(what the agent is conceptually doing) and an *operation* (the underlying
tool call, lossless).

```ts
type ActivityKind =
  | 'file.read'         // read a file
  | 'file.write'        // create/edit/overwrite a file (Edit, Write, MultiEdit, apply_patch, ...)
  | 'file.delete'       // remove a file
  | 'shell.exec'        // run a shell/bash command
  | 'shell.background'  // BashOutput, KillShell — interact with a running shell
  | 'search.code'       // Grep, ripgrep-style code search
  | 'search.files'      // Glob / file-name search
  | 'web.fetch'         // WebFetch — pull a URL
  | 'web.search'        // WebSearch — query a search engine
  | 'mcp.call'          // any mcp__* tool call
  | 'todo.write'        // TodoWrite / todo.updated style
  | 'subagent.spawn'    // Task / Subagent / agent-as-tool
  | 'notebook.edit'     // Jupyter / NotebookEdit
  | 'image.generate'    // GenerateImage and similar
  | 'plan.exit'         // ExitPlanMode and similar control-flow tools
  | 'tool.unknown'      // anything we couldn't classify

interface ActivityRef {
  // Stable id for correlating started/finished/failed across the same
  // activity. Matches the platform's tool_use_id when available; we
  // mint one otherwise.
  id: string
  kind: ActivityKind
  // Original vendor-side tool name, e.g. 'Bash', 'apply_patch',
  // 'Shell', 'tool.execute.before' → 'Edit'. Always preserved so
  // consumers can render or filter by it.
  tool: string
  // Short human-friendly summary, ≤80 chars. e.g. "ls -la",
  // "src/index.ts", "https://example.com". Empty string when no useful
  // detail is available.
  summary: string
}

interface ActivityStartedEvent {
  kind: 'activity.started'
  activity: ActivityRef
}

interface ActivityFinishedEvent {
  kind: 'activity.finished'
  activity: ActivityRef
  duration_ms?: number
  // When the platform exposes the tool result, a *truncated* preview.
  // Consumers wanting the full output should use `raw.payload`.
  output_preview?: string
}

interface ActivityFailedEvent {
  kind: 'activity.failed'
  activity: ActivityRef
  duration_ms?: number
  error_message?: string
  failure_kind?: 'timeout' | 'error' | 'permission_denied' | 'unknown'
}
```

### Permissions

```ts
interface PermissionRequestedEvent {
  kind: 'permission.requested'
  request_id: string          // agent's id when present, otherwise minted by bridge
  activity?: ActivityRef      // the activity gated by this request, if known
  description?: string        // human-friendly text from the agent
}

interface PermissionResolvedEvent {
  kind: 'permission.resolved'
  request_id: string
  decision: 'allow' | 'deny'
  // Some platforms distinguish "allow once" vs "allow always". We
  // collapse to allow|deny but expose the original via this hint.
  scope?: 'once' | 'session' | 'always' | 'unknown'
}
```

### Todos

```ts
interface TodoItem {
  text: string
  status: 'pending' | 'in_progress' | 'completed' | 'cancelled'
}

interface TodoUpdatedEvent {
  kind: 'todo.updated'
  items: TodoItem[]
}
```

### Subagents

Distinct from activities: a subagent is a long-running nested task that
emits its own activities. Treat it like a session-within-a-session.

```ts
interface SubagentStartedEvent {
  kind: 'subagent.started'
  subagent_id: string
  subagent_type?: string      // e.g. 'general-purpose', 'code-reviewer'
  task?: string               // initial instruction
}

interface SubagentFinishedEvent {
  kind: 'subagent.finished'
  subagent_id: string
  cause?: 'completed' | 'aborted' | 'error' | 'unknown'
}
```

### Agent asking the user

Some platforms have a dedicated tool for the agent to pause and ask the
user a clarifying question (Cursor's `ask_question`). Surfaced as a
first-class event because it's user-actionable — the consumer probably
wants to render it differently from a regular assistant message.

```ts
interface AgentQuestionEvent {
  kind: 'agent.question'
  text: string                // the question the agent is asking
  options?: string[]          // suggested answers when the platform provides them
}
```

### Context / housekeeping

```ts
interface ContextCompactingEvent {
  kind: 'context.compacting'
  trigger?: 'auto' | 'manual'
  context_usage_percent?: number
}

interface NotificationEvent {
  kind: 'notification'
  // Platform-specific notification text. No structure imposed.
  text?: string
}

interface UnknownEvent {
  kind: 'unknown'
  // Hatch for events we recognise but haven't classified yet. Lets
  // consumers see them without breaking on unknown union members.
}
```

## Mapping table

How each platform's native events project onto the abstract interface.
Empty cells mean the platform doesn't expose that event (or the bridge
doesn't yet handle it).

| Abstract event           | Claude Code            | Codex CLI              | Cursor 1.7+                                | OpenCode                       |
|--------------------------|------------------------|------------------------|--------------------------------------------|--------------------------------|
| session.started          | SessionStart           | SessionStart           | sessionStart                               | session.created                |
| session.ended            | SessionEnd             | —                      | sessionEnd                                 | session.deleted                |
| turn.started             | UserPromptSubmit       | UserPromptSubmit       | beforeSubmitPrompt                         | message.updated (role=user)    |
| turn.ended               | Stop                   | Stop                   | stop                                       | session.idle                   |
| message.user             | UserPromptSubmit.prompt| UserPromptSubmit.prompt| beforeSubmitPrompt.prompt                  | message.updated.content        |
| message.assistant        | (transcript tail)      | Stop.last_assistant_message | afterAgentResponse                    | message.updated (role=asst)    |
| thinking                 | (transcript tail)      | —                      | afterAgentThought                          | message.part.updated (thinking)|
| activity.started         | PreToolUse             | PreToolUse             | preToolUse †                               | tool.execute.before            |
| activity.finished        | PostToolUse            | PostToolUse            | postToolUse †                              | tool.execute.after             |
| activity.failed          | (PostToolUse + error)  | (PostToolUse + error)  | postToolUseFailure                         | (tool.execute.after + error)   |
| permission.requested     | (Notification) ‡       | PermissionRequest      | (preToolUse permission decision)           | permission.asked               |
| permission.resolved      | (n/a — terminal owns)  | (PermissionRequest reply)| (n/a — IDE owns)                         | permission.replied             |
| todo.updated             | PostToolUse(TodoWrite) | PostToolUse(TodoWrite) | (n/a — Cursor has no todo tool)            | todo.updated                   |
| subagent.started         | PreToolUse(Task)       | PreToolUse(Subagent)   | subagentStart                              | (n/a)                          |
| subagent.finished        | SubagentStop           | (Stop with parent)     | subagentStop                               | (n/a)                          |
| agent.question           | (n/a)                  | (n/a)                  | preToolUse(ask_question)                   | (n/a)                          |
| context.compacting       | PreCompact             | (n/a)                  | preCompact                                 | session.compacted              |
| notification             | Notification           | (n/a)                  | (n/a)                                      | (n/a)                          |

† Cursor's `preToolUse` / `postToolUse` fire for **every** built-in
tool (`read_file`, `edit_file`, `run_terminal_cmd`, `semantic_search`,
`grep_search`, `search_files`, `web_search`, `image_generation`,
`browser`, `ask_question`, `fetch_rules`). The bridge subscribes to
those as the canonical source. Cursor *also* fires the
narrower-payload variants (`beforeShellExecution`/`afterShellExecution`,
`beforeMCPExecution`/`afterMCPExecution`, `beforeReadFile`,
`afterFileEdit`) alongside, with richer per-event detail (e.g. the raw
shell command, MCP server URL). The bridge consumes those for
populating `ActivityRef.summary` but does **not** double-emit
activities for them.

‡ Claude Code's permission flow is delivered via `Notification` hook
text; the bridge synthesises a `permission.requested` only when it can
parse a request id out of that text. Without an MCP channel, the bridge
cannot deliver verdicts back to Claude Code regardless.

## ActivityKind classification table

How each vendor's tool name maps onto `ActivityKind`. Maintained as a
single shared table inside the bridge so adding a tool to one platform
benefits all consumers.

Cursor + Codex use a mix of CamelCase and snake_case identifiers — the
bridge matches case-sensitively against the canonical list below.

| ActivityKind       | Claude tools                  | Codex tools                  | Cursor tools                       | OpenCode tool ids   |
|--------------------|-------------------------------|------------------------------|------------------------------------|---------------------|
| file.read          | Read                          | (apply_patch read paths)     | read_file                          | read                |
| file.write         | Write, Edit, MultiEdit        | apply_patch                  | edit_file                          | write, edit         |
| file.delete        | (via Bash)                    | apply_patch (deletions)      | (Cursor auto-delete; no hook)      | (n/a)               |
| shell.exec         | Bash                          | Bash                         | run_terminal_cmd                   | bash, shell         |
| shell.background   | BashOutput, KillShell         | (n/a)                        | (n/a)                              | (n/a)               |
| search.code        | Grep                          | (n/a)                        | grep_search, semantic_search       | grep                |
| search.files       | Glob                          | (n/a)                        | search_files                       | glob                |
| web.fetch          | WebFetch                      | (n/a)                        | browser                            | webfetch            |
| web.search         | WebSearch                     | (n/a)                        | web_search                         | websearch           |
| mcp.call           | mcp__*                        | mcp__*                       | (any tool via beforeMCPExecution)  | (mcp tools)         |
| todo.write         | TodoWrite                     | TodoWrite                    | (n/a)                              | todo                |
| subagent.spawn     | Task                          | Subagent                     | (subagentStart event)              | (n/a)               |
| notebook.edit      | NotebookEdit                  | EditNotebook                 | (n/a)                              | (n/a)               |
| image.generate     | (n/a)                         | GenerateImage                | image_generation                   | (n/a)               |
| plan.exit          | ExitPlanMode                  | (n/a)                        | (n/a)                              | (n/a)               |
| (→ agent.question) | (n/a)                         | (n/a)                        | ask_question                       | (n/a)               |
| (→ rules fetch)    | (n/a)                         | (n/a)                        | fetch_rules                        | (n/a)               |
| tool.unknown       | *(everything else)*           | *(everything else)*          | *(everything else)*                | *(everything else)* |

`ask_question` is special: it doesn't produce an `activity.*` event —
the parser intercepts it and emits an `agent.question` event with the
question text from `tool_input`. `fetch_rules` is a Cursor-internal
config lookup that's noisy and uninteresting for consumers; the parser
demotes it to `tool.unknown` (or filters it out entirely — TBD when we
write the parser).

## Status

The bridge emits a single typed WebSocket frame: `agent_event`,
matching the `AgentEventEnvelope` shape above. The earlier `hook_event`
and `state_event` frames have been removed. Personality derivation
lives in consumers (the firmware in `robot_v2/`), not the bridge.

Implemented by:

- `plugin/src/agent-event.ts` — type vocabulary
- `plugin/src/activity-classify.ts` — tool name → `ActivityKind`
- `plugin/src/activity-summary.ts` — `ActivityRef.summary` builder
- `plugin/src/adapters/{claude,codex,cursor,opencode}.ts` — per-agent
  parsers returning `ParsedEvent[]`
- `plugin/src/http.ts` / `ws.ts` — wraps each parsed event in an
  envelope and broadcasts
