import type { Adapter } from './types.js'
import { claudeAdapter } from './claude.js'
import { codexAdapter } from './codex.js'
import { cursorAdapter } from './cursor.js'
import { opencodeAdapter } from './opencode.js'

const adapters: Record<string, Adapter> = {
  claude: claudeAdapter,
  codex: codexAdapter,
  cursor: cursorAdapter,
  opencode: opencodeAdapter,
}

export function getAdapter(name: string): Adapter | undefined {
  return adapters[name.toLowerCase()]
}

export function listAdapterNames(): string[] {
  return Object.keys(adapters)
}

export type { Adapter, AdapterInput, NormalizedHook, PersonalityState, ToolAccess } from './types.js'
export { toolAccess } from './types.js'
