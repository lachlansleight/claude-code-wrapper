// Build the short human-friendly summary string for an ActivityRef.
// Ported from robot_v2/ToolFormat.cpp::detail(). Same logic across all
// adapters — vendor-specific input keys feed in but the output shape is
// uniform.

const MAX_LEN = 80

function clip(s: string): string {
  if (s.length <= MAX_LEN) return s
  return s.slice(0, MAX_LEN - 1) + '…'
}

function basename(path: string): string {
  if (!path) return ''
  // Handle both posix and windows separators.
  const cleaned = path.replace(/[\\]+/g, '/').replace(/\/+$/, '')
  const i = cleaned.lastIndexOf('/')
  return i >= 0 ? cleaned.slice(i + 1) : cleaned
}

function host(url: string): string {
  if (!url) return ''
  try {
    return new URL(url).host
  } catch {
    return clip(url)
  }
}

function asString(v: unknown): string {
  return typeof v === 'string' ? v : ''
}

export function summarize(toolName: string, toolInput: unknown): string {
  const input = (toolInput ?? {}) as Record<string, unknown>

  switch (toolName) {
    case 'Read':
    case 'Edit':
    case 'Write':
    case 'MultiEdit':
    case 'NotebookEdit':
      return clip(basename(asString(input.file_path) || asString(input.notebook_path) || asString(input.path)))

    // Cursor / OpenCode
    case 'read_file':
    case 'edit_file':
    case 'read':
    case 'write':
    case 'edit':
      return clip(basename(asString(input.file_path) || asString(input.target_file) || asString(input.path)))

    case 'Bash':
    case 'Shell':
      return clip(asString(input.command))

    case 'run_terminal_cmd':
      return clip(asString(input.command) || asString(input.cmd))

    case 'bash':
    case 'shell':
      return clip(asString(input.command))

    case 'BashOutput':
      return clip(asString(input.bash_id) || asString(input.shell_id))
    case 'KillShell':
    case 'KillBash':
      return clip(asString(input.shell_id) || asString(input.bash_id))

    case 'Grep':
      return clip(asString(input.pattern))
    case 'Glob':
      return clip(asString(input.pattern))
    case 'grep_search':
    case 'semantic_search':
      return clip(asString(input.query) || asString(input.pattern))
    case 'search_files':
      return clip(asString(input.query) || asString(input.pattern))
    case 'grep':
      return clip(asString(input.pattern) || asString(input.query))
    case 'glob':
      return clip(asString(input.pattern))

    case 'WebFetch':
      return host(asString(input.url))
    case 'browser':
      return host(asString(input.url))
    case 'webfetch':
      return host(asString(input.url))

    case 'WebSearch':
    case 'web_search':
    case 'websearch':
      return clip(asString(input.query))

    case 'Task':
    case 'Subagent':
      return clip(asString(input.subagent_type) || asString(input.description) || asString(input.prompt))

    case 'TodoWrite':
    case 'todo': {
      const todos = input.todos
      if (Array.isArray(todos)) return `${todos.length} items`
      return ''
    }

    case 'apply_patch':
    case 'ApplyPatch':
      // Best-effort: expose path or operation summary if present.
      return clip(asString(input.path) || asString(input.file) || '')

    case 'image_generation':
    case 'GenerateImage':
      return clip(asString(input.prompt))

    case 'ExitPlanMode':
      return ''
  }

  if (toolName.startsWith('mcp__')) {
    return toolName.slice('mcp__'.length)
  }
  return ''
}
