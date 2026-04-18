import { createServer, type IncomingMessage, type ServerResponse, type Server as HttpServer } from 'node:http'
import { randomUUID } from 'node:crypto'
import { bus } from './bus.js'
import { state } from './state.js'
import { extractTokenFromHeaders, isValidToken } from './auth.js'
import { logger } from './logger.js'
import type { BridgeConfig } from './types.js'

const VERSION = '0.1.0'

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
    json(res, 200, { ok: true, version: VERSION, uptime_seconds: state.uptimeSeconds() })
    return
  }

  // All other endpoints require auth.
  if (!isValidToken(extractTokenFromHeaders(req), config.token)) {
    json(res, 401, { error: 'unauthorized' })
    return
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

  if (method === 'POST' && path === '/api/hook-event') {
    const body = (await readJsonBody(req)) as { hook_type?: unknown; payload?: unknown }
    if (typeof body.hook_type !== 'string' || body.hook_type.length === 0) {
      json(res, 400, { error: 'hook_type_required' })
      return
    }
    bus.emit('hook_event', {
      hook_type: body.hook_type,
      payload: body.payload ?? null,
      ts: Date.now(),
    })
    res.writeHead(204).end()
    return
  }

  if (method === 'POST' && path.startsWith('/api/permissions/')) {
    const request_id = decodeURIComponent(path.slice('/api/permissions/'.length))
    const body = (await readJsonBody(req)) as { behavior?: unknown }
    if (body.behavior !== 'allow' && body.behavior !== 'deny') {
      json(res, 400, { error: 'behavior_must_be_allow_or_deny' })
      return
    }
    const wasPending = state.getPendingPermission(request_id) !== undefined
    bus.emit('permission_verdict', { request_id, behavior: body.behavior, client_id: 'http' })
    json(res, 200, { request_id, behavior: body.behavior, applied: wasPending })
    return
  }

  json(res, 404, { error: 'not_found' })
}
