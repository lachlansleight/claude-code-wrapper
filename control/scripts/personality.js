// Port of robot_v2/Personality.cpp. Drives state transitions in response
// to AgentEvents (consumed via Personality.handleAgentEvent) and timeouts
// (advanced via Personality.tick or the auto-tick loop).

(function () {
  const STATES = {
    idle:            { name: "idle",            min_ms: 0,    max_ms: 30 * 60 * 1000, on_timeout: "sleep" },
    thinking:        { name: "thinking",        min_ms: 0,    max_ms: 0,              on_timeout: "idle" },
    reading:         { name: "reading",         min_ms: 0,    max_ms: 0,              on_timeout: "idle" },
    writing:         { name: "writing",         min_ms: 0,    max_ms: 0,              on_timeout: "idle" },
    executing:       { name: "executing",       min_ms: 0,    max_ms: 5 * 1000,       on_timeout: "executing_long" },
    executing_long:  { name: "executing_long",  min_ms: 0,    max_ms: 30 * 1000,      on_timeout: "blocked" },
    finished:        { name: "finished",        min_ms: 1500, max_ms: 1500,           on_timeout: "excited" },
    excited:         { name: "excited",         min_ms: 0,    max_ms: 10 * 1000,      on_timeout: "ready" },
    ready:           { name: "ready",           min_ms: 0,    max_ms: 60 * 1000,      on_timeout: "idle" },
    waking:          { name: "waking",          min_ms: 1000, max_ms: 1000,           on_timeout: "thinking" },
    sleep:           { name: "sleep",           min_ms: 0,    max_ms: 0,              on_timeout: "idle" },
    blocked:         { name: "blocked",         min_ms: 0,    max_ms: 0,              on_timeout: "idle" },
    wants_attention: { name: "wants_attention", min_ms: 1000, max_ms: 1000,           on_timeout: "idle" },
  };

  const kToolLingerMs = 1000;

  let sCurrent = "sleep";
  let sQueued = "sleep";
  let sHasQueued = false;
  let sEnteredMs = 0;

  let sToolLingerDeadlineMs = 0;
  let sPostWakeTarget = "thinking";
  let sPreBlockedState = "thinking";
  let sPreAttentionState = "idle";
  let sBlockedByPermission = false;
  let sPendingPermission = false;

  const listeners = [];

  function nowMs() { return performance.now(); }

  function notify() {
    for (const fn of listeners) fn(sCurrent);
  }

  function transitionTo(target) {
    if (target === sCurrent) {
      sHasQueued = false;
      return;
    }
    sCurrent = target;
    sEnteredMs = nowMs();
    sHasQueued = false;
    sToolLingerDeadlineMs = 0;
    if (target !== "blocked") sBlockedByPermission = false;
    notify();
  }

  function request(target) {
    if (target === sCurrent) return;
    const elapsed = nowMs() - sEnteredMs;
    if (elapsed < STATES[sCurrent].min_ms) {
      sQueued = target;
      sHasQueued = true;
      return;
    }
    transitionTo(target);
  }

  function routeToActive(target) {
    if (sCurrent === "sleep") {
      sPostWakeTarget = target;
      request("waking");
    } else {
      request(target);
    }
  }

  // Map an agent_event activity to a Personality state. Mirrors the
  // firmware's classifyActivity: shell.exec → executing, *.write/* /
  // notebook.edit etc → writing, everything else → reading.
  function activityToState(activity) {
    const kind = activity?.kind || "";
    if (kind === "shell.exec" || kind === "shell.background") return "executing";
    if (
      kind === "file.write" ||
      kind === "file.edit" ||
      kind === "file.delete" ||
      kind === "notebook.edit"
    ) return "writing";
    return "reading";
  }

  function handleAgentEvent(ev) {
    if (!ev || !ev.kind) return;
    switch (ev.kind) {
      case "session.started": routeToActive("excited"); return;
      case "session.ended":   request("sleep"); return;
      case "turn.started":    routeToActive("thinking"); return;
      case "turn.ended":      request("finished"); return;
      case "activity.started":
        routeToActive(activityToState(ev.activity));
        sToolLingerDeadlineMs = 0;
        return;
      case "activity.finished":
      case "activity.failed":
        if (
          sCurrent === "reading" || sCurrent === "writing" ||
          sCurrent === "executing" || sCurrent === "executing_long"
        ) {
          sToolLingerDeadlineMs = nowMs() + kToolLingerMs;
        }
        return;
      case "permission.requested":
        sPendingPermission = true;
        return;
      case "permission.resolved":
        sPendingPermission = false;
        return;
      case "notification": {
        const text = ev.text || "";
        if (!text.startsWith("Claude needs")) return;
        if (sCurrent === "sleep" || sCurrent === "waking" ||
            sCurrent === "wants_attention") return;
        sPreAttentionState = sCurrent;
        request("wants_attention");
        return;
      }
      default:
        return;
    }
  }

  function tick() {
    const now = nowMs();
    const elapsed = now - sEnteredMs;
    const cfg = STATES[sCurrent];

    if (sPendingPermission && sCurrent !== "blocked" && sCurrent !== "waking") {
      sPreBlockedState = sCurrent;
      sBlockedByPermission = true;
      transitionTo("blocked");
      return;
    }
    if (!sPendingPermission && sCurrent === "blocked" && sBlockedByPermission) {
      sBlockedByPermission = false;
      transitionTo(sPreBlockedState);
      return;
    }

    if (sHasQueued && elapsed >= cfg.min_ms) {
      transitionTo(sQueued);
      return;
    }

    if (sToolLingerDeadlineMs !== 0 && now >= sToolLingerDeadlineMs &&
        (sCurrent === "reading" || sCurrent === "writing" ||
         sCurrent === "executing" || sCurrent === "executing_long")) {
      sToolLingerDeadlineMs = 0;
      transitionTo("thinking");
      return;
    }

    if (cfg.max_ms > 0 && elapsed >= cfg.max_ms) {
      let next = cfg.on_timeout;
      if (sCurrent === "waking") next = sPostWakeTarget;
      else if (sCurrent === "wants_attention") next = sPreAttentionState;
      transitionTo(next);
    }
  }

  function start() {
    sEnteredMs = nowMs();
    setInterval(tick, 50);
  }

  window.Personality = {
    start,
    tick,
    current() { return sCurrent; },
    request,
    handleAgentEvent,
    onChange(fn) { listeners.push(fn); },
    states() { return Object.keys(STATES); },
  };
})();
