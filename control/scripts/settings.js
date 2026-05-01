// Mirrors the firmware's Settings::NamedColor defaults (see Settings.cpp /
// control/index.html COLOR_DEFS). Editable at runtime via setColor().
window.RobotSettings = (function () {
  const defaults = {
    background: [0, 0, 0],
    foreground: [255, 255, 255],
    thinking: [36, 56, 120],
    reading: [78, 146, 210],
    writing: [104, 118, 228],
    executing: [156, 64, 216],
    executing_long: [210, 75, 220],
    blocked: [255, 48, 24],
    finished: [255, 228, 32],
    excited: [40, 255, 80],
    wants_at: [255, 200, 40],
  };
  const colors = JSON.parse(JSON.stringify(defaults));
  let version = 1;
  return {
    rgb(name) {
      return colors[name] || [0, 0, 0];
    },
    set(name, r, g, b) {
      if (!colors[name]) return;
      colors[name] = [r & 0xff, g & 0xff, b & 0xff];
      version += 1;
    },
    version() {
      return version;
    },
    keys() {
      return Object.keys(defaults);
    },
  };
})();
