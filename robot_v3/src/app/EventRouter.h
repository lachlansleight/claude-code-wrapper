#pragma once

#include <ArduinoJson.h>

/**
 * @file EventRouter.h
 * @brief Composition root: maps bridge events into behaviour systems.
 *
 * The bridge speaks lifecycle events; the behaviour systems
 * (`VerbSystem`, `EmotionSystem`) speak verbs and emotion drivers.
 * EventRouter is the wiring that converts one into the other and is
 * the only place these mappings live. It also owns the deterministic
 * dispatch order — every loop, AgentEvents::tick first, then
 * VerbSystem::tick, then derived emotion-driver maintenance, then
 * EmotionSystem::tick.
 *
 * ## Mappings (agent_event → behaviour)
 *
 *  - `session.started` — fire `Waking` overlay then clear verb;
 *    impulse valence + arousal.
 *  - `session.ended` — back to `Sleeping`.
 *  - `turn.started` — `Thinking`. If we were sleeping, wake first.
 *  - `activity.started` — `Reading` / `Writing` / `Executing` based
 *    on AgentEvents::classifyActivity (shell.exec is always
 *    Executing, file.write/delete/notebook.edit are Writing).
 *  - `activity.finished/failed` — arm a 1 s linger so rapid same-type
 *    tools stay in the same verb instead of strobing.
 *  - `turn.ended` — clear verb; impulse positive valence + arousal.
 *  - `notification` starting with "Claude needs" — fire
 *    `AttractingAttention` overlay.
 *
 * ## Derived emotion drivers
 *
 *  - **Pending permission** held at v=−0.6 while
 *    AgentState.pending_permission is non-empty.
 *  - **Strain** held at v=−0.4 once Straining has been current for
 *    ≥30 s.
 *
 * Both are released as soon as the condition drops, so behaviour can
 * recover smoothly.
 *
 * ## Bridge controls
 *
 * EventRouter also wires the `BridgeControl` callbacks (palette,
 * display mode, servo override) directly into Settings / AgentEvents /
 * Motion. Plus a small "raw command" path for `emotion.command` and
 * verb/overlay test frames sent by operator UIs.
 *
 * ## Session re-latching
 *
 * On reconnect, the firmware loses any latched session id. While
 * connected and unlatched, the router polls
 * `Bridge::requestSessions()` every 5 s so the bridge resends the
 * active list, allowing AgentEvents to relatch.
 */
namespace EventRouter {

/**
 * Initialise behaviour systems and register all callbacks. Must be
 * called once in setup() after Settings::begin() (so the initial
 * RenderMode reflects the persisted face_mode).
 */
void begin();

/**
 * Per-loop pump. Drives AgentEvents::tick, polls for session
 * re-latching, runs VerbSystem::tick, applies/releases derived
 * emotion drivers based on the current AgentState + verb, then
 * EmotionSystem::tick. Order is deliberate — verb tick must run
 * before driver maintenance so the strain timer is up-to-date.
 */
void tick();

/**
 * Hand a parsed bridge frame to all dispatchers in order: raw
 * command parser → AgentEvents → BridgeControl. Each is independent
 * and ignores frames it doesn't recognise. Wired directly into
 * Bridge::onMessage from setup().
 */
void onBridgeMessage(JsonDocument& doc);

/// Forward Bridge connection state changes into AgentEvents. Wired into Bridge::onConnection.
void onBridgeConnection(bool connected);

}  // namespace EventRouter
