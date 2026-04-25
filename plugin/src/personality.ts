// Personality state machine. TS port of robot_v2/Personality.cpp.
// Drives a high-level PersonalityState off NormalizedHook events. Emits
// transitions on the bus as `state_event`.

import { bus } from './bus.js'
import { logger } from './logger.js'
import type { NormalizedHook, PersonalityState } from './adapters/types.js'

interface StateConfig {
  name: PersonalityState
  min_ms: number   // transitions requested before this are queued
  max_ms: number   // auto-transition after this (0 = never)
  on_timeout: PersonalityState
}

const CONFIG: Record<PersonalityState, StateConfig> = {
  idle:     { name: 'idle',     min_ms: 0,    max_ms: 30 * 60 * 1000, on_timeout: 'sleep' },
  thinking: { name: 'thinking', min_ms: 0,    max_ms: 0,              on_timeout: 'idle' },
  reading:  { name: 'reading',  min_ms: 0,    max_ms: 0,              on_timeout: 'idle' },
  writing:  { name: 'writing',  min_ms: 0,    max_ms: 0,              on_timeout: 'idle' },
  finished: { name: 'finished', min_ms: 1500, max_ms: 1500,           on_timeout: 'excited' },
  excited:  { name: 'excited',  min_ms: 0,    max_ms: 10 * 1000,      on_timeout: 'ready' },
  ready:    { name: 'ready',    min_ms: 0,    max_ms: 60 * 1000,      on_timeout: 'idle' },
  waking:   { name: 'waking',   min_ms: 1000, max_ms: 1000,           on_timeout: 'thinking' },
  sleep:    { name: 'sleep',    min_ms: 0,    max_ms: 0,              on_timeout: 'idle' },
  blocked:  { name: 'blocked',  min_ms: 0,    max_ms: 0,              on_timeout: 'idle' },
}

const TOOL_LINGER_MS = 1000

let current: PersonalityState = 'sleep'
let queued: PersonalityState | null = null
let enteredAt = Date.now()
let toolLingerDeadline = 0
let postWakeTarget: PersonalityState = 'thinking'
let preBlockedState: PersonalityState = 'thinking'
let pendingPermission = false
let tickHandle: NodeJS.Timeout | undefined

function transitionTo(target: PersonalityState): void {
  if (target === current) {
    queued = null
    return
  }
  const prev = current
  logger.info(`personality: ${prev} -> ${target} (after ${Date.now() - enteredAt}ms)`)
  current = target
  enteredAt = Date.now()
  queued = null
  toolLingerDeadline = 0
  bus.emit('state_event', { state: current, prev, ts: enteredAt })
}

function request(target: PersonalityState): void {
  if (target === current) return
  const elapsed = Date.now() - enteredAt
  if (elapsed < CONFIG[current].min_ms) {
    // Protected window — queue. Last write wins.
    queued = target
    return
  }
  transitionTo(target)
}

function routeToActive(target: PersonalityState): void {
  if (current === 'sleep') {
    postWakeTarget = target
    request('waking')
  } else {
    request(target)
  }
}

function accessToState(access: 'read' | 'write' | 'unknown'): PersonalityState {
  return access === 'write' ? 'writing' : 'reading'
}

export function handleHook(hook: NormalizedHook): void {
  switch (hook.kind) {
    case 'session_start':
      // SLEEP → wake to READY; IDLE → READY; READY → EXCITED
      if (current === 'sleep') routeToActive('ready')
      else if (current === 'idle') request('ready')
      else if (current === 'ready') request('excited')
      return

    case 'session_end':
      // No explicit transition — let max_ms timeouts decay back to sleep.
      return

    case 'user_prompt':
      routeToActive('thinking')
      return

    case 'pre_tool':
      routeToActive(accessToState(hook.access))
      toolLingerDeadline = 0
      return

    case 'post_tool':
      if (current === 'reading' || current === 'writing') {
        toolLingerDeadline = Date.now() + TOOL_LINGER_MS
      }
      return

    case 'permission_request':
      pendingPermission = true
      return

    case 'permission_resolved':
      pendingPermission = false
      return

    case 'stop':
      request('finished')
      return

    case 'notification':
    case 'unknown':
      return
  }
}

function tick(): void {
  const now = Date.now()
  const elapsed = now - enteredAt
  const cfg = CONFIG[current]

  // Permission gating, polled.
  if (pendingPermission && current !== 'blocked' && current !== 'waking') {
    preBlockedState = current
    transitionTo('blocked')
    return
  }
  if (!pendingPermission && current === 'blocked') {
    transitionTo(preBlockedState)
    return
  }

  // Queued pre-empt becomes eligible once min_ms has passed.
  if (queued !== null && elapsed >= cfg.min_ms) {
    transitionTo(queued)
    return
  }

  // Tool-linger fallback.
  if (toolLingerDeadline !== 0 && now >= toolLingerDeadline &&
      (current === 'reading' || current === 'writing')) {
    toolLingerDeadline = 0
    transitionTo('thinking')
    return
  }

  // max_ms timeout.
  if (cfg.max_ms > 0 && elapsed >= cfg.max_ms) {
    const next = current === 'waking' ? postWakeTarget : cfg.on_timeout
    transitionTo(next)
  }
}

export function startPersonality(): void {
  current = 'sleep'
  queued = null
  enteredAt = Date.now()
  toolLingerDeadline = 0
  pendingPermission = false
  if (tickHandle) clearInterval(tickHandle)
  tickHandle = setInterval(tick, 100)
  // Don't keep the process alive for this alone.
  if (typeof tickHandle.unref === 'function') tickHandle.unref()
  logger.info(`personality: start state=${current}`)
}

export function currentState(): PersonalityState {
  return current
}

export function timeInStateMs(): number {
  return Date.now() - enteredAt
}
