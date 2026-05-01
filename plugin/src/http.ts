import fs from 'node:fs'
import path from 'node:path'
import { createServer, type IncomingMessage, type ServerResponse, type Server as HttpServer } from 'node:http'
import { randomUUID } from 'node:crypto'
import { bus } from './bus.js'
import { state } from './state.js'
import { extractTokenFromHeaders, isValidToken } from './auth.js'
import { logger } from './logger.js'
import { getParser, listAdapterNames, ParsedEvent } from './adapters/index.js'
import type { AgentEvent, AgentEventEnvelope, AgentName } from './agent-event.js'
import type { BridgeConfig } from './types.js'
import { repairMojibakeDeep } from './mojibake.js'

const LOGS_DIR = 'logs'

interface TurnLog {
  path: string
  startedAt: number
  startStamp: string
  agentSlug: string
  eventCount: number
}

const activeTurnLogs = new Map<string, TurnLog>()

function safeAgentSlug(name: string): string {
  return name.replace(/[^A-Za-z0-9_-]/g, '_') || 'agent'
}

function startStamp(d: Date): string {
  const pad = (n: number): string => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}_${pad(d.getHours())}-${pad(d.getMinutes())}`
}

function turnLogPath(log: Pick<TurnLog, 'agentSlug' | 'startStamp' | 'eventCount'>, durationSeconds?: number): string {
  const suffix =
    durationSeconds === undefined
      ? ''
      : `_${log.eventCount}i-${durationSeconds}s`
  return path.join(LOGS_DIR, log.agentSlug, `${log.agentSlug}_${log.startStamp}${suffix}.log`)
}

function openTurnLog(agent: string): TurnLog {
  fs.mkdirSync(LOGS_DIR, { recursive: true })
  const startedAt = Date.now()
  const log: TurnLog = {
    path: '',
    startedAt,
    startStamp: startStamp(new Date(startedAt)),
    agentSlug: safeAgentSlug(agent),
    eventCount: 0,
  }
  const filePath = turnLogPath(log)
  log.path = filePath
  fs.mkdirSync(path.dirname(filePath), { recursive: true })
  fs.writeFileSync(filePath, '')
  logger.info(`turn log opened agent=${agent} path=${filePath}`)
  return log
}

function recordTurnHook(agent: string, hook_type: string, payload: unknown, parsed: ParsedEvent[]): void {
  const hasTurnStart = parsed.some((p) => p.event && (p.event as { kind?: string }).kind === 'turn.started')

  // Rotate to a fresh file on every turn.started so each turn is its own
  // self-contained log for replay. Anything fired outside a turn (SessionStart,
  // PreCompact, file watchers, etc.) is appended to whatever log is currently
  // open — or to a freshly opened one if this is the first hook ever seen.
  if (hasTurnStart || !activeTurnLogs.has(agent)) {
    activeTurnLogs.set(agent, openTurnLog(agent))
  }

  const log = activeTurnLogs.get(agent)!
  const ts = Date.now()
  const entry = {
    ts,
    delta_ms: ts - log.startedAt,
    agent,
    hook_type,
    payload,
    parsed,
  }
  try {
    fs.appendFileSync(log.path, JSON.stringify(entry) + '\n')
    log.eventCount += 1
    const durationSeconds = Math.round((ts - log.startedAt) / 1000)
    const nextPath = turnLogPath(log, durationSeconds)
    if (nextPath !== log.path) {
      fs.renameSync(log.path, nextPath)
      log.path = nextPath
    }
  } catch (err) {
    logger.warn(`turn log append failed path=${log.path} err=${String(err)}`)
  }
}

const VERSION = '0.3.0'

function json(res: ServerResponse, status: number, body: unknown): void {
  const payload = JSON.stringify(body)
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(payload),
  })
  res.end(payload)
}

async function readJsonBody(req: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = []
  let total = 0
  const MAX = 1_000_000
  for await (const chunk of req) {
    const buf = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)
    total += buf.length
    if (total > MAX) throw new Error('payload too large')
    chunks.push(buf)
  }
  if (total === 0) return {}
  return JSON.parse(Buffer.concat(chunks).toString('utf8'))
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

// Common path for both /hooks/:agent and the legacy /api/hook-event alias.
function processAgentHook(rawAgentName: string, agentName: AgentName, hook_type: string, parsedData: ParsedEvent[], payload: unknown): { ok: boolean; error?: string; listChanged: boolean } {
  // Session bookkeeping (works regardless of parser).
  const p = (payload ?? {}) as Record<string, unknown>
  const session_id =
    typeof p.session_id === 'string' ? p.session_id :
    typeof p.sessionId === 'string' ? p.sessionId :
    typeof p.sessionID === 'string' ? p.sessionID :
    typeof p.conversation_id === 'string' ? p.conversation_id : undefined

  let listChanged = false
  if (session_id) {
    const isEnd = /session[._]?end/i.test(hook_type) || /session\.deleted/i.test(hook_type)
    listChanged = isEnd ? state.endSession(session_id) : state.trackSession(session_id)
  }

  const now = Date.now()

  recordTurnHook(rawAgentName, hook_type, payload, parsedData)

  for (const item of parsedData) {
    const envelope: AgentEventEnvelope = {
      type: 'agent_event',
      agent: agentName,
      ts: now,
      session_id: item.session_id ?? session_id,
      turn_id: item.turn_id,
      event: item.event,
    }

    // Mirror permission events into state.
    if (item.event.kind === 'permission.requested') {
      state.addPendingPermission({
        request_id: item.event.request_id,
        activity: item.event.activity,
        description: item.event.description,
      })
      bus.emit('permission_request', {
        request_id: item.event.request_id,
        activity: item.event.activity,
        description: item.event.description,
        opened_at: now,
      })
    } else if (item.event.kind === 'permission.resolved') {
      state.resolvePendingPermission(item.event.request_id)
      bus.emit('permission_resolved', {
        request_id: item.event.request_id,
        behavior: item.event.decision,
        by: 'remote',
      })
    }

    bus.emit('agent_event', envelope)
  }

  return { ok: true, listChanged }
}

export function startHttpServer(
  config: BridgeConfig,
  attachUpgrade: (httpServer: HttpServer) => void,
): HttpServer {
  const server = createServer(async (req, res) => {
    try {
      await handle(req, res, config)
    } catch (err) {
      logger.error(`http handler error: ${String(err)}`)
      if (!res.headersSent) json(res, 500, { error: 'internal_error' })
    }
  })

  attachUpgrade(server)

  server.listen(config.port, config.host, () => {
    logger.info(`http listening on http://${config.host}:${config.port}`)
  })

  return server
}

async function handle(req: IncomingMessage, res: ServerResponse, config: BridgeConfig): Promise<void> {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`)
  const path = url.pathname
  const method = req.method ?? 'GET'

  if (method === 'GET' && path === '/api/health') {
    json(res, 200, {
      ok: true,
      version: VERSION,
      uptime_seconds: state.uptimeSeconds(),
      agents: listAdapterNames(),
    })
    return
  }

  // All other endpoints require auth.
  if (!isValidToken(extractTokenFromHeaders(req), config.token)) {
    json(res, 401, { error: 'unauthorized' })
    return
  }

  // /hooks/:agent — generic per-agent hook receiver.
  if (method === 'POST' && path.startsWith('/hooks/')) {
    const agent = decodeURIComponent(path.slice('/hooks/'.length))
    const body = repairMojibakeDeep(await readJsonBody(req)) as { hook_type?: unknown; payload?: unknown }
    if (typeof body.hook_type !== 'string' || body.hook_type.length === 0) {
      json(res, 400, { error: 'hook_type_required' })
      return
    }
    const parser = getParser(agent)
    const parsedEvents = parser?.parse({ hook_type: body.hook_type, payload: body.payload }) || [];
    const result = processAgentHook(agent, (parser?.name || agent) as AgentName, body.hook_type, parsedEvents, body.payload ?? null)
    if (!result.ok) {
      json(res, 400, { error: result.error })
      return
    }
    if (result.listChanged) {
      bus.emit('sessions_changed', { session_ids: state.listActiveSessions() })
    }
    res.writeHead(204).end()
    return
  }

  if (method === 'POST' && path.startsWith('/hooksRaw/')) {
    const agent = decodeURIComponent(path.slice('/hooksRaw/'.length))
    const body = repairMojibakeDeep(await readJsonBody(req)) as { hook_type?: unknown; events?: ParsedEvent[] }
    let listChanged = false
    for (const item of body.events || []) {
      const sid = item.session_id
      if (sid && item.event && typeof item.event === 'object' && 'kind' in item.event) {
        const k = (item.event as { kind: string }).kind
        if (k === 'session.ended') {
          if (state.endSession(sid)) listChanged = true
        } else {
          if (state.trackSession(sid)) listChanged = true
        }
      }

      const envelope: AgentEventEnvelope = {
        type: 'agent_event',
        agent: agent as AgentName,
        ts: Date.now(),
        session_id: item.session_id ?? 'debug',
        turn_id: item.turn_id,
        event: item.event,
      }

      if (item.event && typeof item.event === 'object' && 'kind' in item.event) {
        const ev = item.event as AgentEvent
        if (ev.kind === 'permission.requested') {
          state.addPendingPermission({
            request_id: ev.request_id,
            activity: ev.activity,
            description: ev.description,
          })
          bus.emit('permission_request', {
            request_id: ev.request_id,
            activity: ev.activity,
            description: ev.description,
            opened_at: Date.now(),
          })
        } else if (ev.kind === 'permission.resolved') {
          state.resolvePendingPermission(ev.request_id)
          bus.emit('permission_resolved', {
            request_id: ev.request_id,
            behavior: ev.decision,
            by: 'remote',
          })
        }
      }

      bus.emit('agent_event', envelope)
    }
    if (listChanged) {
      bus.emit('sessions_changed', { session_ids: state.listActiveSessions() })
    }
    res.writeHead(204).end()
    return
  }

  if (method === 'POST' && path === "/api/simulate-hook") {

  }

  if (method === 'POST' && path === '/api/messages') {
    const body = (await readJsonBody(req)) as { content?: unknown; chat_id?: unknown; meta?: unknown }
    if (typeof body.content !== 'string' || body.content.length === 0) {
      json(res, 400, { error: 'content_required' })
      return
    }
    const chat_id = typeof body.chat_id === 'string' && body.chat_id ? body.chat_id : `chat_${randomUUID()}`
    const meta =
      body.meta === undefined
        ? undefined
        : isPlainStringRecord(body.meta)
          ? sanitizeMetaKeys(body.meta)
          : undefined
    const client_id = `http_${randomUUID()}`
    state.appendMessage(chat_id, { direction: 'inbound', content: body.content, ts: Date.now(), client_id })
    bus.emit('inbound_message', { content: body.content, chat_id, client_id, meta })
    json(res, 200, { chat_id, status: 'forwarded' })
    return
  }

  if (method === 'GET' && path.startsWith('/api/messages/')) {
    const chat_id = decodeURIComponent(path.slice('/api/messages/'.length))
    const messages = state.getChat(chat_id) ?? []
    json(res, 200, { chat_id, messages })
    return
  }

  if (method === 'GET' && path === '/api/state') {
    json(res, 200, {
      chats: state.listChats(),
      pending_permissions: state.listPendingPermissions(),
      uptime_seconds: state.uptimeSeconds(),
    })
    return
  }

  if (method === 'GET' && path === '/api/permissions') {
    json(res, 200, state.listPendingPermissions())
    return
  }

  if (method === 'GET' && path === '/api/firebaseData') {
    const base = process.env.BRIDGE_DATABASE_URL?.replace(/\/+$/, '')
    const id = process.env.BRIDGE_AGENT_ID
    if (!base || !id) {
      json(res, 503, { error: 'firebase_not_configured' })
      return
    }
    try {
      const upstream = await fetch(`${base}/agents/${encodeURIComponent(id)}.json`)
      const text = await upstream.text()

      const data = JSON.parse(text)
      const trimData = {
        working: data.working,
        lastMessage: { summary: data.lastMessage?.summary },
        starting: data.starting || false,
      }
      const payload = JSON.stringify(trimData)
      res.writeHead(upstream.status, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(payload),
      })
      res.end(payload)
    } catch (err) {
      logger.warn(`firebase proxy err=${String(err)}`)
      json(res, 502, { error: 'upstream_error' })
    }
    return
  }

  if (method === 'GET' && path === '/api/sessions') {
    json(res, 200, { session_ids: state.listActiveSessions() })
    return
  }

  if (method === 'POST' && path.startsWith('/api/permissions/')) {
    const request_id = decodeURIComponent(path.slice('/api/permissions/'.length))
    const body = (await readJsonBody(req)) as { behavior?: unknown }
    if (body.behavior !== 'allow' && body.behavior !== 'deny') {
      json(res, 400, { error: 'behavior_must_be_allow_or_deny' })
      return
    }
    // Without an MCP channel back to Claude Code, verdicts can't actually
    // unblock the agent — they just resolve our local pending entry and
    // notify clients. Surface this honestly via `applied: false` whenever
    // we've stopped tracking the request.
    const wasPending = state.getPendingPermission(request_id) !== undefined
    if (wasPending) {
      state.resolvePendingPermission(request_id)
      bus.emit('permission_resolved', { request_id, behavior: body.behavior, by: 'remote' })
    }
    bus.emit('permission_verdict', { request_id, behavior: body.behavior, client_id: 'http' })
    json(res, 200, { request_id, behavior: body.behavior, applied: wasPending, note: 'verdict broadcast only; agent CLI still owns the prompt' })
    return
  }

  json(res, 404, { error: 'not_found' })
}
