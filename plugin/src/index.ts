#!/usr/bin/env node
// Agent-agnostic robot bridge. Standalone Node service. No MCP transport —
// agent CLIs (Claude Code, Codex, Cursor, OpenCode) POST hook payloads to
// /hooks/<agent>; clients subscribe over WebSocket to live events and the
// derived personality state.

import { configureLogger, logger } from './logger.js'
import { startHttpServer } from './http.js'
import { attachWebSocketServer } from './ws.js'
import { startFirebaseSync } from './firebase.js'
import { startPersonality } from './personality.js'
import type { BridgeConfig } from './types.js'

const VERSION = '0.2.0'

function loadConfig(): BridgeConfig {
  const token = process.env.BRIDGE_TOKEN
  if (!token || token.trim().length === 0) {
    process.stderr.write(
      '\n[bridge:fatal] BRIDGE_TOKEN is not set.\n' +
        '  Set it to a strong random string before launching the bridge, e.g.\n' +
        '    export BRIDGE_TOKEN="$(openssl rand -hex 32)"\n' +
        '  The bridge will not start without it.\n\n',
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
  logger.info(`agent-bridge v${VERSION} starting`)
  logger.info(`host=${config.host} port=${config.port} log_file=${config.logFile ?? '(stderr only)'}`)

  startPersonality()
  const attachUpgrade = attachWebSocketServer(config)
  startHttpServer(config, attachUpgrade)
  startFirebaseSync()

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
