import type { IncomingMessage } from 'node:http'

export function extractTokenFromHeaders(req: IncomingMessage): string | null {
  const auth = req.headers['authorization']
  if (typeof auth === 'string') {
    const match = /^Bearer\s+(.+)$/i.exec(auth.trim())
    if (match) return match[1].trim()
  }
  const xToken = req.headers['x-bridge-token']
  if (typeof xToken === 'string') return xToken.trim()
  return null
}

export function extractTokenFromUrl(rawUrl: string | undefined): string | null {
  if (!rawUrl) return null
  try {
    const url = new URL(rawUrl, 'http://localhost')
    return url.searchParams.get('token')
  } catch {
    return null
  }
}

export function isValidToken(provided: string | null, expected: string): boolean {
  if (!provided) return false
  if (provided.length !== expected.length) return false
  let mismatch = 0
  for (let i = 0; i < expected.length; i++) {
    mismatch |= provided.charCodeAt(i) ^ expected.charCodeAt(i)
  }
  return mismatch === 0
}

const LOOPBACK_ADDRESSES = new Set(['127.0.0.1', '::1', '::ffff:127.0.0.1'])

export function isLoopback(req: IncomingMessage): boolean {
  return LOOPBACK_ADDRESSES.has(req.socket.remoteAddress ?? '')
}

/**
 * Hook POSTs from this machine are accepted without a token. Claude Code's
 * HTTP hooks can only put allow-listed environment variables in headers, so
 * requiring a token would mean shell setup for every user (the sultana
 * plugin sends none).
 *
 * Web pages open in a local browser must not be able to forge hooks, so the
 * request must be JSON and carry no Origin header: browsers always send
 * Origin on cross-origin POSTs, and can't send JSON cross-origin without a
 * CORS preflight. Requests from other machines still need the token.
 */
export function isTrustedLocalHook(req: IncomingMessage): boolean {
  if (!isLoopback(req)) return false
  if (req.headers.origin !== undefined) return false
  const contentType = String(req.headers['content-type'] ?? '').split(';')[0].trim().toLowerCase()
  return contentType === 'application/json'
}
