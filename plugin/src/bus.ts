import { EventEmitter } from 'node:events'
import type {
  InboundMessagePayload,
  OutboundReplyPayload,
  PendingPermission,
  PermissionVerdictPayload,
} from './types.js'
import type { AgentEventEnvelope } from './agent-event.js'

export interface BusEvents {
  inbound_message: (payload: InboundMessagePayload) => void
  outbound_reply: (payload: OutboundReplyPayload) => void
  permission_request: (payload: PendingPermission) => void
  permission_verdict: (payload: PermissionVerdictPayload) => void
  permission_resolved: (payload: { request_id: string; behavior: 'allow' | 'deny'; by: 'remote' }) => void
  session_event: (payload: { event: 'connected' | 'disconnected' }) => void
  agent_event: (payload: AgentEventEnvelope) => void
  sessions_changed: (payload: { session_ids: string[] }) => void
}

class TypedBus extends EventEmitter {
  emit<E extends keyof BusEvents>(event: E, ...args: Parameters<BusEvents[E]>): boolean {
    return super.emit(event, ...args)
  }
  on<E extends keyof BusEvents>(event: E, listener: BusEvents[E]): this {
    return super.on(event, listener as (...a: unknown[]) => void)
  }
  off<E extends keyof BusEvents>(event: E, listener: BusEvents[E]): this {
    return super.off(event, listener as (...a: unknown[]) => void)
  }
}

export const bus = new TypedBus()
bus.setMaxListeners(50)
