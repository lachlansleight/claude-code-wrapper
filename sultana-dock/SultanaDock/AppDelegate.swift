import AppKit
import Combine
import SwiftUI

/// Wires the built-in bridge, the bridge socket, the firmware core, the Dock
/// tile, the robot window and bounces together. Everything runs on the main
/// thread.
///
///   built-in bridge (child process, or an external bridge)
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
    private let builtInBridge = BuiltInBridge()
    private let claudePlugin = ClaudePlugin()
    private lazy var robotMonitor = RobotMonitor(preferences: preferences)
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
        preferences.$armColorHex
            .compactMap { CGColor.fromHex($0) }
            .sink { [weak self] color in self?.robotWindow.view.armColor = color }
            .store(in: &cancellables)
        preferences.$bodyColorHex
            .compactMap { CGColor.fromHex($0) }
            .sink { [weak self] color in self?.robotWindow.view.bodyColor = color }
            .store(in: &cancellables)
        // The size setting drives the window; this also applies it at launch.
        Publishers.CombineLatest(preferences.$robotSize, preferences.$robotCustomWidth)
            .map { size, customWidth in size.width ?? CGFloat(customWidth) }
            .removeDuplicates()
            .sink { [weak self] width in self?.robotWindow.setSide(width) }
            .store(in: &cancellables)
        robotWindow.onPinch = { [weak self] width in
            guard let self else { return }
            self.preferences.robotCustomWidth = Double(width)
            self.preferences.robotSize = .custom
        }

        // @Published sends before the value changes, hence the hop to the
        // next run loop pass before acting on the new settings.
        Publishers.Merge(
            preferences.$bridgeMode.dropFirst().map { _ in () },
            preferences.$allowNetworkRobots.dropFirst().map { _ in () }
        )
        .receive(on: RunLoop.main)
        .sink { [weak self] in
            guard let self, !self.replay.isPlaying else { return }
            self.connectLive()
        }
        .store(in: &cancellables)

        engine.onSend = { [weak self] text in self?.socket.send(text) }
        socket.delegate = self
        builtInBridge.onStatusChange = { [weak self] status in self?.builtInBridgeChanged(status) }
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
        // The first launch opens Settings ▸ Setup, where Claude Code's plugin
        // is installed and robots are paired.
        let firstLaunch = !UserDefaults.standard.bool(forKey: Self.launchedBeforeKey)
        if firstLaunch { UserDefaults.standard.set(true, forKey: Self.launchedBeforeKey) }
        if !preferences.endpoint.isComplete && !replay.isPlaying {
            showSettings(tab: .bridge)
        } else if firstLaunch {
            showSettings(tab: .setup)
        } else if let tab = env["SULTANA_DEBUG_SETTINGS"] {
            showSettings(tab: SettingsTab(rawValue: tab) ?? .general)
        }
    }

    private static let launchedBeforeKey = "launchedBefore"

    func applicationWillTerminate(_ notification: Notification) {
        socket.stop()
        builtInBridge.stopAndWait()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    /// Clicking the Dock icon brings the app forward like any other (menu
    /// bar, Settings) and shows the robot, or opens Settings when the
    /// connection needs fixing. macOS won't bounce the icon while the app is
    /// frontmost; bounces resume as soon as you switch away.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if connectionNeedsFixing {
            showSettings(tab: .bridge)
        } else {
            preferences.robotWindowVisible = true
            robotWindow.show()
        }
        return false
    }

    func applicationDockMenu(_ sender: NSApplication) -> NSMenu? {
        let menu = NSMenu()
        for line in [status.summary, status.detail].compactMap({ $0 }) {
            let item = NSMenuItem(title: line, action: nil, keyEquivalent: "")
            item.isEnabled = false
            menu.addItem(item)
        }
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
        DebugDump.shared?.tick(
            engine: engine, dockTile: dockTile, robotWindow: robotWindow, settingsWindow: settingsWindow,
            status: [status.summary, status.detail].compactMap { $0 }.joined(separator: ": "))
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

    /// Starts or stops the built-in bridge to match the settings, and
    /// (re)connects to whichever bridge they name.
    private func connectLive() {
        if preferences.mode == .builtIn {
            preferences.ensureToken()
            builtInBridge.start(preferences.builtInConfiguration)
        } else {
            builtInBridge.stopAndWait()
        }
        socket.start(preferences.endpoint)
        updateStatus()
    }

    private func builtInBridgeChanged(_ bridgeStatus: BuiltInBridge.Status) {
        DebugDump.shared?.event("built-in bridge \(bridgeStatus)")
        if bridgeStatus == .running { socket.retryNow() }
        updateStatus()
    }

    /// Clicking the Dock icon opens Settings instead of showing the robot.
    private var connectionNeedsFixing: Bool {
        if !preferences.endpoint.isComplete || socket.status == .unauthorized { return true }
        guard preferences.mode == .builtIn, socket.status != .connected else { return false }
        return builtInBridge.status == .unavailable || builtInBridge.status == .portInUse
    }

    /// A short status ("Connected", "Port in use") and, when there's more to
    /// say, a sentence of detail.
    private func connectionDescription() -> (summary: String, detail: String?, connected: Bool) {
        if replay.isPlaying { return ("Replaying", "Playing \(replay.name).", false) }
        let endpoint = preferences.endpoint
        if preferences.mode == .external {
            let place = endpoint.displayName
            switch socket.status {
            case .stopped: return ("Disconnected", nil, false)
            case .notConfigured: return ("Not set up", "Add the bridge's host, port and token.", false)
            case .connecting: return ("Connecting…", nil, false)
            case .connected: return ("Connected", nil, true)
            case .unauthorized: return ("Token rejected", "\(place) rejected the token.", false)
            case .waitingToRetry(let reason): return ("Disconnected", "Can't reach \(place): \(reason)", false)
            }
        }
        // While something else holds the port, the socket reaches that
        // instead, which works if it's a bridge that takes our token.
        let port = endpoint.port
        switch (socket.status, builtInBridge.status) {
        case (.connected, .portInUse):
            return ("Connected", "To the bridge that was already running on port \(port), not Sultana's own.", true)
        case (.connected, _): return ("Connected", nil, true)
        case (.unauthorized, .portInUse):
            return ("Token rejected", "The bridge already running on port \(port) rejected the token.", false)
        case (.unauthorized, _): return ("Token rejected", "The built-in bridge rejected the token.", false)
        case (_, .unavailable): return ("Unavailable", "This build of Sultana doesn't include the bridge.", false)
        case (_, .portInUse): return ("Port in use", "Something else is using port \(port).", false)
        case (_, .failed(let reason)): return ("Restarting…", "The bridge \(reason).", false)
        case (_, .running): return ("Connecting…", nil, false)
        case (_, .stopped), (_, .starting): return ("Starting…", nil, false)
        }
    }

    private func updateStatus() {
        let (summary, detail, connected) = connectionDescription()
        status.summary = summary
        status.detail = detail
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
    /// The Size menus' presets; the menu item's tag is the width in points.
    @objc func setRobotSize(_ sender: NSMenuItem) {
        if let preset = RobotSize.presets.first(where: { $0.width == CGFloat(sender.tag) }) {
            preferences.robotSize = preset
        }
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
        for preset in RobotSize.presets {
            let size = sizes.addItem(withTitle: preset.label, action: #selector(setRobotSize(_:)), keyEquivalent: "")
            size.target = self
            size.tag = Int(preset.width ?? 0)
            size.state = preferences.robotSize == preset ? .on : .off
        }
        sizes.addItem(.separator())
        let hint = sizes.addItem(withTitle: "Pinch the robot to resize freely", action: nil, keyEquivalent: "")
        hint.isEnabled = false
        item.submenu = sizes
        return item
    }

    // MARK: Settings and replay

    private func showSettings(tab: SettingsTab) {
        status.settingsTab = tab
        showSettings(nil)
    }

    @objc func showSettings(_ sender: Any?) {
        if settingsWindow == nil {
            let view = SettingsView(
                preferences: preferences, status: status, plugin: claudePlugin, robots: robotMonitor
            ) { [weak self] in
                guard let self else { return }
                if self.replay.isPlaying { self.replay.stop() } else { self.connectLive() }
            }
            let window = NSWindow(contentViewController: NSHostingController(rootView: view))
            window.title = "Sultana Settings"
            window.styleMask = [.titled, .closable]
            window.isReleasedWhenClosed = false
            window.center()
            settingsWindow = window
            // The robot setup steps start folded each time Settings opens.
            NotificationCenter.default.addObserver(
                forName: NSWindow.willCloseNotification, object: window, queue: .main
            ) { [weak self] _ in
                MainActor.assumeIsolated { self?.status.robotStepsExpanded = false }
            }
        }
        NSApp.unhide(nil)
        NSApp.activate()
        settingsWindow?.makeKeyAndOrderFront(nil)
    }

    @objc func showAbout(_ sender: Any?) {
        AboutPanel.show()
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
        // Lost the bridge that held the built-in bridge's port: take it over.
        if preferences.mode == .builtIn { builtInBridge.retryIfPortInUse() }
        updateStatus()
    }

    func bridgeSocket(_ socket: BridgeSocket, didReceive text: String) {
        guard !replay.isPlaying else { return }
        if BridgeSignal.agent(ofFrame: text) == "claude",
            status.lastClaudeHook.map({ Date().timeIntervalSince($0) >= 1 }) ?? true
        {
            status.lastClaudeHook = Date()
        }
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
            menuItem.state = preferences.robotSize.width == CGFloat(menuItem.tag) ? .on : .off
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

        let app = NSMenu(title: "Sultana")
        app.addItem(withTitle: "About Sultana", action: #selector(AppDelegate.showAbout(_:)), keyEquivalent: "")
            .target = target
        app.addItem(.separator())
        app.addItem(withTitle: "Settings…", action: #selector(AppDelegate.showSettings(_:)), keyEquivalent: ",")
            .target = target
        app.addItem(.separator())
        app.addItem(withTitle: "Replay Log…", action: #selector(AppDelegate.openReplay(_:)), keyEquivalent: "o")
            .target = target
        app.addItem(withTitle: "Stop Replay", action: #selector(AppDelegate.stopReplay(_:)), keyEquivalent: ".")
            .target = target
        app.addItem(.separator())
        app.addItem(withTitle: "Hide Sultana", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        app.addItem(
            withTitle: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h"
        ).keyEquivalentModifierMask = [.command, .option]
        app.addItem(withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Quit Sultana", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        main.addItem(withTitle: "Sultana", action: nil, keyEquivalent: "").submenu = app

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
        for preset in RobotSize.presets {
            let size = sizes.addItem(
                withTitle: preset.label, action: #selector(AppDelegate.setRobotSize(_:)), keyEquivalent: "")
            size.target = target
            size.tag = Int(preset.width ?? 0)
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
