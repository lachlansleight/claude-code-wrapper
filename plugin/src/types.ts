export type Direction = 'inbound' | 'outbound'

export interface ChatMessage {
  direction: Direction
  content: string
  ts: number
  client_id?: string
}

export interface PendingPermission {
  request_id: string
  tool_name: string
  description: string
  input_preview: string
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
