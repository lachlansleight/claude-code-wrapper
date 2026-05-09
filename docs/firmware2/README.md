# robot_v3 firmware documentation

This folder documents the current state of the ESP32-S3 firmware in
[`robot_v3/`](../../robot_v3/). The older `docs/firmware/` was written for an
earlier shape of the codebase and is being retired.

Read in order:

1. [`OVERVIEW.md`](OVERVIEW.md) — module map, layering, setup/loop, per-frame
   timing.
2. [`FRAME_CONTROLLER.md`](FRAME_CONTROLLER.md) — the per-frame face pipeline:
   emotion smoothing, verb timeline sampling, blink, body bob, gaze, mood
   ring, the final modification pass.
3. [`EMOTION_SYSTEM.md`](EMOTION_SYSTEM.md) — 2D valence×activation emotion
   model, hysteresis snap, held drivers, Delaunay triangulation blend.
4. [`VERB_SYSTEM.md`](VERB_SYSTEM.md) — discrete verb state machine, overlays,
   linger, Executing→Straining promotion. Also covers the verb animation
   timeline (sparse-override format, ready for keyframing).
5. [`SCENE_AND_RENDERING.md`](SCENE_AND_RENDERING.md) — Scene composition,
   FaceRenderer geometry, effects, mood ring, activity dots, text scene.
6. [`MOTION.md`](MOTION.md) — servo HAL and `MotionBehaviors` arm policy.
7. [`BRIDGE_AND_EVENTS.md`](BRIDGE_AND_EVENTS.md) — WebSocket client, the
   `agent_event` vocabulary, `EventRouter` mapping, `SceneContextFill`.
8. [`CONFIG_AND_HAL.md`](CONFIG_AND_HAL.md) — display, settings/NVS,
   provisioning, WiFi, utilities.

For the bridge service itself (the producer of `agent_event`), see
[`docs/bridge/OBJECT_INTERFACE.md`](../bridge/OBJECT_INTERFACE.md) and
[`docs/bridge/HOOK_MAPPING.md`](../bridge/HOOK_MAPPING.md).
