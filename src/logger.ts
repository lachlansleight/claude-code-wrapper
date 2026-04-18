import { appendFileSync } from 'node:fs'

let logFile: string | undefined

export function configureLogger(opts: { logFile?: string }): void {
  logFile = opts.logFile
}

function write(level: 'info' | 'warn' | 'error', msg: string): void {
  const line = `[${new Date().toISOString()}] [bridge:${level}] ${msg}`
  process.stderr.write(line + '\n')
  if (logFile) {
    try {
      appendFileSync(logFile, line + '\n')
    } catch {
      // never throw from the logger; failing to mirror to file is non-fatal
    }
  }
}

export const logger = {
  info: (msg: string) => write('info', msg),
  warn: (msg: string) => write('warn', msg),
  error: (msg: string) => write('error', msg),
}
