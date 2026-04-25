# Legacy Text + Icon Display

Note for future-us. Before the face UI landed, `Display.cpp` rendered an
SSD1306-style layout on the 240×240 TFT: a three-icon header on top
(wifi strength bars, bridge check/cross, working spinner / idle blink)
and two or more wrapped lines of body text underneath showing either
the pending permission, the current tool + detail, or the last assistant
summary.

If we ever want to bring that back (e.g. as a debug overlay or a
"verbose" display mode), the full implementation lives in the git
history — see the commit before the "Face + Personality" work landed.
The commit message to look for is the one that split `Display.cpp`
into a driver + sprite accessor.

Short reference of the old API shape, for grepping history:

- `Display::tick()` — owned the redraw loop, called once per main loop
  pass.
- `Display::invalidate()` — forced a redraw on the next tick.
- Internal: `drawHeader()`, `drawBody()`, `drawBodyText()`,
  `drawBodyHard()`. Plus the 16x16 vector icons `drawWifi`,
  `drawCheck`, `drawCross`, `drawSpinner`, `drawIdleDot`.

The text wrapper was chord-aware — for each row it computed the
half-chord of the safe 100px-radius circle at that row's vertical
centre, and greedy-fit words into that pixel width. That code might be
worth resurrecting on its own if we ever add an info overlay that needs
to wrap inside the round panel.

The state sources are unchanged (`ClaudeEvents::state()` — same
`pending_permission`, `current_tool`, `tool_detail`, `last_summary`
fields), so a revived text UI would still have everything it needs.
