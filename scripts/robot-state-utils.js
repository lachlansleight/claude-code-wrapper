import { randomUUID } from 'node:crypto'

const BRIDGE_URL = process.env.BRIDGE_URL ?? 'http://127.0.0.1:8787'
const BRIDGE_TOKEN = process.env.BRIDGE_TOKEN ?? 'e0112a5b1f05'
const SESSION_ID = process.env.SESSION_ID ?? `robot_sim_${Math.random().toString(36).slice(2, 10)}`
const TURN_ID = process.env.TURN_ID ?? `turn_${Math.random().toString(36).slice(2, 10)}`

if (!BRIDGE_TOKEN) {
  console.error('BRIDGE_TOKEN env var is required')
  process.exit(1)
}

export const context = {
  bridgeUrl: BRIDGE_URL,
  bridgeToken: BRIDGE_TOKEN,
  sessionId: SESSION_ID,
  turnId: TURN_ID,
}

/** POST canonical ParsedEvent[] to the bridge (matches agent-hooks.log PARSED EVENTS shape). */
export async function postEvents(events, label = '') {
  const res = await fetch(`${context.bridgeUrl}/hooksRaw/simulator`, {
    method: 'POST',
    headers: {
      Authorization: `Bearer ${context.bridgeToken}`,
      'Content-Type': 'application/json',
    },
    body: JSON.stringify({ events }),
  })
  if (!res.ok) {
    const text = await res.text().catch(() => '')
    throw new Error(`hooksRaw/simulator failed: ${res.status} ${text}`)
  }
  const kinds = events.map((e) => e?.event?.kind).filter(Boolean)
  console.log(`-> ${label || 'events'} [${kinds.join(', ')}]`)
}

export function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

export function randomInt(min, max) {
  return Math.floor(Math.random() * (max - min + 1)) + min
}

export function random(min, max) {
  return Math.random() * (max - min) + min
}

export function pick(arr) {
  return arr[Math.floor(Math.random() * arr.length)]
}

export function summaryFromPath(filePath) {
  const norm = String(filePath).replace(/\\/g, '/')
  const seg = norm.split('/')
  return seg[seg.length - 1] || norm
}

export function truncateSummary(s, max = 80) {
  if (s.length <= max) return s
  return `${s.slice(0, max - 1)}…`
}

export function newActivityId() {
  return `sim_${randomUUID()}`
}

/** Same shape as Claude logs: file.read + tool "Read". */
export function readActivityPair(filePath) {
  const id = newActivityId()
  const summary = summaryFromPath(filePath)
  const activity = { id, kind: 'file.read', tool: 'Read', summary }
  return [
    {
      event: { kind: 'activity.started', activity: { ...activity } },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
    {
      event: { kind: 'activity.finished', activity: { ...activity } },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
  ]
}

/** Same shape as Claude logs: file.write + tool "Edit". */
export function writeActivityPair(filePath) {
  const id = newActivityId()
  const summary = summaryFromPath(filePath)
  const activity = { id, kind: 'file.write', tool: 'Edit', summary }
  return [
    {
      event: { kind: 'activity.started', activity: { ...activity } },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
    {
      event: { kind: 'activity.finished', activity: { ...activity } },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
  ]
}

/** Build a stable `activity` ref; emit started now and finished later with the same `id`. */
export function buildShellActivity(command) {
  const id = newActivityId()
  const summary = truncateSummary(command, 80)
  return { id, kind: 'shell.exec', tool: 'Bash', summary }
}

/** Same shape as Claude logs: shell.exec + tool "Bash". */
export function shellActivityPair(command, durationMs) {
  const activity = buildShellActivity(command)
  return [
    {
      event: { kind: 'activity.started', activity: { ...activity } },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
    {
      event: {
        kind: 'activity.finished',
        activity: { ...activity },
        duration_ms: durationMs,
      },
      session_id: context.sessionId,
      turn_id: context.turnId,
    },
  ]
}

export function shellStartedItem(activity) {
  return {
    event: { kind: 'activity.started', activity: { ...activity } },
    session_id: context.sessionId,
    turn_id: context.turnId,
  }
}

export function shellFinishedItem(activity, durationMs) {
  return {
    event: {
      kind: 'activity.finished',
      activity: { ...activity },
      duration_ms: durationMs,
    },
    session_id: context.sessionId,
    turn_id: context.turnId,
  }
}

export async function doTimedToolCall(toolName, durationSeconds) {
  const activity = buildShellActivity(toolName)
  await postEvents([shellStartedItem(activity)]);
  await sleep(durationSeconds * 1000)
  await postEvents([shellFinishedItem(activity, durationSeconds * 1000)]);
}

export async function startSessionAndTurn(prompt = 'Robot state simulation') {
  await postEvents(
    [
      {
        event: { kind: 'session.started', cause: 'startup' },
        session_id: context.sessionId,
      },
      {
        event: { kind: 'turn.started', prompt },
        session_id: context.sessionId,
        turn_id: context.turnId,
      },
      {
        event: { kind: 'message.user', text: prompt },
        session_id: context.sessionId,
        turn_id: context.turnId,
      },
    ],
    'session.started + turn.started',
  )
}

export async function endTurn() {
  await postEvents(
    [
      {
        event: { kind: 'turn.ended', cause: 'completed' },
        session_id: context.sessionId,
        turn_id: context.turnId,
      },
    ],
    'turn.ended',
  )
}

export async function endSession() {
  await postEvents(
    [
      {
        event: { kind: 'session.ended', cause: 'unknown' },
        session_id: context.sessionId,
      },
    ],
    'session.ended',
  )
}
