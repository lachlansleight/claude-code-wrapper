#!/usr/bin/env node
// CRITICAL: Stdio is the MCP transport. Anything written to stdout that isn't
// an MCP JSON frame will disconnect the channel. Redirect stray writes to
// stderr BEFORE importing anything that might log.
import { appendFileSync } from 'node:fs'

const _origStdoutWrite = process.stdout.write.bind(process.stdout)
const _origStderrWrite = process.stderr.write.bind(process.stderr)

// Optional: mirror every outgoing MCP frame to the log file for debugging.
// Activated by BRIDGE_DEBUG_FRAMES=1.
if (process.env.BRIDGE_DEBUG_FRAMES === '1' && process.env.BRIDGE_LOG_FILE) {
  const debugFile = process.env.BRIDGE_LOG_FILE
  process.stdout.write = ((chunk: unknown, ...rest: unknown[]): boolean => {
    try {
      const text = typeof chunk === 'string' ? chunk : Buffer.isBuffer(chunk) ? chunk.toString('utf8') : String(chunk)
      appendFileSync(debugFile, `[${new Date().toISOString()}] [bridge:stdout-frame] ${text}${text.endsWith('\n') ? '' : '\n'}`)
    } catch {}
    return _origStdoutWrite(chunk as Parameters<typeof _origStdoutWrite>[0], ...(rest as []))
  }) as typeof process.stdout.write
}

console.log = (...args: unknown[]) => {
  _origStderrWrite('[bridge:stdout-redirect] ' + args.map(String).join(' ') + '\n')
}
console.info = console.log
console.debug = console.log

import { configureLogger, logger } from './logger.js'
import { startMcpServer } from './mcp.js'
import { startHttpServer } from './http.js'
import { attachWebSocketServer } from './ws.js'
import type { BridgeConfig } from './types.js'

const VERSION = '0.1.0'

function loadConfig(): BridgeConfig {
  const token = process.env.BRIDGE_TOKEN
  if (!token || token.trim().length === 0) {
    process.stderr.write(
      '\n[bridge:fatal] BRIDGE_TOKEN is not set.\n' +
        '  Set it to a strong random string before launching the bridge, e.g.\n' +
        '    export BRIDGE_TOKEN="$(openssl rand -hex 32)"\n' +
        '  Or set it in your .mcp.json env block. The bridge will not start without it.\n\n',
    )
    process.exit(1)
  }
  const portRaw = process.env.BRIDGE_PORT
  const port = portRaw ? Number.parseInt(portRaw, 10) : 8787
  if (!Number.isFinite(port) || port <= 0 || port > 65535) {
    process.stderr.write(`[bridge:fatal] BRIDGE_PORT is invalid: ${portRaw}\n`)
    process.exit(1)
  }
  const host = process.env.BRIDGE_HOST?.trim() || '127.0.0.1'
  const logFile = process.env.BRIDGE_LOG_FILE?.trim() || undefined
  return { token: token.trim(), host, port, logFile }
}

async function main(): Promise<void> {
  const config = loadConfig()
  configureLogger({ logFile: config.logFile })
  logger.info(`claude-code-bridge v${VERSION} starting`)
  logger.info(`host=${config.host} port=${config.port} log_file=${config.logFile ?? '(stderr only)'}`)

  const attachUpgrade = attachWebSocketServer(config)
  startHttpServer(config, attachUpgrade)
  await startMcpServer(VERSION)

  process.on('SIGINT', () => {
    logger.info('SIGINT received, exiting')
    process.exit(0)
  })
  process.on('SIGTERM', () => {
    logger.info('SIGTERM received, exiting')
    process.exit(0)
  })
}

main().catch((err) => {
  process.stderr.write(`[bridge:fatal] ${String(err?.stack ?? err)}\n`)
  process.exit(1)
})
