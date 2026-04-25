# Idea: declarative transition table for Personality

## Why this exists

Right now `Personality.cpp` mixes two kinds of routing:

- `kStates[]` — per-state timeout decay (`max_ms` → `on_timeout`)
- `onHook()` — imperative `if/else` chain mapping hook events to states,
  with ad-hoc guards (e.g. `routeToActive` special-casing SLEEP via
  WAKING, the tool-linger logic, etc.)

Adding a conditional rule like "SessionStart in SLEEP → WAKING(target=READY),
in IDLE → READY, in READY → EXCITED" means another branch in `onHook`
plus, in the SLEEP case, a side-channel like `sPostWakeTarget` to override
the WAKING timeout. Workable for one or two rules; gets messy after that.

## Sketch

Replace the `onHook` chain (and possibly the timeout column on `kStates`)
with a flat list of transition objects:

```cpp
struct Transition {
  enum Trigger { Hook, Timeout, Linger };
  Trigger     trigger;
  const char* hook_type;        // for Hook; nullptr = any
  uint32_t    state_mask;       // bitmask of source states; 0 = any
  bool      (*guard)(const Context&);   // optional extra condition
  State       to;
  // optional side-effects: set linger deadline, set post-wake target, etc.
};

static const Transition kTransitions[] = {
  // SessionStart routing
  { Hook, "SessionStart", bit(SLEEP),               nullptr, WAKING /*+target=READY*/ },
  { Hook, "SessionStart", bit(IDLE),                nullptr, READY                   },
  { Hook, "SessionStart", bit(READY),               nullptr, EXCITED                 },

  // UserPromptSubmit
  { Hook, "UserPromptSubmit", bit(SLEEP),           nullptr, WAKING /*+target=THINKING*/ },
  { Hook, "UserPromptSubmit", 0 /* any other */,    nullptr, THINKING },

  // PreToolUse — guard picks READING/WRITING/THINKING from tool name
  // (or split into three rows with guards)

  // Stop
  { Hook, "Stop", 0, nullptr, FINISHED },

  // Timeouts (subsume kStates[].on_timeout)
  { Timeout, nullptr, bit(FINISHED), nullptr, EXCITED },
  { Timeout, nullptr, bit(EXCITED),  nullptr, READY   },
  { Timeout, nullptr, bit(READY),    nullptr, IDLE    },
  { Timeout, nullptr, bit(IDLE),     nullptr, SLEEP   },
  { Timeout, nullptr, bit(WAKING),   nullptr, /* lookup post-wake target */ THINKING },
};
```

Dispatcher: on each hook event, walk the list and fire the first row whose
trigger + state_mask + guard all match. On each `tick()`, walk the Timeout
rows and fire any whose source state matches and whose `max_ms`
(still kept per-state, or moved into the row) has elapsed.

## What gets cleaner

- New rules are one row, not a new branch.
- "X happens only when in state Y" stops needing side-channel flags.
- Reading the table tells you everything that can happen and when.

## What gets worse

- ~80 lines of table + dispatcher to replace ~30 lines of `if`s.
- Side-effects (tool-linger arming, post-wake target) need a place to live
  — either an extra column (function pointer) or a parallel "on enter X"
  hook table. Either adds plumbing.
- Less obvious in a debugger which row fired (mitigated by logging the
  row index).
- Harder to read top-to-bottom for someone scanning for "what does
  Stop do?" — they need to know the table layout first.

## When to actually do it

When we reach for a third "only fire if state is X" guard. The
`sPostWakeTarget` patch is the second; one more conditional rule and
the imperative form starts costing more than the table would.
