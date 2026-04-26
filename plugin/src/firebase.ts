import { request as httpRequest } from 'node:http'
import { request as httpsRequest } from 'node:https'
import { URL } from 'node:url'
import { bus } from './bus.js'
import { logger } from './logger.js'

// Firebase mirror. Drives a tiny doc that other clients (web UI,
// older ESP32 sketches) poll. Maps the abstract AgentEvent stream onto
// the legacy doc shape: { working, starting, lastMessage, preToolUse,
// postToolUse, permissionRequest }.

export function startFirebaseSync(): void {
  const base = process.env.BRIDGE_DATABASE_URL
  const id = process.env.BRIDGE_AGENT_ID
  if (!base || !id) return

  const baseUrl = base.replace(/\/+$/, '')
  const agentBase = `${baseUrl}/agents/${encodeURIComponent(id)}`

  function put(path: string, value: unknown): void {
    const url = new URL(`${agentBase}/${path}.json`)
    const body = JSON.stringify(value ?? null)
    const mod = url.protocol === 'https:' ? httpsRequest : httpRequest
    const req = mod(
      {
        hostname: url.hostname,
        port: url.port || (url.protocol === 'https:' ? 443 : 80),
        path: url.pathname + url.search,
        method: 'PUT',
        headers: {
          'Content-Type': 'application/json; charset=utf-8',
          'Content-Length': Buffer.byteLength(body),
        },
        timeout: 3000,
      },
      (res) => {
        if (res.statusCode && res.statusCode >= 400) {
          logger.warn(`firebase put path=${path} status=${res.statusCode}`)
        }
        res.resume()
      },
    )
    req.on('error', (err) => logger.warn(`firebase put path=${path} err=${err.message}`))
    req.on('timeout', () => { logger.warn(`firebase put path=${path} timeout`); req.destroy() })
    req.write(body)
    req.end()
  }

  put('lastAwake', Date.now())

  bus.on('agent_event', (env) => {
    const ev = env.event
    switch (ev.kind) {
      case 'session.started':
        put('starting', true)
        put('lastMessage', null)
        put('working', true)
        break
      case 'turn.started':
        put('starting', false)
        put('working', true)
        break
      case 'activity.started':
        put('working', true)
        put('preToolUse', { tool: ev.activity.tool, kind: ev.activity.kind, summary: ev.activity.summary })
        break
      case 'activity.finished':
      case 'activity.failed':
        put('postToolUse', { tool: ev.activity.tool, kind: ev.activity.kind, summary: ev.activity.summary })
        break
      case 'context.compacting':
        put('working', true)
        break
      case 'turn.ended': {
        put('working', false)
        const summary = ev.last_assistant_text ?? ''
        if (summary) put('lastMessage', { summary, blocks: [summary] })
        break
      }
      case 'session.ended':
        put('working', false)
        break
      case 'permission.requested':
        put('permissionRequest', {
          request_id: ev.request_id,
          activity: ev.activity ?? null,
          description: ev.description ?? '',
        })
        break
      case 'permission.resolved':
        put('permissionRequest', null)
        break
    }
  })

  logger.info(`firebase sync enabled db=${baseUrl} id=${id}`)
}
