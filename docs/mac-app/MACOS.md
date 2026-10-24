# macOS platform notes

Dock tile, bounce, the robot window, hosting the engine in JavaScriptCore,
power, and distribution.

Facts with an external source are listed under **Sources** at the end.
Anything marked *(verify)* should be checked on the macOS version you're
targeting. Tahoe (macOS 26) changed icon handling, and later releases may have
changed it again.

---

## App type

- **It must be a regular app** (activation policy `.regular`; `LSUIElement`
  not set) to have a Dock tile. That also means it appears in ⌘-Tab.
  Menu-bar-only (accessory) apps have no Dock tile.
- **Suggested minimum: macOS 14.** That gives you `NSView.displayLink`, and
  `SMAppService` (macOS 13) for launch at login.

## Dock tile

### Drawing

There are two APIs:

1. **`NSApp.dockTile.contentView = view`, then `NSApp.dockTile.display()`
   (recommended).** Nothing animates by itself: you call `display()` for
   every frame. Libraries such as DSFDockTile and DSFImageFlipbook do it this
   way.
2. **`NSApp.applicationIconImage = image`.** Simpler, but a long-standing
   Cocoa-dev report saw memory grow when it was set repeatedly for animation.
   Electron's `app.dock.setIcon` uses this path.

Practical notes:
- Draw at `NSApp.dockTile.size` (in points) × the screen's backing scale.
- The face source is 240×240. Scale with nearest-neighbour
  (`interpolationQuality = .none` / `magnificationFilter = .nearest`) to keep
  the TFT look.
- The tile is square and the robot's screen is round. You decide what fills
  the corners.

### Frame rate

- Apple documents no limit, and I found no benchmarks. Each `display()` is a
  round trip to the Dock process.
- **Measure** Dock and app CPU in Instruments at 10, 15 and 30 fps before
  settling on a number.
- Suggested policy:
  - ~30 fps while something is animating;
  - drop to a few fps, or a still frame, when nothing visible is changing (for
    example asleep, or no verb with the emotion settled);
  - don't push 60 fps to the Dock even when the simulation runs at 60.
  - Blink and glance timing are random, so at very low rates you'll miss
    blinks. That's acceptable when idle.

### Icon appearance on Tahoe

- **While the app is running**, a runtime icon (either API) is shown *exactly
  as given*:
  - no Liquid Glass edge highlights;
  - no automatic Dark / Clear / Tinted variants (handle them yourself if you
    care);
  - it can fill the whole tile and doesn't have to be a squircle.
- **When the app isn't running**, the Dock shows the bundle's static icon.
  Make it with Icon Composer (`.icon`); Tahoe puts non-conforming icons inside
  a squircle.
- **A custom icon that persists after quit** needs an `NSDockTilePlugIn`,
  which the Dock loads itself. It isn't allowed in the Mac App Store and isn't
  meant for animation. You probably don't need it: launching at login keeps
  the live tile up.

### Bounce, badge, menu

- **Bounce:**
  - `NSApp.requestUserAttention(.informationalRequest)` bounces once.
  - `.criticalRequest` keeps bouncing until the user switches to the app.
  - `cancelUserAttentionRequest(_:)` stops it.
- **The bounce does nothing while the app is active (frontmost).** This is why
  the robot window should be a non-activating panel (below). The bounce
  animation is fixed by the system; any custom "hop" has to be drawn inside
  the tile.
- **Badge:** `NSApp.dockTile.badgeLabel = "2"`, for example the number of
  sessions waiting on a prompt. Optional; the robot has no equivalent.
- **Dock menu:** implement `applicationDockMenu(_:)` for "Show robot",
  "Always on top", "Connect…" and similar.
- **Clicking the Dock icon** calls
  `applicationShouldHandleReopen(_:hasVisibleWindows:)`. Show the robot window
  there.

---

## Robot window

### Window setup

- **Use an `NSPanel` with `.nonactivatingPanel`.** Clicking or dragging the
  robot then doesn't bring the app to the front, so bounces keep working.
- **Set `hidesOnDeactivate = false`.** Panels default to `true`, which hides
  the panel whenever the app is in the background, which is always, for this
  app.
- **Resizable with a fixed shape:** `contentAspectRatio` plus `minSize`. Save
  the position and size with `setFrameAutosaveName`.
- **Always on top (a toggle):** `level = .floating`, back to `.normal` when
  off. To also show over full-screen apps and on every Space, set
  `collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]`.

### Two possible looks

1. **A normal window with a transparent title bar:**
   `titlebarAppearsTransparent`, `.fullSizeContentView`, title hidden. You
   keep the resize edges and traffic-light buttons for free.
2. **A borderless "desktop pet" window shaped like the robot:**
   - `styleMask = [.borderless, .nonactivatingPanel, .resizable]`
   - `isOpaque = false`, `backgroundColor = .clear`, `hasShadow = true`,
     `isMovableByWindowBackground = true`
   - There are no visible resize edges or buttons, so add a resize grip, size
     presets in the Dock/context menu, or both.
   - Optional click-through mode: `ignoresMouseEvents = true`. Clicks on fully
     transparent pixels generally pass through to the window below anyway
     *(verify)*.

### Drawing

One engine tick per frame feeds both the Dock tile and the window.

Use a layer tree, with implicit animations disabled
(`CATransaction.setDisableActions(true)`) for each frame's updates:

```
root (clear)
 ├─ body        CALayer, contents = body art
 ├─ armLeft     CALayer, anchorPoint = shoulder pivot, transform = rotate(−θ)
 ├─ armRight    CALayer, anchorPoint = shoulder pivot, transform = rotate(+θ)
 └─ screen      CALayer, contents = CGImage(240×240), magnificationFilter = .nearest,
                cornerRadius = w/2, masksToBounds = true   (round GC9A01)
```

- **θ** is the engine's arm offset in degrees, rounded and clamped to ±45° (see
  `ENGINE.md` §5). The sign convention and the rest pose depend on your art.
- **Layer order** (arms in front of or behind the body) is a design choice.
- **Pixel-art scaling:** at non-integer scale factors, nearest-neighbour makes
  some screen pixels wider than others. Optionally snap the screen layer to a
  whole multiple of 240 px, or accept it.
- **Frame pacing:** use `NSView.displayLink(target:selector:)` (macOS 14) and
  advance the simulation at the robot's own rate: 33 ms, or 16 ms while the
  code streams are showing.
- **Stop drawing the window when it's hidden** behind other windows
  (`NSWindow.didChangeOcclusionStateNotification` / `occlusionState`). Keep
  the simulation and the Dock tile running.

---

## Running the engine in JavaScriptCore

- Create a `JSContext` and run the esbuild bundle in it (`evaluateScript`).
- **JavaScriptCore provides only the language itself:** no `performance`,
  `console`, `setTimeout` or `requestAnimationFrame`.
  - Inject a `now()` clock and a `console.log` shim.
  - Make sure the engine code paths don't use `setTimeout`. `frameController`
    only uses `requestAnimationFrame`; `logReplay.ts`, which is editor-only,
    uses `setTimeout`.
- **Threading:** a `JSContext` is not thread-safe. Keep all calls on one
  queue (main, or a dedicated serial queue), and send WebSocket messages to
  that queue.
- **Per frame:** `robot.tick(nowMs)`, then read the 240×240×4 RGBA buffer.
  The C function `JSObjectGetTypedArrayBytesPtr` gives you direct access to
  the typed array's bytes without copying. Wrap them in a `CGImage` through a
  `CGDataProvider`, or copy them into a reused bitmap context.
- **Debugging:** set `context.isInspectable = true` (macOS 13.3+), then attach
  Safari's Web Inspector (Develop menu).
- **Live-reloading the config (optional):** watch
  `FACE_CONFIG_DATA.snapshot.json` in a repo checkout and push it into the
  engine. Saves from the editor would then show up in the Dock without
  rebuilding.

---

## Power, sleep and wake

- **App Nap:** an app with no visible windows gets napped, which throttles its
  timers. While animating, hold an activity token from
  `ProcessInfo.processInfo.beginActivity(options:reason:)` and end it when
  idle. `.userInitiatedAllowingIdleSystemSleep` is a reasonable starting point
  *(verify that it keeps the frame timer steady)*.
- **Sleep/wake:** listen for `NSWorkspace.willSleepNotification` and
  `didWakeNotification`. Pause rendering while asleep and reconnect the
  WebSocket on wake. Remember the robot's disconnect gap (`BRIDGE.md`).
- **Screen sleep** (`screensDidSleepNotification`): pause rendering.

## Preferences and secrets

- Store host and port in `UserDefaults`, and the bridge token in the Keychain.
- If a preference is missing, fail with a clear message rather than inventing
  a default token. This matches the bridge, which refuses to start without
  `BRIDGE_TOKEN`.

## Launch at login

`SMAppService.mainApp.register()` (macOS 13+), behind a preference toggle.

## Distribution

- **Just for you:** build and run from Xcode.
- **To share outside the App Store:** sign with a Developer ID and notarize
  (`notarytool`).
- **Mac App Store:** requires the sandbox plus the
  `com.apple.security.network.client` entitlement. `NSDockTilePlugIn` tricks
  aren't allowed.
- **Local Network privacy:** connecting to a bridge on *another* machine on
  the LAN may trigger macOS's Local Network permission prompt (Sequoia and
  later), so add `NSLocalNetworkUsageDescription`. `127.0.0.1` isn't affected
  *(verify)*.

---

## Electron, for comparison

If you go the Electron route instead (see `README.md`, "Alternatives
considered"):

| Need | Electron |
|---|---|
| Dock image | `app.dock.setIcon(nativeImage)` (the `applicationIconImage` path; reuse images and watch memory) |
| Bounce | `app.dock.bounce('informational' \| 'critical')`, `app.dock.cancelBounce(id)` |
| Badge | `app.dock.setBadge(text)` |
| Always on top | `win.setAlwaysOnTop(true, 'floating')`, `win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true })` |
| Aspect lock | `win.setAspectRatio(1)` |
| Pet window | `new BrowserWindow({ transparent: true, frame: false })` |
| Bridge in-process | Possible: import `plugin/dist` into the main process |

---

## Sources

- [NSDockTile (Apple docs)](https://developer.apple.com/documentation/appkit/nsdocktile)
- [DSFDockTile](https://github.com/dagronf/DSFDockTile) and [DSFImageFlipbook](https://swiftpackageregistry.com/dagronf/DSFImageFlipbook): animated Dock tile libraries (frames pushed by hand with `display()`)
- [Cocoa-dev: does setApplicationIconImage leak memory?](https://lists.apple.com/archives/Cocoa-dev/2008/Aug/msg00552.html)
- [macOS Tahoe's new theming system (Michael Tsai)](https://mjtsai.com/blog/2025/06/19/macos-tahoes-new-theming-system/): runtime icons shown as given, no highlights, full tile
- [Escape from squircle jail (Daring Fireball)](https://daringfireball.net/linked/2026/07/08/escape-from-squircle-jail): `NSDockTilePlugIn` outside the App Store
- [So, you think it's easy to change an app icon? (Granola)](https://www.granola.ai/blog/so-you-think-its-easy-to-change-an-app-icon): plugin, Finder icon, and Dock icon cache behaviour
- [Electron dock API](https://www.electronjs.org/docs/latest/api/dock)
