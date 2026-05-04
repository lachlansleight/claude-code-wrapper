import { WebSocketServer, WebSocket } from 'ws'
import type { Server as HttpServer, IncomingMessage } from 'node:http'
import { randomUUID } from 'node:crypto'
import { bus } from './bus.js'
import { state } from './state.js'
import { extractTokenFromHeaders, extractTokenFromUrl, isValidToken } from './auth.js'
import { logger } from './logger.js'
import type { BridgeConfig } from './types.js'
import type { AgentEvent, AgentName, AgentEventEnvelope } from './agent-event.js'

const VERSION = '0.3.0'
const WS_PATH = '/ws'

interface ClientInfo {
  client_id: string
}

export function attachWebSocketServer(config: BridgeConfig): (httpServer: HttpServer) => void {
  const wss = new WebSocketServer({ noServer: true })
  const clients = new Map<WebSocket, ClientInfo>()

  function broadcast(payload: unknown): void {
    const data = JSON.stringify(payload)
    for (const ws of clients.keys()) {
      if (ws.readyState === WebSocket.OPEN) {
        ws.send(data)
      }
    }
  }

  bus.on('inbound_message', (p) => broadcast({ type: 'inbound_message', ...p }))
  bus.on('outbound_reply', (p) => broadcast({ type: 'outbound_reply', ...p }))
  bus.on('permission_request', (p) => broadcast({ type: 'permission_request', ...p }))
  bus.on('permission_resolved', (p) => broadcast({ type: 'permission_resolved', ...p }))
  bus.on('session_event', (p) => broadcast({ type: 'session_event', ...p }))
  bus.on('agent_event', (p) => broadcast(p))
  bus.on('sessions_changed', (p) => broadcast({ type: 'active_sessions', ...p }))
  bus.on('raw_client_broadcast', (p) => broadcast(p))

  wss.on('connection', (ws, req: IncomingMessage) => {
    const client_id = `ws_${randomUUID()}`
    clients.set(ws, { client_id })
    logger.info(`ws connected client_id=${client_id} total=${clients.size}`)
    ws.send(JSON.stringify({ type: 'hello', client_id, server_version: VERSION }))
    ws.send(JSON.stringify({ type: 'active_sessions', session_ids: state.listActiveSessions() }))

    ws.on('message', (raw) => {
      let msg: unknown
      try {
        msg = JSON.parse(raw.toString())
      } catch {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_json' }))
        return
      }
      handleClientMessage(ws, client_id, msg)
    })

    ws.on('close', () => {
      clients.delete(ws)
      logger.info(`ws disconnected client_id=${client_id} total=${clients.size}`)
    })

    ws.on('error', (err) => {
      logger.warn(`ws error client_id=${client_id} ${String(err)}`)
    })
  })

  function handleClientMessage(ws: WebSocket, client_id: string, msg: unknown): void {
    if (typeof msg !== 'object' || msg === null || typeof (msg as { type?: unknown }).type !== 'string') {
      ws.send(JSON.stringify({ type: 'error', message: 'missing_type' }))
      return
    }
    const m = msg as { type: string; [k: string]: unknown }

    if (m.type === 'ping') {
      ws.send(JSON.stringify({ type: 'pong' }))
      return
    }

    if (m.type === 'request_sessions') {
      ws.send(JSON.stringify({ type: 'active_sessions', session_ids: state.listActiveSessions() }))
      return
    }

    if (m.type === 'send_message') {
      if (typeof m.content !== 'string' || m.content.length === 0) {
        ws.send(JSON.stringify({ type: 'error', message: 'content_required' }))
        return
      }
      const chat_id = typeof m.chat_id === 'string' && m.chat_id ? m.chat_id : `chat_${randomUUID()}`
      const meta = isPlainStringRecord(m.meta) ? sanitizeMetaKeys(m.meta) : undefined
      state.appendMessage(chat_id, {
        direction: 'inbound',
        content: m.content,
        ts: Date.now(),
        client_id,
      })
      bus.emit('inbound_message', { content: m.content, chat_id, client_id, meta })
      return
    }

    if (m.type === 'permission_verdict') {
      if (typeof m.request_id !== 'string' || (m.behavior !== 'allow' && m.behavior !== 'deny')) {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_verdict' }))
        return
      }
      bus.emit('permission_verdict', {
        request_id: m.request_id,
        behavior: m.behavior,
        client_id,
      })
      return
    }

    if (m.type === 'set_servo_position') {
      const pos = typeof m.position === 'number' ? m.position : Number.NaN
      if (!Number.isFinite(pos) || pos < -90 || pos > 90) {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_servo_position' }))
        return
      }
      const duration =
        typeof m.duration_ms === 'number' && Number.isFinite(m.duration_ms) && m.duration_ms > 0
          ? Math.min(60000, Math.floor(m.duration_ms))
          : 5000
      broadcast({
        type: 'set_servo_position',
        position: Math.round(pos),
        duration_ms: duration,
        by: client_id,
        ts: Date.now(),
      })
      return
    }

    if (m.type === 'config_change') {
      if (typeof m.display_mode !== 'string' || (m.display_mode !== 'face' && m.display_mode !== 'text' && m.display_mode !== 'debug')) {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_config_change' }))
        return
      }
      broadcast({ type: 'config_change', display_mode: m.display_mode, by: client_id, ts: Date.now() })
      return
    }

    if (m.type === 'setColor') {
      const key = typeof m.key === 'string' ? m.key : typeof m.name === 'string' ? m.name : ''
      const clampByte = (v: unknown): number => {
        const n = typeof v === 'number' ? v : Number.NaN
        if (!Number.isFinite(n)) return Number.NaN
        return Math.max(0, Math.min(255, Math.round(n)))
      }
      const colorObj = typeof m.color === 'object' && m.color !== null ? (m.color as Record<string, unknown>) : null
      const r = clampByte(colorObj?.r ?? m.r)
      const g = clampByte(colorObj?.g ?? m.g)
      const b = clampByte(colorObj?.b ?? m.b)
      if (!key || !Number.isFinite(r) || !Number.isFinite(g) || !Number.isFinite(b)) {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_set_color' }))
        return
      }
      broadcast({
        type: 'setColor',
        key,
        r,
        g,
        b,
        by: client_id,
        ts: Date.now(),
      })
      return
    }

    if (m.type === 'emit_agent_event') {
      if (typeof m.event !== 'object' || m.event === null || typeof (m.event as { kind?: unknown }).kind !== 'string') {
        ws.send(JSON.stringify({ type: 'error', message: 'invalid_emit_agent_event' }))
        return
      }
      const agent: AgentName =
        typeof m.agent === 'string' &&
        (m.agent === 'claude' ||
          m.agent === 'cursor' ||
          m.agent === 'codex' ||
          m.agent === 'opencode' ||
          m.agent === 'simulator')
          ? m.agent
          : 'simulator'
      const sessionId = typeof m.session_id === 'string' && m.session_id.length > 0 ? m.session_id : undefined
      const turnId = typeof m.turn_id === 'string' && m.turn_id.length > 0 ? m.turn_id : undefined
      const event = m.event as AgentEvent

      let listChanged = false
      if (sessionId) {
        if (event.kind === 'session.ended') {
          listChanged = state.endSession(sessionId)
        } else {
          listChanged = state.trackSession(sessionId)
        }
      }

      const envelope: AgentEventEnvelope = {
        type: 'agent_event',
        agent,
        ts: Date.now(),
        session_id: sessionId,
        turn_id: turnId,
        event,
      }
      bus.emit('agent_event', envelope)
      if (listChanged) {
        bus.emit('sessions_changed', { session_ids: state.listActiveSessions() })
      }
      return
    }

    ws.send(JSON.stringify({ type: 'error', message: `unknown_type:${m.type}` }))
  }

  return (httpServer: HttpServer) => {
    httpServer.on('upgrade', (req, socket, head) => {
      const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`)
      if (url.pathname !== WS_PATH) {
        socket.destroy()
        return
      }
      const token = extractTokenFromHeaders(req) ?? extractTokenFromUrl(req.url)
      if (!isValidToken(token, config.token)) {
        socket.write(
          'HTTP/1.1 401 Unauthorized\r\nConnection: close\r\nContent-Length: 0\r\n\r\n',
        )
        socket.destroy()
        logger.warn('ws upgrade rejected: bad token')
        return
      }
      wss.handleUpgrade(req, socket, head, (ws) => {
        wss.emit('connection', ws, req)
      })
    })
  }
}

function isPlainStringRecord(value: unknown): value is Record<string, string> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return false
  for (const v of Object.values(value)) {
    if (typeof v !== 'string') return false
  }
  return true
}

function sanitizeMetaKeys(meta: Record<string, string>): Record<string, string> {
  const out: Record<string, string> = {}
  for (const [k, v] of Object.entries(meta)) {
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(k)) out[k] = v
  }
  return out
}
