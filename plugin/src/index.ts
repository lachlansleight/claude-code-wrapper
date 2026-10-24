#!/usr/bin/env node
// Agent-agnostic robot bridge. Standalone Node service — agent CLIs
// (Claude Code, Codex, Cursor, OpenCode) POST hook payloads to
// /hooks/<agent>; clients subscribe over WebSocket to live events and the
// derived personality state.

import net from 'node:net'
import path from 'node:path'
import { parseArgs } from 'node:util'
import { loadDotenv } from './dotenv.js'
import { configureLogger, logger } from './logger.js'
import { startHttpServer } from './http.js'
import { attachWebSocketServer } from './ws.js'
import { startFirebaseSync } from './firebase.js'
import type { BridgeConfig } from './types.js'

const VERSION = '0.3.0'

/** Exit status when the port is taken, so a supervising app can tell. */
const EXIT_PORT_IN_USE = 3

const USAGE = `usage: node dist/index.js [options]

  --host <address>   address to listen on (default: BRIDGE_HOST, else 127.0.0.1;
                     0.0.0.0 lets robots on your network connect)
  --port <n>         port (default: BRIDGE_PORT, else 8787)
  --data-dir <dir>   where turn logs go (default: the working directory)
  --turn-logs        write turn logs under --managed too
  --managed          run as an app's child process: ignore .env, no turn logs,
                     print one JSON line on stdout once listening, and exit
                     when stdin closes
  -h, --help         show this help

The token always comes from BRIDGE_TOKEN (environment or .env), never the
command line, so it doesn't show up in ps. Exits with status ${EXIT_PORT_IN_USE} if the port is
already in use.
`

interface CommandLine {
  host?: string
  port?: string
  dataDir?: string
  turnLogs: boolean
  managed: boolean
}

function fatal(message: string): never {
  process.stderr.write(`[bridge:fatal] ${message}\n`)
  process.exit(1)
}

function parseCommandLine(): CommandLine {
  try {
    const { values } = parseArgs({
      options: {
        host: { type: 'string' },
        port: { type: 'string' },
        'data-dir': { type: 'string' },
        'turn-logs': { type: 'boolean', default: false },
        managed: { type: 'boolean', default: false },
        help: { type: 'boolean', short: 'h', default: false },
      },
    })
    if (values.help) {
      process.stdout.write(USAGE)
      process.exit(0)
    }
    return {
      host: values.host,
      port: values.port,
      dataDir: values['data-dir'],
      turnLogs: values['turn-logs'] === true,
      managed: values.managed === true,
    }
  } catch (err) {
    process.stderr.write(`${(err as Error).message}\n\n${USAGE}`)
    process.exit(2)
  }
}

const args = parseCommandLine()
// A managed bridge gets everything from its parent; a stray .env beside the
// script must not change it.
const dotenvPath = args.managed ? null : loadDotenv()

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
  const portRaw = args.port ?? process.env.BRIDGE_PORT
  const port = portRaw ? Number(portRaw) : 8787
  if (!Number.isInteger(port) || port <= 0 || port > 65535) fatal(`port is invalid: ${portRaw}`)
  const host = args.host?.trim() || process.env.BRIDGE_HOST?.trim() || '127.0.0.1'
  const logFile = process.env.BRIDGE_LOG_FILE?.trim() || undefined
  const writeTurnLogs = args.turnLogs || !args.managed
  const turnLogDir = writeTurnLogs ? path.join(args.dataDir ?? '.', 'logs') : undefined
  return { token: token.trim(), host, port, logFile, turnLogDir }
}

/**
 * Whether something already accepts connections on this port over loopback.
 * macOS lets a 127.0.0.1 listener bind over another process's 0.0.0.0 one
 * and then takes all local traffic from it, so a successful bind doesn't
 * prove the port was free.
 */
function loopbackPortAnswers(config: BridgeConfig): Promise<boolean> {
  const loopback =
    config.host === '127.0.0.1' || config.host === '0.0.0.0' || config.host === 'localhost' ? '127.0.0.1'
    : config.host === '::1' || config.host === '::' ? '::1'
    : undefined
  if (!loopback) return Promise.resolve(false)
  return new Promise((resolve) => {
    const socket = net.connect({ host: loopback, port: config.port, timeout: 500 })
    const answer = (answers: boolean): void => {
      socket.destroy()
      resolve(answers)
    }
    socket.once('connect', () => answer(true))
    socket.once('timeout', () => answer(false))
    socket.once('error', () => answer(false))
  })
}

function exitPortInUse(config: BridgeConfig): never {
  logger.error(`port ${config.port} on ${config.host} is already in use`)
  process.exit(EXIT_PORT_IN_USE)
}

function exit(reason: string): never {
  logger.info(`${reason}, exiting`)
  process.exit(0)
}

async function main(): Promise<void> {
  const config = loadConfig()
  configureLogger({ logFile: config.logFile })
  logger.info(`agent-bridge v${VERSION} starting${args.managed ? ' (managed)' : ''}`)
  if (dotenvPath) logger.info(`loaded env from ${dotenvPath}`)
  logger.info(
    `host=${config.host} port=${config.port} log_file=${config.logFile ?? '(stderr only)'} ` +
      `turn_logs=${config.turnLogDir ?? '(off)'}`,
  )

  if (await loopbackPortAnswers(config)) exitPortInUse(config)
  const attachUpgrade = attachWebSocketServer(config)
  const server = startHttpServer(config, attachUpgrade)
  server.on('error', (err: NodeJS.ErrnoException) => {
    if (err.code === 'EADDRINUSE') exitPortInUse(config)
    fatal(`can't listen on ${config.host}:${config.port}: ${err.message}`)
  })
  if (args.managed) {
    server.on('listening', () => {
      process.stdout.write(
        JSON.stringify({ event: 'listening', host: config.host, port: config.port, version: VERSION }) + '\n',
      )
    })
    // The parent holds the other end of stdin. However it goes away (quit,
    // crash, force quit) the pipe closes, and we follow so the port is freed.
    process.stdin.on('end', () => exit('stdin closed'))
    process.stdin.resume()
  }
  startFirebaseSync()

  process.on('SIGINT', () => exit('SIGINT received'))
  process.on('SIGTERM', () => exit('SIGTERM received'))
}

main().catch((err) => {
  fatal(String(err?.stack ?? err))
})
