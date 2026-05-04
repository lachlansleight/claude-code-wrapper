# Personality State Machine

`Personality` is the single answer to "what is the robot doing right
now?". It reduces the raw `AgentEvent` stream into one of 13 high-level
states, which the Face and Motion renderers dispatch on each frame.

Implementation: `robot_v2/Personality.cpp`. All timing knobs live in the
`kStates[]` table at the top of that file; tweak there and reflash.

## States

| State              | Meaning                                         | Default decay                       |
|--------------------|-------------------------------------------------|-------------------------------------|
| `IDLE`             | Boot / long inactivity — patient, bored-ish    | → `SLEEP` after 30 min              |
| `THINKING`         | Working, no active tool                         | (no auto-decay)                     |
| `READING`          | Read-like activity active                       | tool-linger → `THINKING`            |
| `WRITING`          | Write-like activity active                      | tool-linger → `THINKING`            |
| `EXECUTING`        | Shell command running                           | → `EXECUTING_LONG` after 5 s        |
| `EXECUTING_LONG`   | Shell still running                             | → `BLOCKED` after 30 s              |
| `FINISHED`         | Turn just ended — the celebratory beat         | → `EXCITED` after 1.5 s (protected) |
| `EXCITED`          | Post-finished energy, big smile                 | → `READY` after 10 s                |
| `READY`            | "Your turn" — attentive, waiting for you       | → `IDLE` after 60 s                 |
| `WAKING`           | "Oh! they're back" startle on session start    | → `sPostWakeTarget` after 1 s (protected) |
| `SLEEP`            | They're not coming back right now              | (no auto-decay)                     |
| `BLOCKED`          | Awaiting permission verdict — sad              | (held while `pending_permission`)   |
| `WANTS_ATTENTION`  | 1 s startle to flag the user                   | → `sPreAttentionState` after 1 s (protected) |

Boot state is `SLEEP`. The state-machine transitions log to serial as
`personality: <from> -> <to> (after Nms)`.

## Event → state transitions

`Personality::onAgentEvent(e)` dispatches on `e.kind`:

| `event.kind`         | Action                                                                    |
|----------------------|---------------------------------------------------------------------------|
| `session.started`    | `routeToActive(EXCITED)` — fresh session is exciting                      |
| `session.ended`      | `request(SLEEP)`                                                          |
| `turn.started`       | `routeToActive(THINKING)`                                                 |
| `activity.started`   | `routeToActive(READING / WRITING / EXECUTING)` based on `activity.kind`   |
| `activity.finished` / `activity.failed` | arm a 1 s tool-linger if currently in a tool state    |
| `turn.ended`         | `request(FINISHED)`                                                       |
| `notification`       | if text starts with "Claude needs " → enter `WANTS_ATTENTION`             |

Activity-to-state mapping (`activityToState`):

- `activity.kind == "shell.exec"` → `EXECUTING`
- `AgentEvents::classifyActivity()` returns `ACTIVITY_WRITE` → `WRITING`
- otherwise → `READING`

`classifyActivity` lives in `AgentEvents.cpp` and treats `file.write`,
`file.delete`, `notebook.edit`, and shell commands matching `shellLikelyWrites`
(redirections, `tee`, `rm`, `mv`, `cp`, package installs, …) as writes.

## Wake-up routing

`routeToActive(target)` is the entry path for any event that means
"the agent is doing something":

```
if current == SLEEP:    sPostWakeTarget = target;  request(WAKING)
else:                   request(target)
```

`WAKING` plays its 1 s protected animation, then transitions to whatever
was stashed in `sPostWakeTarget` instead of its static `on_timeout`.

## Tool-linger

When `READING` / `WRITING` / `EXECUTING` / `EXECUTING_LONG` is current
and an `activity.finished` (or `.failed`) arrives, a `kToolLingerMs`
(1000 ms) deadline is set. Each `tick()`:

- if a matching `activity.started` arrives first → re-enter same tool
  state, deadline reset (so bursts of same-type calls keep the state stable)
- if a different-type `activity.started` arrives → pre-empt to that
  state immediately (the agent has moved on)
- if the deadline passes with neither → fall back to `THINKING`

This is what makes "READING for ten Reads in a row" feel stable rather
than flapping `READING ↔ THINKING` between each tool call.

## Permission gating (polled)

`Personality::tick()` polls `AgentEvents::state().pending_permission`:

- pending and not currently `BLOCKED`/`WAKING` → stash current as
  `sPreBlockedState`, transition to `BLOCKED`, set
  `sBlockedByPermission = true`.
- pending clears and `sBlockedByPermission == true` → restore
  `sPreBlockedState`. (The agent will usually fire its own follow-up
  hook that overrides this immediately anyway.)

The `pending_permission` field is cleared on `turn.started` as a
recovery path, since the bridge can't reliably relay
`permission.resolved` for every agent (see
[../AGENT_TO_ROBOT_PIPELINE.md](../AGENT_TO_ROBOT_PIPELINE.md)
"Permission verdicts: the asterisk").

## Protected states (`min_ms`)

`FINISHED`, `WAKING`, and `WANTS_ATTENTION` have non-zero `min_ms`
windows. While inside one of those windows:

- `request(target)` does **not** transition immediately — it stashes
  `target` in `sQueued` (last write wins).
- `tick()` fires the queued transition once `elapsed >= min_ms`.

This lets short, character-defining animations always play in full
without a fast-arriving event cutting them short.

## State machine sketch

```
SLEEP  ──session.started──►  WAKING  ──(1s)──►  EXCITED  ──(10s)──►  READY  ──(60s)──►  IDLE  ──(30min)──►  SLEEP
                                                         ▲                                      │
              ┌──turn.started/activity──────────────────►┘                                      │
              │                                                                                 │
              ▼                                                                                 │
         THINKING ◄──(linger)──── READING / WRITING                                             │
              │                                                                                 │
              │                   EXECUTING ──(5s)──► EXECUTING_LONG ──(30s)──► BLOCKED         │
              │                                                                                 │
              ▼                                                                                 │
          turn.ended ──► FINISHED ──(1.5s)──► EXCITED ──────────────────────────────────────────┘

  Permission pending (polled):                  current ──► BLOCKED ──► (resume on resolve)
  "Claude needs ..." Notification:              current ──► WANTS_ATTENTION ──(1s)──► current
```

## Public API

```cpp
namespace Personality {
  void begin();
  void tick();

  State        current();
  uint32_t     enteredAtMs();
  uint32_t     timeInStateMs();
  const char*  stateName(State s);

  // For diagnostics / testing — goes through min-window queue logic.
  void request(State target);
}
```

## Adding a state

1. Add the enum value (before `kStateCount`) in `Personality.h`.
2. Add a row to `kStates[]` in `Personality.cpp`. The `static_assert` in
   `MotionBehaviors.cpp` will fail compilation until you also add a row
   to `kMotion[]` in the same order.
3. Add a `kBaseTargets` row in `FrameController.cpp` (also enum-indexed).
4. Add a `Settings::NamedColor` if the state should drive the mood ring,
   and extend `moodColorForState` in `FrameController.cpp`.
5. Add the event-handler clause in `Personality::onAgentEvent` that
   transitions into the new state.

The compile-time `static_assert` checks save you from mismatched table
sizes; the rest the compiler can't help with — read carefully.
