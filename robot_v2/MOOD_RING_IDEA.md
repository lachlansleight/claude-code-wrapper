# Idea: mood-coloured ring around the GC9A01 face

A coloured halo around the outside of the round display, driven by the
current Personality mood. Lerps each frame towards the target colour
with a 200 ms time constant, so transitions feel breathed rather than
snapped.

## Mood → colour

Initial palette (tweak on hardware):

| State    | Colour                       | Notes                                  |
|----------|------------------------------|----------------------------------------|
| THINKING | pale, dark blue              | the "working quietly" mood             |
| EXCITED  | bright pale yellow-orange    | finished + happy                       |
| READY    | vivid bright green           | freshly attentive                      |
| BLOCKED  | (TBD — red? amber?)          | sad face, awaiting permission verdict  |
| others   | off / very dark              | IDLE, SLEEP, WAKING, FINISHED          |

Document the chosen RGB565 values inline in `Face.cpp` next to the
palette.

## Rendering

- Outer annulus on the 240×240 GC9A01, somewhere between the bezel and
  the face. Width TBD — 8–12 px is probably enough to read at a glance
  without crowding the eyes/mouth.
- Single colour fill per frame — no gradient, no animation beyond the
  lerp. Simpler is better here.

## Timing

- Each frame, lerp current RGB towards target RGB.
- 200 ms time constant — i.e. per-frame `alpha = 1 - exp(-dt / 200ms)`,
  applied per channel. Simpler frame-rate-independent form than fixed
  step sizes.
- When mood changes mid-lerp, target swaps immediately; current keeps
  going from wherever it was. No queue, no easing curves.

## Implementation sketch

- New module `MoodRing.{h,cpp}` (or fold into `Face` if it ends up
  trivial). Polls `Personality::current()` once per frame, looks up
  the target RGB from a small table, lerps state, draws.
- Lives next to the existing face render so it shares the same redraw
  cadence.
- No dirty-rect optimisation needed — the ring is small and the GC9A01
  push is fast enough that a full redraw per frame is fine.

## Open questions

- Exact colours — pick on hardware, the pale/vivid distinction matters
  more than the hex.
- Does BLOCKED get a colour? (Probably yes — red feels right.)
- Does the ring blink/pulse for BLOCKED to demand attention, or stay
  solid? Solid first; revisit if it doesn't read.
