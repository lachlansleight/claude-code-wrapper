import type { Parser } from './types.js'
import type { AgentName } from '../agent-event.js'
import { claudeParser } from './claude.js'
import { codexParser } from './codex.js'
import { cursorParser } from './cursor.js'
import { opencodeParser } from './opencode.js'

const simulatorParser: Parser = {
  name: 'simulator',
  parse() {
    return []
  },
}

const parsers: Record<AgentName, Parser> = {
  claude: claudeParser,
  codex: codexParser,
  cursor: cursorParser,
  opencode: opencodeParser,
  simulator: simulatorParser,
}

export function getParser(name: string): Parser | undefined {
  return parsers[name.toLowerCase() as AgentName]
}

export function listAdapterNames(): string[] {
  return Object.keys(parsers)
}

export type { Parser, ParsedEvent } from './types.js'
