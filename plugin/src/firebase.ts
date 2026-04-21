import { request as httpRequest } from 'node:http'
import { request as httpsRequest } from 'node:https'
import { URL } from 'node:url'
import { bus } from './bus.js'
import { logger } from './logger.js'

const WORKING_HOOKS = new Set(['UserPromptSubmit', 'PreToolUse', 'PostToolUse', 'PreCompact'])
const IDLE_HOOKS = new Set(['Stop', 'SessionEnd'])

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

  bus.on('hook_event', (ev) => {
    if (ev.hook_type === 'SessionStart') {
      put('starting', true)
      put('lastMessage', null)
    }
    if (ev.hook_type === 'UserPromptSubmit') put('starting', false)
    if (WORKING_HOOKS.has(ev.hook_type)) put('working', true)
    if (IDLE_HOOKS.has(ev.hook_type)) {
      put('working', false)
      if (ev.hook_type === 'Stop') {
        const p = (ev.payload ?? {}) as { assistant_text?: unknown }
        if (Array.isArray(p.assistant_text)) {
          const blocks = p.assistant_text.filter((t): t is string => typeof t === 'string')
          const summary = blocks.length > 0 ? blocks[blocks.length - 1] : ''
          put('lastMessage', { summary, blocks })
        }
      }
    }
    if (ev.hook_type === 'PreToolUse') put('preToolUse', ev.payload)
    if (ev.hook_type === 'PostToolUse') put('postToolUse', ev.payload)
  })

  bus.on('permission_request', (p) => put('permissionRequest', p))
  bus.on('permission_resolved', () => put('permissionRequest', null))

  logger.info(`firebase sync enabled db=${baseUrl} id=${id}`)
}
