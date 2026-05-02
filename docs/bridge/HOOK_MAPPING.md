# HOOK_MAPPING

This document describes how adapter-specific hooks/events map to the shared `AgentEvent` vocabulary.

Scope:
- Adapters: `claude`, `codex`, `cursor`, `opencode`
- Source of truth: `plugin/src/adapters/*.ts` and `plugin/src/activity-classify.ts`

## SPECIFIC -> GENERIC

### Claude

| Claude hook | Generic event(s) emitted | Notes |
|---|---|---|
| `SessionStart` | `session.started` | `cause` from `payload.source` (`startup`, `resume`, `clear`, else `unknown`) |
| `SessionEnd` | `session.ended` | Cause currently `unknown` |
| `UserPromptSubmit` | `turn.started`, `message.user` | Prompt copied from `payload.prompt` |
| `PreToolUse` | `activity.started` | Activity from `tool_name`, `tool_input`, `tool_use_id` |
| `PostToolUse` | `activity.finished` (+ `todo.updated` for `TodoWrite`) | `todo.updated` emitted when `tool_name === TodoWrite` and `todos` parse succeeds |
| `Stop` | `message.assistant` (optional), `turn.ended` | Uses `assistant_text` array (last block) if present |
| `SubagentStop` | `subagent.finished` | Uses session id as fallback `subagent_id` |
| `PreCompact` | `context.compacting` | Optional trigger from `payload.trigger` (`auto`/`manual`) |
| `Notification` | `notification` (+ optional `permission.requested`) | Emits `permission.requested` if a request token is parsed from message text |
| any other hook | `unknown` | Default fallback |

#### Claude activities

`PreToolUse`/`PostToolUse` use `classifyTool('claude', tool_name)`:

| Claude tool | Activity kind |
|---|---|
| `Read` | `file.read` |
| `Write`, `Edit`, `MultiEdit` | `file.write` |
| `Bash` | `shell.exec` |
| `BashOutput`, `KillShell`, `KillBash` | `shell.background` |
| `Grep` | `search.code` |
| `Glob` | `search.files` |
| `WebFetch` | `web.fetch` |
| `WebSearch` | `web.search` |
| `TodoWrite` | `todo.write` |
| `Task` | `subagent.spawn` |
| `NotebookEdit` | `notebook.edit` |
| `ExitPlanMode` | `plan.exit` |
| `mcp__*` | `mcp.call` |
| anything else | `tool.unknown` |

### Codex

| Codex hook | Generic event(s) emitted | Notes |
|---|---|---|
| `SessionStart` | `session.started` | `cause` from `payload.source` |
| `UserPromptSubmit` | `turn.started`, `message.user` | Prompt from `payload.prompt` |
| `PreToolUse` | `activity.started` | Activity from tool fields |
| `PostToolUse` | `activity.failed` OR `activity.finished` (+ optional `todo.updated`) | If `error`/`error_message` is present => `activity.failed`; else `activity.finished`; `TodoWrite` also emits `todo.updated` |
| `PermissionRequest` | `permission.requested` | Requires `request_id`; optional activity+description |
| `Stop` | `message.assistant` (optional), `turn.ended` | Uses `payload.last_assistant_message` |
| any other hook | `unknown` | Default fallback |

#### Codex activities

`PreToolUse`/`PostToolUse` use `classifyTool('codex', tool_name)`:

| Codex tool | Activity kind |
|---|---|
| `Bash`, `Shell` | `shell.exec` |
| `apply_patch`, `ApplyPatch` | `file.write` |
| `Subagent` | `subagent.spawn` |
| `EditNotebook` | `notebook.edit` |
| `GenerateImage` | `image.generate` |
| `TodoWrite` | `todo.write` |
| `mcp__*` | `mcp.call` |
| anything else | `tool.unknown` |

### Cursor

| Cursor hook | Generic event(s) emitted | Notes |
|---|---|---|
| `sessionStart` | `session.started` | Cause fixed to `startup` |
| `sessionEnd` | `session.ended` | Cause currently `unknown` |
| `beforeSubmitPrompt` | `turn.started`, `message.user` | Prompt from `payload.prompt` |
| `afterAgentResponse` | `message.assistant` | `final: true` |
| `afterAgentThought` | `thinking` | Text from `text`/`thought`/`content` |
| `preToolUse` | `activity.started` OR `agent.question` OR nothing | `ask_question` => `agent.question`; `fetch_rules` dropped; otherwise `activity.started` |
| `postToolUse` | `activity.finished` or nothing | `fetch_rules`/`ask_question` dropped |
| `postToolUseFailure` | `activity.failed` or nothing | `fetch_rules`/`ask_question` dropped |
| `stop` | `turn.ended` | Cause `completed` |
| `subagentStart` | `subagent.started` | Includes id/type/task if available |
| `subagentStop` | `subagent.finished` | Cause `completed` |
| `preCompact` | `context.compacting` | No trigger currently mapped |
| `beforeShellExecution`, `afterShellExecution` | none | Hint-only for future activity summary enrichment |
| `beforeMCPExecution`, `afterMCPExecution` | none | Hint-only for summary enrichment |
| `beforeReadFile`, `beforeTabFileRead` | none | Hint-only for summary enrichment |
| `afterFileEdit`, `afterTabFileEdit` | none | Hint-only for summary enrichment |
| any other hook | `unknown` | Default fallback |

#### Cursor activities

`preToolUse`/`postToolUse`/`postToolUseFailure` use `classifyTool('cursor', tool_name)` when not intercepted:

| Cursor tool | Activity kind |
|---|---|
| `read_file` | `file.read` |
| `edit_file` | `file.write` |
| `run_terminal_cmd` | `shell.exec` |
| `grep_search`, `semantic_search` | `search.code` |
| `search_files` | `search.files` |
| `web_search` | `web.search` |
| `browser` | `web.fetch` |
| `image_generation` | `image.generate` |
| `mcp__*` | `mcp.call` |
| anything else | `tool.unknown` |

Special-cased tools:
- `ask_question`: emits `agent.question` (not activity events)
- `fetch_rules`: dropped

### OpenCode

| OpenCode hook | Generic event(s) emitted | Notes |
|---|---|---|
| `session.created` | `session.started` | Cause fixed to `startup` |
| `session.deleted` | `session.ended` | Cause currently `unknown` |
| `session.idle` | `turn.ended` | Cause `completed`; also clears internal turn-tracking state |
| `session.compacted` | `context.compacting` | |
| `tool.execute.before` | `activity.started` | |
| `tool.execute.after` | `activity.failed` OR `activity.finished` (+ optional `todo.updated`) | Error => failed; `tool=todo` may also emit `todo.updated` |
| `todo.updated` | `todo.updated` | Uses `payload.todos` or `payload.items` |
| `permission.asked` | `permission.requested` | Requires `permissionID`/`id`; optional activity/description |
| `permission.replied` | `permission.resolved` | `response` `once`/`always` => allow; otherwise deny |
| `message.updated`, `message.part.updated` | `thinking` OR user turn/message OR assistant message | `partType/type=thinking` => `thinking`; user role may emit `turn.started` + `message.user`; assistant role => `message.assistant` |
| any other hook | `unknown` | Default fallback |

#### OpenCode activities

`tool.execute.before`/`tool.execute.after` use `classifyTool('opencode', tool)`:

| OpenCode tool | Activity kind |
|---|---|
| `read` | `file.read` |
| `write`, `edit` | `file.write` |
| `bash`, `shell` | `shell.exec` |
| `grep` | `search.code` |
| `glob` | `search.files` |
| `webfetch` | `web.fetch` |
| `websearch` | `web.search` |
| `todo` | `todo.write` |
| `mcp__*` | `mcp.call` |
| anything else | `tool.unknown` |

## GENERIC -> SPECIFIC

### Generic event mapping table

| Generic event | Claude | Codex | Cursor | OpenCode |
|---|---|---|---|---|
| `session.started` | `SessionStart` | `SessionStart` | `sessionStart` | `session.created` |
| `session.ended` | `SessionEnd` | - | `sessionEnd` | `session.deleted` |
| `turn.started` | `UserPromptSubmit` | `UserPromptSubmit` | `beforeSubmitPrompt` | `message.updated`/`message.part.updated` (role=user, first in turn) |
| `turn.ended` | `Stop` | `Stop` | `stop` | `session.idle` |
| `message.user` | `UserPromptSubmit` | `UserPromptSubmit` | `beforeSubmitPrompt` | `message.updated`/`message.part.updated` (role=user) |
| `message.assistant` | `Stop` (if `assistant_text` present) | `Stop` (if `last_assistant_message` present) | `afterAgentResponse` | `message.updated`/`message.part.updated` (role=assistant) |
| `thinking` | - | - | `afterAgentThought` | `message.updated`/`message.part.updated` (`type`/`partType`=`thinking`) |
| `activity.started` | `PreToolUse` | `PreToolUse` | `preToolUse` (excluding `ask_question`/`fetch_rules`) | `tool.execute.before` |
| `activity.finished` | `PostToolUse` | `PostToolUse` (no error) | `postToolUse` (excluding `ask_question`/`fetch_rules`) | `tool.execute.after` (no error) |
| `activity.failed` | - | `PostToolUse` (with `error`/`error_message`) | `postToolUseFailure` | `tool.execute.after` (with `error`/`error_message`) |
| `permission.requested` | `Notification` (parsed token) | `PermissionRequest` | - | `permission.asked` |
| `permission.resolved` | - | - | - | `permission.replied` |
| `todo.updated` | `PostToolUse` when `tool_name=TodoWrite` | `PostToolUse` when `tool_name=TodoWrite` | - | `tool.execute.after` when `tool=todo`; also `todo.updated` |
| `subagent.started` | - | - | `subagentStart` | - |
| `subagent.finished` | `SubagentStop` | - | `subagentStop` | - |
| `agent.question` | - | - | `preToolUse` with `tool_name=ask_question` | - |
| `context.compacting` | `PreCompact` | - | `preCompact` | `session.compacted` |
| `notification` | `Notification` | - | - | - |
| `unknown` | any unmapped hook | any unmapped hook | any unmapped hook | any unmapped hook |

### Known activities (tool -> activity kind)

This is the canonical per-adapter classification from `activity-classify.ts`.

| Activity kind | Claude tools | Codex tools | Cursor tools | OpenCode tools |
|---|---|---|---|---|
| `file.read` | `Read` | - | `read_file` | `read` |
| `file.write` | `Write`, `Edit`, `MultiEdit` | `apply_patch`, `ApplyPatch` | `edit_file` | `write`, `edit` |
| `file.delete` | - | - | - | - |
| `shell.exec` | `Bash` | `Bash`, `Shell` | `run_terminal_cmd` | `bash`, `shell` |
| `shell.background` | `BashOutput`, `KillShell`, `KillBash` | - | - | - |
| `search.code` | `Grep` | - | `grep_search`, `semantic_search` | `grep` |
| `search.files` | `Glob` | - | `search_files` | `glob` |
| `web.fetch` | `WebFetch` | - | `browser` | `webfetch` |
| `web.search` | `WebSearch` | - | `web_search` | `websearch` |
| `mcp.call` | any `mcp__*` | any `mcp__*` | any `mcp__*` | any `mcp__*` |
| `todo.write` | `TodoWrite` | `TodoWrite` | - | `todo` |
| `subagent.spawn` | `Task` | `Subagent` | - | - |
| `notebook.edit` | `NotebookEdit` | `EditNotebook` | - | - |
| `image.generate` | - | `GenerateImage` | `image_generation` | - |
| `plan.exit` | `ExitPlanMode` | - | - | - |
| `tool.unknown` | any unlisted tool | any unlisted tool | any unlisted tool | any unlisted tool |

Notes:
- Cursor also has non-activity tool hooks:
  - `ask_question` -> `agent.question`
  - `fetch_rules` -> dropped
- Activity summary hint hooks in Cursor (`before*`/`after*` shell/MCP/file variants) do not emit generic events directly.
