import type { ActivityRef } from './agent-event.js'

export type Direction = 'inbound' | 'outbound'

export interface ChatMessage {
  direction: Direction
  content: string
  ts: number
  client_id?: string
}

export interface PendingPermission {
  request_id: string
  activity?: ActivityRef
  description?: string
  opened_at: number
}

export interface InboundMessagePayload {
  content: string
  chat_id: string
  client_id: string
  meta?: Record<string, string>
}

export interface OutboundReplyPayload {
  chat_id: string
  text: string
}

export interface PermissionVerdictPayload {
  request_id: string
  behavior: 'allow' | 'deny'
  client_id: string
}

export interface BridgeConfig {
  token: string
  host: string
  port: number
  logFile?: string
}
