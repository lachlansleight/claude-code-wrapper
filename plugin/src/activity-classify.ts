import type { ActivityKind, AgentName } from './agent-event.js'

// Per-agent tool-name → ActivityKind tables. Mirrors the classification
// table at the bottom of OBJECT_INTERFACE.md. Case-sensitive match.

const CLAUDE: Record<string, ActivityKind> = {
  Read: 'file.read',
  Write: 'file.write',
  Edit: 'file.write',
  MultiEdit: 'file.write',
  Bash: 'shell.exec',
  BashOutput: 'shell.background',
  KillShell: 'shell.background',
  KillBash: 'shell.background',
  Grep: 'search.code',
  Glob: 'search.files',
  WebFetch: 'web.fetch',
  WebSearch: 'web.search',
  TodoWrite: 'todo.write',
  Task: 'subagent.spawn',
  NotebookEdit: 'notebook.edit',
  ExitPlanMode: 'plan.exit',
}

const CODEX: Record<string, ActivityKind> = {
  Bash: 'shell.exec',
  Shell: 'shell.exec',
  apply_patch: 'file.write',
  ApplyPatch: 'file.write',
  Subagent: 'subagent.spawn',
  EditNotebook: 'notebook.edit',
  GenerateImage: 'image.generate',
  TodoWrite: 'todo.write',
}

const CURSOR: Record<string, ActivityKind> = {
  read_file: 'file.read',
  edit_file: 'file.write',
  run_terminal_cmd: 'shell.exec',
  grep_search: 'search.code',
  semantic_search: 'search.code',
  search_files: 'search.files',
  web_search: 'web.search',
  browser: 'web.fetch',
  image_generation: 'image.generate',
  // ask_question is intercepted upstream and emitted as agent.question.
  // fetch_rules is dropped upstream.
}

const OPENCODE: Record<string, ActivityKind> = {
  read: 'file.read',
  write: 'file.write',
  edit: 'file.write',
  bash: 'shell.exec',
  shell: 'shell.exec',
  grep: 'search.code',
  glob: 'search.files',
  webfetch: 'web.fetch',
  websearch: 'web.search',
  todo: 'todo.write',
}

const TABLES: Record<AgentName, Record<string, ActivityKind>> = {
  claude: CLAUDE,
  codex: CODEX,
  cursor: CURSOR,
  opencode: OPENCODE,
}

export function classifyTool(agent: AgentName, toolName: string | undefined | null): ActivityKind {
  if (!toolName) return 'tool.unknown'
  if (toolName.startsWith('mcp__')) return 'mcp.call'
  const table = TABLES[agent]
  return table?.[toolName] ?? 'tool.unknown'
}
