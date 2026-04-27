# Personality + Face Plan

Design notes for the "once the display is working" phase. The goal is a
cartoony procedural face (two eyes + mouth) that reacts to Claude Code
activity, with a single servo waggling both arms in sympathy.

Style: **monochrome white-on-black, cute and elegant, face only** — no
status icons. Connection state will be encoded in the face itself later
if needed.

Animation scope for v1: **static face frames per state.** Tweening,
blinks, breath, saccades — all deferred until after the state machine is
proven. Frames snap on transition; we'll refine once all states work.

## Architecture

Three layers, each with one job:

```
  BridgeClient / ClaudeEvents   →   raw events, polled state
              │
              ▼
     Personality (new)          →   high-level state machine
              │
     ┌────────┴────────┐
     ▼                 ▼
   Face (new)       Motion behaviours
   (Display.cpp)    (replaces AmbientMotion + AttractScheduler)
```

- **Personality** owns the state machine. Reads `ClaudeEvents::state()`
  and event callbacks, exposes `Personality::current()` + `entered_at_ms`.
  Nothing else.
- **Face** reads personality state each frame, renders the static frame
  for that state into the existing sprite framebuffer.
- **Motion behaviours** subscribe to the same state, issue non-blocking
  keyframe jogs to `Motion::playJog`. Collapses today's `AmbientMotion`
  + `AttractScheduler` into one state-driven module. The single servo
  drives the arm-waggle mechanism; future motors will read from the
  same state.

## States

| State      | Meaning                                     | Minimum time | Exit                             |
|------------|---------------------------------------------|--------------|----------------------------------|
| `idle`     | Boot / long inactivity — patient, bored-ish | —            | state machine events; timeout → `sleep` after 30 min |
| `thinking` | Working, no active tool                     | —            | tool state, or `finished`        |
| `reading`  | Read-like tool active                       | 1 s linger   | next tool, `thinking`, etc.      |
| `writing`  | Write-like tool active                      | 1 s linger   | next tool, `thinking`, etc.      |
| `finished` | Assistant message arrived + not working     | **3 s fixed**| `ready` (no pre-emption)         |
| `ready`    | "Your turn" — attentive, waiting for you    | —            | `idle` after 1 min; new work interrupts |
| `waking`   | 1 s "oh! they're back" pose                 | **1 s fixed**| `thinking` (no pre-emption)      |
| `sleep`    | "They're not coming back right now"         | —            | new activity → `waking`          |

### Decay chain

```
  finished  ─(3s fixed)─→  ready  ─(60s)─→  idle  ─(30min)─→  sleep
```

Any new Claude activity from `ready` or `idle` pops back into `thinking`
or the appropriate tool state immediately. From `sleep`, new activity
routes through `waking` for 1 s first, then `thinking`.

### Priority when events stack

1. `reading` / `writing` (tool-active)
2. `thinking`
3. `finished` (protected — 3 s fixed, queues pre-emptions)
4. `waking` (protected — 1 s fixed, queues pre-emptions)
5. `ready` / `idle` / `sleep`

A fresh tool start **pre-empts immediately** — no linger wait —
*unless* we're mid-`finished` or mid-`waking`, in which case the
transition is queued and runs at the end of the protected window. Claude
tends to produce bursts of same-type tool calls (many reads, then many
writes); during a burst the state stays stable because the same-type
tool re-entering just refreshes the linger timer. Long gaps between
bursts of *different* types are when we'll see the state flip through
`thinking` briefly.

Two states are protected from pre-emption: `finished` (3 s celebration)
and `waking` (1 s "oh! they're back"). If new activity arrives during
either window, the transition is queued and fires at the end of the
window. In practice this is rare — Claude doesn't usually resume
instantly after signalling completion, and `waking` is 1 s of harmless
lead-in before `thinking`.

### Tool → state mapping (first pass)

- `reading`: `Read`, `Grep`, `Glob`, `WebFetch`, `WebSearch`
- `writing`: `Write`, `Edit`, `MultiEdit`, `NotebookEdit`
- Everything else (`Bash`, `Task`, MCP tools, …) shows as `thinking` —
  don't over-fragment.

### Boot

Firmware starts in `idle`. It's the only state that doesn't assume any
recent history.

## State machine abstractions

Since you'll be tuning times and transitions iteratively without me
seeing the display, the machine needs to be **easy to tweak in one
place**. Proposed shape:

```cpp
// Personality.cpp — tunables at the top, edit and reflash.
struct StateConfig {
  const char* name;
  uint32_t min_ms;    // transitions requested before this are deferred
  uint32_t max_ms;    // auto-transition after this (0 = never)
  State    on_timeout;
};

static const StateConfig kStates[kStateCount] = {
  // name         min     max          on_timeout
  { "idle",          0,   30 * 60000,  SLEEP    },
  { "thinking",      0,   0,           IDLE     },   // no auto decay
  { "reading",       0,   0,           IDLE     },
  { "writing",       0,   0,           IDLE     },
  { "finished",   3000,   3000,        READY    },   // fixed 3 s
  { "ready",         0,   60000,       IDLE     },
  { "waking",     1000,   1000,        THINKING },   // fixed 1 s
  { "sleep",         0,   0,           IDLE     },
};

// Per-tool linger — held in a small table, extended/refreshed each
// matching PreToolUse.
static constexpr uint32_t kToolLingerMs = 1000;
```

Event handlers stay small and declarative:

```cpp
onPreToolUse(name):   routeToActive(toolToState(name))
onPostToolUse(name):  extendLinger(toolToState(name), kToolLingerMs)
onStopHook:           request(FINISHED)      // trigger for "hooray done"
onUserPromptSubmit:   routeToActive(THINKING)

// Helper: incoming activity from sleep → waking, else direct.
void routeToActive(State target) {
  if (current() == SLEEP) request(WAKING);   // waking's timeout → THINKING
  else                    request(target);
}
```

Transition rules enforced inside `request(target)`:

- If current state is in its `min_ms` window, **queue** the request.
- Else if `target` is higher priority or equal, transition immediately.
- Else, ignore.

`tick()` checks the timeout and the queued transition each loop.

### What this gets you

- **Every time constant in one table.** Linger times, fixed animation
  lengths, decay periods — all at the top of one file.
- **Adding a state is one row** plus an event-handler clause.
- **Priority-protected transitions** (finished can't be stomped).
- **Queue-on-min-time** so fixed animations always play in full.

## Face rendering

For v1, each state has a **static frame-drawing function** in `Face.cpp`:

```cpp
void drawIdle(TFT_eSprite& s);
void drawThinking(TFT_eSprite& s);
void drawReading(TFT_eSprite& s);
void drawWriting(TFT_eSprite& s);
void drawFinished(TFT_eSprite& s);
void drawReady(TFT_eSprite& s);
void drawSleep(TFT_eSprite& s);
```

`FrameController::tick()` dispatches on `Personality::current()`. No tweening, no
procedural modulators. Snap-change on state transition.

When we later add animation, these become "compute FaceParams for this
state at time `t` since entry" and the renderer becomes shared. The
state machine doesn't change.

Initial frame sketches (to be refined on-hardware):

- **idle** — both eyes open, medium size, pupils centred, mouth flat
- **thinking** — eyes open, pupils up-and-to-one-side (looking away in
  thought), mouth small neutral
- **reading** — eyes narrowed horizontally (squint), pupils centred,
  mouth flat
- **writing** — eyes open, pupils down (looking at work), mouth slight
  open focused
- **finished** — eyes large and happy (^_^ curves), mouth big smile
- **ready** — eyes open, pupils centred on user, mouth small friendly
  smile
- **waking** — eyes popping open wide (surprised), mouth small "oh"
- **sleep** — eyes closed (horizontal lines), mouth flat, maybe "Z"s
  later

## Motion per state

Single servo drives both arms via a gear. Available gestures are limited
to pan amplitude + frequency:

- `idle` — rare small waggle, long pauses (attract-scheduler style)
- `thinking` — small continuous oscillation (current thinking-osc)
- `reading` — brief jog on entry, then still
- `writing` — rhythmic small jogs while active
- `finished` — single excited wave on entry, settle
- `ready` — neutral centred, rare attention-seeking flick
- `waking` — quick startle-jolt on entry
- `sleep` — completely still, centred

Each handler is tiny — e.g. `reading` entry = `Motion::playJog(+15°)`
once, then nothing until state changes.

## Integration with what's already built

- `AmbientMotion` and `AttractScheduler` get replaced by the Motion
  behaviours module. Keep them compiled and ignored during transition;
  delete once the new module reaches parity.
- `Display::drawBody*` / header code can stay for now behind a debug
  flag — useful while we're bringing the face up. Drop once the face
  is the default UI.

## Decisions (locked in)

- **`finished` trigger**: the Stop hook. Cleanest signal for "hooray done".
- **Waking from sleep**: new activity while in `sleep` routes through a
  1 s `waking` state before `thinking`, so there's a visible "oh!
  they're back" beat.
- **Protected windows**: `finished` (3 s) and `waking` (1 s) play in
  full. Pre-empting transitions are queued and fire on timeout.
