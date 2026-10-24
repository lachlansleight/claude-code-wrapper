# SultanaCore: the robot_v3 firmware on a desktop

This folder builds the firmware's own behaviour, face and motion code for a
desktop host, unmodified, and drives it through a small C API. It is the
engine inside the Sultana Mac app (`sultana-dock/`) and, through the
`sultana-replay` CLI, the firmware's regression test suite.

Nothing here is compiled into the ESP32 build: Arduino only compiles the
sketch's `src/` folder.

## Build

Needs a C++17 compiler (clang or gcc) and `make`.

```bash
cd robot_v3/host
make          # build/libSultanaCore.a and build/sultana-replay
make test     # replay every fixture, diff against its golden trace
```

The firmware's per-machine `robot_v3/src/config.h` must exist (copy
`config.example.h`), because `hal/Motion.cpp` includes it by relative path.
The host only uses its `SERVO_PIN`, which the servo stub ignores.

On Windows, use clang or gcc from MSYS2 with its `make`; this has not been
tried yet.

## Replaying a log

```bash
./build/sultana-replay fixtures/claude-turn.jsonl --trace
```

```
t=   9193 ms  << agent_event activity.started (shell.exec) [fixture-a]
t=   9193 ms     verb=executing     expr=verb_executing   v=+0.55 a=0.50 arm=-18 latch=fixture-a
t=  14193 ms     verb=straining     expr=verb_straining   v=+0.52 a=0.50 arm=-10 latch=fixture-a
```

`<<` lines are frames the robot received; the other lines appear whenever
the effective verb, expression, latched session or pending permission
changes, with valence, activation and the commanded arm angle. `>>` lines are
frames the robot sent back (`request_sessions`).

| Option | |
|---|---|
| `--trace` | Print the trace above. |
| `--frames DIR` | Write the screen as PNGs into `DIR` every `--frame-every` ms (default 250). |
| `--seed N` | Seed for blink and glance timing (default 1). Same log and seed, same pixels. |
| `--step MS` | Simulation step (default 1 ms, close to the device's loop rate). |
| `--tail MS` | Keep running this long after the last frame (default 4000). |
| `--frame-hash` | Print a hash over every frame pushed, to check whether anything on screen changed. |
| `--final-state` | Print the core's state JSON at the end. |
| `--logs` | Show the firmware's `LOG_*` output on stderr. |

The log is JSONL, one received WebSocket frame per line, as written by
`scripts/record-ws.mjs`:

```
{"t": 2538, "frame": {"type": "agent_event", ...}}
{"t": 27258, "connected": false}
```

`{"t", "text": "<raw frame>"}` also works. The clock comes from `t`, so a
replay is deterministic.

## Fixtures and golden traces

`fixtures/*.jsonl` are real bridge recordings; `fixtures/*.trace.txt` are
the traces they produce today. `make test` fails if the firmware now
behaves differently. If the change was intended, run `make golden` and
commit the new traces with the firmware change, so the diff shows the
behaviour change. Recording new fixtures: [`fixtures/README.md`](fixtures/README.md).

## How it works

```
stubs/        Arduino.h, TFT_eSPI.h, ESP32Servo.h, Preferences.h, esp_random.h
hal/          Display_host.cpp, Bridge_host.cpp: host versions of src/hal/Display.h
              and src/bridge/BridgeClient.h
core/         sultana_core.h / .cpp: the C API (read the header for the contract)
cli/          sultana-replay, plus a dependency-free PNG writer
third_party/  ArduinoJson 7.4.3 (single header), TFT_eSPI licence
fixtures/     recorded logs, golden traces, the scenario script that made them
```

- **The firmware files compile as they are.** `FIRMWARE_SRCS` in the
  `Makefile` lists them: everything under `agents/`, `app/`, `behaviour/`,
  `core/` and `face/`, plus `hal/Motion.cpp`, `MotionBehaviors.cpp` and
  `Settings.cpp`. WiFi, provisioning, serial debugging, bridge discovery, the
  real display driver and the real WebSocket client are left out.
- **The sprite is pixel-faithful.** `stubs/TFT_eSPI.h` ports TFT_eSPI's
  sprite primitives (lines, circles, the anti-aliased `fillSmoothCircle`)
  so edges land on the same pixels as the panel. Text drawing is a no-op;
  only the text and debug render modes use it, and the host forces face mode.
- **Clock and randomness are injected.** `millis()` is whatever the host
  passed to the last API call, plus 10 s so it is never 0. `random()` and
  `esp_random()` share one seeded xorshift generator.
- **Loop order matches `robot_v3.ino`.** `sultana_core_tick()` runs
  `EventRouter::tick`, `SceneContextFill::fill`, `Face::tickEffectiveParams`,
  `MotionBehaviors::tick`, `Motion::tick`, `Face::tick`, in that order. The
  face throttles itself to 33 ms (16 ms while code streams show), so ticking
  faster changes nothing.

Adding a firmware source file means adding it to `FIRMWARE_SRCS` here and to
the `SultanaCore` target in `sultana-dock/`. If it needs a new Arduino or
ESP-IDF API, extend the matching stub.

## What matches the robot

Identical: every rule in the event-to-verb and emotion logic, frame cadence,
face geometry, mood ring, code streams, activity dots, and the arm angle the
firmware commands. Not identical: blink and glance timing (random), how far
the physical servo has actually travelled, the panel's colour response, and
text/debug render modes. More detail in
[`docs/mac-app/PLAN.md`](../../docs/mac-app/PLAN.md) §4.7.

Behaviour the replays have surfaced, all real firmware behaviour:

- A turn that starts within 1.5 s of `session.started` plays Waking again,
  because the current verb is still Sleeping until the first Waking overlay
  finishes (`two-sessions` fixture, t=1535).
