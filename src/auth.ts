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
