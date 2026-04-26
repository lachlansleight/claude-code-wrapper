import { readFileSync, existsSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

// Tiny zero-dep .env loader. Reads `src/.env` (or `dist/.env` after
// build) sitting next to this file. Existing process.env values win —
// real shell exports always override the file.

function parseLine(line: string): [string, string] | null {
  const trimmed = line.trim()
  if (!trimmed || trimmed.startsWith('#')) return null
  const eq = trimmed.indexOf('=')
  if (eq <= 0) return null
  const key = trimmed.slice(0, eq).trim()
  let value = trimmed.slice(eq + 1).trim()
  if ((value.startsWith('"') && value.endsWith('"')) || (value.startsWith("'") && value.endsWith("'"))) {
    value = value.slice(1, -1)
  }
  return [key, value]
}

export function loadDotenv(): string | null {
  const here = dirname(fileURLToPath(import.meta.url))
  const path = join(here, '.env')
  if (!existsSync(path)) return null
  try {
    const text = readFileSync(path, 'utf8')
    for (const raw of text.split(/\r?\n/)) {
      const kv = parseLine(raw)
      if (!kv) continue
      const [k, v] = kv
      if (!(k in process.env)) process.env[k] = v
    }
    return path
  } catch {
    return null
  }
}
