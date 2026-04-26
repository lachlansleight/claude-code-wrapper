import type { AgentEvent, AgentName, ParserInput } from '../agent-event.js'

// Result of parsing a single native hook payload. The bridge wraps each
// emitted event in an AgentEventEnvelope before broadcasting.
export interface ParsedEvent {
  event: AgentEvent
  session_id?: string
  turn_id?: string
}

export interface Parser {
  name: AgentName
  parse(input: ParserInput): ParsedEvent[]
}
