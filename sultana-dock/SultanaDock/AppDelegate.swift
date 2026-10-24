import AppKit
import Combine
import SwiftUI

/// Wires the bridge socket, the firmware core, the Dock tile, the robot
/// window and bounces together. Everything runs on the main thread.
///
///   bridge ──text frame──► BridgeSignal (bounce, any session)
///                     └──► SultanaEngine (firmware: latch, verbs, emotion, face)
///   display link ──► engine.tick ──pixels changed──► Dock tile (≤ fps cap)
///                                              └───► robot window (screen + arms)
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let preferences = AppPreferences()
    private let status = AppStatus()
    private let engine = SultanaEngine()
    private let socket = BridgeSocket()
    private let attention = AttentionController()
    private let replay = ReplayController()
    private var dockTile: DockTileController!
    private var robotWindow: RobotWindowController!
    private var latestFrame: CGImage?
    private var robotFrameChangeCount: UInt64 = .max
    private var displayLink: CADisplayLink?
    private var activity: NSObjectProtocol?
    private var settingsWindow: NSWindow?
    private var isAsleep = false
    private var cancellables: Set<AnyCancellable> = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = MainMenu.build(target: self)

        dockTile = DockTileController()
        robotWindow = RobotWindowController()
        robotWindow.onVisibilityChange = { [weak self] visible in
            guard let self, self.preferences.robotWindowVisible != visible else { return }
            self.preferences.robotWindowVisible = visible
        }
        robotWindow.view.menuProvider = { [weak self] in self?.robotContextMenu() }
        preferences.$dockFramesPerSecond
            .sink { [weak self] fps in self?.dockTile.framesPerSecond = fps }
            .store(in: &cancellables)
        preferences.$robotWindowVisible
            .sink { [weak self] visible in self?.setRobotWindowVisible(visible) }
            .store(in: &cancellables)
        preferences.$robotWindowOnTop
            .sink { [weak self] onTop in self?.robotWindow.keepsOnTop = onTop }
            .store(in: &cancellables)

        engine.onSend = { [weak self] text in self?.socket.send(text) }
        socket.delegate = self
        replay.onFrame = { [weak self] text in self?.handleFrame(text) }
        replay.onConnected = { [weak self] connected in self?.engine.setConnected(connected) }
        replay.onFinish = { [weak self] in self?.replayFinished() }

        startDisplayLink()
        NotificationCenter.default.addObserver(
            forName: NSApplication.didChangeScreenParametersNotification, object: nil, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.startDisplayLink() }
        }
        observeSleepAndWake()

        // Developer aids: SULTANA_REPLAY=/path/to/log.jsonl replays a recording
        // at launch instead of connecting; SULTANA_REPLAY_CLOCK=log replays it
        // on the log's clock (frame-exact with sultana-replay --seed 1).
        let env = ProcessInfo.processInfo.environment
        if let path = env["SULTANA_REPLAY"], !path.isEmpty {
            let clock = ReplayController.Clock(rawValue: env["SULTANA_REPLAY_CLOCK"] ?? "") ?? .wall
            startReplay(URL(fileURLWithPath: path), clock: clock)
        } else {
            connectLive()
        }
        if preferences.endpoint.isComplete || replay.isPlaying {
            DispatchQueue.main.async { self.yieldFocus() }
        } else {
            showSettings(nil)
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        socket.stop()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    /// Clicking the Dock icon shows the robot window, or Settings when the
    /// connection needs fixing.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !preferences.endpoint.isComplete || socket.status == .unauthorized {
            showSettings(nil)
        } else {
            preferences.robotWindowVisible = true
            robotWindow.show()
            yieldFocus()
        }
        return false
    }

    func applicationDockMenu(_ sender: NSApplication) -> NSMenu? {
        let menu = NSMenu()
        let statusItem = NSMenuItem(title: status.text, action: nil, keyEquivalent: "")
        statusItem.isEnabled = false
        menu.addItem(statusItem)
        menu.addItem(.separator())
        menu.addItem(
            withTitle: robotWindow.isVisible ? "Hide Robot" : "Show Robot",
            action: #selector(toggleRobotWindow(_:)), keyEquivalent: ""
        ).target = self
        let onTop = menu.addItem(
            withTitle: "Keep Robot on Top", action: #selector(toggleRobotOnTop(_:)), keyEquivalent: "")
        onTop.target = self
        onTop.state = preferences.robotWindowOnTop ? .on : .off
        menu.addItem(robotSizeItem(title: "Robot Size"))
        menu.addItem(.separator())
        if replay.isPlaying {
            menu.addItem(withTitle: "Stop Replay", action: #selector(stopReplay(_:)), keyEquivalent: "").target = self
        } else {
            menu.addItem(withTitle: "Replay Log…", action: #selector(openReplay(_:)), keyEquivalent: "").target = self
        }
        menu.addItem(withTitle: "Settings…", action: #selector(showSettings(_:)), keyEquivalent: "").target = self
        return menu
    }

    /// macOS activates a freshly launched app and an app clicked in the Dock,
    /// and won't bounce the active app. Unless Settings is open, hand focus
    /// straight back to whatever was in front. The robot window is a
    /// non-activating panel and stays put.
    private func yieldFocus() {
        guard NSApp.isActive, settingsWindow?.isVisible != true else { return }
        NSApp.deactivate()
    }

    // MARK: Tick loop

    private func startDisplayLink() {
        displayLink?.invalidate()
        displayLink = nil
        guard let screen = NSScreen.main ?? NSScreen.screens.first else { return }
        let link = screen.displayLink(target: self, selector: #selector(displayLinkFired(_:)))
        link.add(to: .main, forMode: .common)
        link.isPaused = isAsleep
        displayLink = link
    }

    @objc private func displayLinkFired(_ link: CADisplayLink) {
        // The firmware throttles its own rendering to 33 ms (16 ms while the
        // code streams show), so ticking at the display rate is harmless.
        let changed: Bool
        if replay.isPlaying, replay.clock == .log {
            changed = replay.advanceLogClock(engine: engine)
        } else {
            replay.tick()
            changed = engine.tick()
        }
        if changed {
            latestFrame = engine.makeImage()
            dockTile.markDirty()
        }
        dockTile.pushIfDue(latestFrame)

        robotWindow.updateMousePassThrough()
        if robotWindow.needsUpdates {
            if robotFrameChangeCount != engine.changeCount, let frame = latestFrame {
                robotWindow.view.show(frame: frame)
                robotFrameChangeCount = engine.changeCount
            }
            robotWindow.view.updateArms(commandedDegrees: engine.armOffsetDegrees, nowMs: MonotonicClock.nowMs())
        }
        DebugDump.shared?.tick(engine: engine, dockTile: dockTile, robotWindow: robotWindow, status: status.text)
    }

    // MARK: Sleep and wake

    private func observeSleepAndWake() {
        let center = NSWorkspace.shared.notificationCenter
        center.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.systemWillSleep() }
        }
        center.addObserver(forName: NSWorkspace.didWakeNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.systemDidWake() }
        }
    }

    /// Stop drawing and close the socket cleanly; the firmware sees an
    /// ordinary disconnect, as the robot would if the bridge vanished.
    private func systemWillSleep() {
        isAsleep = true
        displayLink?.isPaused = true
        if !replay.isPlaying { socket.stop() }
        DebugDump.shared?.event("system will sleep")
    }

    /// Reconnect straight away rather than waiting for the heartbeat to
    /// notice the old socket is dead.
    private func systemDidWake() {
        isAsleep = false
        displayLink?.isPaused = false
        if !replay.isPlaying { connectLive() }
        DebugDump.shared?.event("system did wake")
    }

    // MARK: Frames and connection

    private func handleFrame(_ text: String) {
        if let signal = BridgeSignal.from(frame: text) {
            attention.handle(signal, preferences: preferences)
        }
        engine.inject(text)
    }

    private func connectLive() {
        socket.start(preferences.endpoint)
        updateStatus()
    }

    private func updateStatus() {
        let place = preferences.endpoint.displayName
        var connected = false
        let text: String
        if replay.isPlaying {
            text = "Replaying \(replay.name)"
        } else {
            switch socket.status {
            case .stopped: text = "Not connected"
            case .notConfigured: text = "Not configured: add the bridge token in Settings"
            case .connecting: text = "Connecting to \(place)…"
            case .connected:
                text = "Connected to \(place)"
                connected = true
            case .unauthorized: text = "\(place) rejected the token"
            case .waitingToRetry(let reason): text = "Can't reach \(place): \(reason)"
            }
        }
        status.text = text
        status.isConnected = connected

        // Keep App Nap from throttling the animation while there is
        // something to animate.
        let busy = socket.isOpen || replay.isPlaying
        if busy, activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiatedAllowingIdleSystemSleep], reason: "Animating the robot's face in the Dock")
        } else if !busy, let current = activity {
            ProcessInfo.processInfo.endActivity(current)
            activity = nil
        }
    }

    // MARK: Robot window

    private func setRobotWindowVisible(_ visible: Bool) {
        if visible, !robotWindow.isVisible {
            robotWindow.show()
            robotFrameChangeCount = .max
        } else if !visible, robotWindow.isVisible {
            robotWindow.hide()
        }
    }

    @objc func toggleRobotWindow(_ sender: Any?) {
        preferences.robotWindowVisible = !robotWindow.isVisible
    }

    @objc func toggleRobotOnTop(_ sender: Any?) {
        preferences.robotWindowOnTop.toggle()
    }

    /// Size presets; the menu item's tag is the width in points.
    @objc func setRobotSize(_ sender: NSMenuItem) {
        robotWindow.setSide(CGFloat(sender.tag))
    }

    /// The robot has no window chrome, so this right-click menu is how it's
    /// sized, pinned and put away.
    private func robotContextMenu() -> NSMenu {
        let menu = NSMenu()
        let onTop = menu.addItem(withTitle: "Keep on Top", action: #selector(toggleRobotOnTop(_:)), keyEquivalent: "")
        onTop.target = self
        onTop.state = preferences.robotWindowOnTop ? .on : .off
        menu.addItem(robotSizeItem(title: "Size"))
        menu.addItem(.separator())
        menu.addItem(withTitle: "Hide Robot", action: #selector(toggleRobotWindow(_:)), keyEquivalent: "").target = self
        menu.addItem(withTitle: "Settings…", action: #selector(showSettings(_:)), keyEquivalent: "").target = self
        return menu
    }

    fileprivate func robotSizeItem(title: String) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        let sizes = NSMenu(title: title)
        for (name, side) in RobotWindowController.sizes {
            let size = sizes.addItem(withTitle: name, action: #selector(setRobotSize(_:)), keyEquivalent: "")
            size.target = self
            size.tag = Int(side)
            size.state = abs(robotWindow.side - side) < 1 ? .on : .off
        }
        sizes.addItem(.separator())
        let hint = sizes.addItem(withTitle: "Pinch the robot to resize freely", action: nil, keyEquivalent: "")
        hint.isEnabled = false
        item.submenu = sizes
        return item
    }

    // MARK: Settings and replay

    @objc func showSettings(_ sender: Any?) {
        if settingsWindow == nil {
            let view = SettingsView(preferences: preferences, status: status) { [weak self] in
                guard let self else { return }
                if self.replay.isPlaying { self.replay.stop() } else { self.connectLive() }
            }
            let window = NSWindow(contentViewController: NSHostingController(rootView: view))
            window.title = "Sultana Dock Settings"
            window.styleMask = [.titled, .closable]
            window.isReleasedWhenClosed = false
            window.center()
            settingsWindow = window
        }
        NSApp.unhide(nil)
        NSApp.activate()
        settingsWindow?.makeKeyAndOrderFront(nil)
    }

    @objc func openReplay(_ sender: Any?) {
        NSApp.unhide(nil)
        NSApp.activate()
        let panel = NSOpenPanel()
        panel.message = "Choose a bridge recording (.jsonl, from scripts/record-ws.mjs)."
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        startReplay(url)
    }

    private func startReplay(_ url: URL, clock: ReplayController.Clock = .wall) {
        do {
            let entries = try ReplayController.load(url)
            socket.stop()
            if clock == .log {
                // Same starting point as sultana-replay: core initialised at
                // t=0 with seed 1, every pushed frame hashed.
                engine.manualClockMs = 0
                engine.frameHash = SultanaEngine.frameHashSeed
                engine.reset(seed: 1)
            } else {
                engine.reset()
            }
            replay.start(entries, name: url.lastPathComponent, clock: clock)
            updateStatus()
            DebugDump.shared?.event("replay started \(url.lastPathComponent) (\(clock.rawValue) clock)")
        } catch {
            let alert = NSAlert()
            alert.messageText = "Can't replay \(url.lastPathComponent)"
            alert.informativeText = error.localizedDescription
            alert.runModal()
        }
    }

    @objc func stopReplay(_ sender: Any?) {
        replay.stop()
    }

    private func replayFinished() {
        if replay.clock == .log, let hash = engine.frameHash {
            let summary = String(format: "frame-hash %016llx (%u frames)", hash, engine.framesPushed)
            Log.app.info("replay finished (log clock): \(summary, privacy: .public)")
            DebugDump.shared?.event("replay finished (log clock): \(summary)")
        } else {
            DebugDump.shared?.event("replay finished")
        }
        engine.frameHash = nil
        engine.manualClockMs = nil
        engine.reset(seed: UInt32.random(in: 1...UInt32.max))
        connectLive()
    }
}

extension AppDelegate: BridgeSocketDelegate {
    func bridgeSocket(_ socket: BridgeSocket, didChangeStatus status: BridgeSocket.Status) {
        updateStatus()
    }

    func bridgeSocketDidOpen(_ socket: BridgeSocket) {
        engine.setConnected(true)
        DebugDump.shared?.event("connected to \(socket.endpoint?.displayName ?? "?")")
        updateStatus()
    }

    func bridgeSocketDidClose(_ socket: BridgeSocket) {
        engine.setConnected(false)
        DebugDump.shared?.event("disconnected")
        updateStatus()
    }

    func bridgeSocket(_ socket: BridgeSocket, didReceive text: String) {
        guard !replay.isPlaying else { return }
        handleFrame(text)
    }
}

extension AppDelegate: NSMenuItemValidation {
    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        switch menuItem.action {
        case #selector(stopReplay(_:)):
            return replay.isPlaying
        case #selector(toggleRobotWindow(_:)):
            menuItem.title = robotWindow.isVisible ? "Hide Robot" : "Show Robot"
            return true
        case #selector(toggleRobotOnTop(_:)):
            menuItem.state = preferences.robotWindowOnTop ? .on : .off
            return true
        case #selector(setRobotSize(_:)):
            menuItem.state = abs(robotWindow.side - CGFloat(menuItem.tag)) < 1 ? .on : .off
            return true
        default:
            return true
        }
    }
}

enum MainMenu {
    @MainActor
    static func build(target: AppDelegate) -> NSMenu {
        let main = NSMenu()

        let app = NSMenu(title: "Sultana Dock")
        app.addItem(
            withTitle: "About Sultana Dock", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
            keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Settings…", action: #selector(AppDelegate.showSettings(_:)), keyEquivalent: ",")
            .target = target
        app.addItem(.separator())
        app.addItem(withTitle: "Replay Log…", action: #selector(AppDelegate.openReplay(_:)), keyEquivalent: "o")
            .target = target
        app.addItem(withTitle: "Stop Replay", action: #selector(AppDelegate.stopReplay(_:)), keyEquivalent: ".")
            .target = target
        app.addItem(.separator())
        app.addItem(withTitle: "Hide Sultana Dock", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        app.addItem(
            withTitle: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h"
        ).keyEquivalentModifierMask = [.command, .option]
        app.addItem(withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Quit Sultana Dock", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        main.addItem(withTitle: "Sultana Dock", action: nil, keyEquivalent: "").submenu = app

        let edit = NSMenu(title: "Edit")
        edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z").keyEquivalentModifierMask = [
            .command, .shift,
        ]
        edit.addItem(.separator())
        edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        main.addItem(withTitle: "Edit", action: nil, keyEquivalent: "").submenu = edit

        let window = NSMenu(title: "Window")
        window.addItem(withTitle: "Show Robot", action: #selector(AppDelegate.toggleRobotWindow(_:)), keyEquivalent: "1")
            .target = target
        window.addItem(
            withTitle: "Keep Robot on Top", action: #selector(AppDelegate.toggleRobotOnTop(_:)), keyEquivalent: "t"
        ).keyEquivalentModifierMask = [.command, .option]
        window.items.last?.target = target
        let sizes = NSMenu(title: "Robot Size")
        for (name, side) in RobotWindowController.sizes {
            let size = sizes.addItem(withTitle: name, action: #selector(AppDelegate.setRobotSize(_:)), keyEquivalent: "")
            size.target = target
            size.tag = Int(side)
        }
        window.addItem(withTitle: "Robot Size", action: nil, keyEquivalent: "").submenu = sizes
        window.addItem(.separator())
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        window.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        main.addItem(withTitle: "Window", action: nil, keyEquivalent: "").submenu = window
        NSApp.windowsMenu = window

        return main
    }
}
