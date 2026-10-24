import AppKit
import Combine
import SwiftUI

/// Wires the bridge socket, the firmware core, the Dock tile and bounces
/// together. Everything runs on the main thread.
///
///   bridge ──text frame──► BridgeSignal (bounce, any session)
///                     └──► SultanaEngine (firmware: latch, verbs, emotion, face)
///   display link ──► engine.tick ──pixels changed──► Dock tile (≤ fps cap)
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private let preferences = AppPreferences()
    private let status = AppStatus()
    private let engine = SultanaEngine()
    private let socket = BridgeSocket()
    private let attention = AttentionController()
    private let replay = ReplayController()
    private var dockTile: DockTileController!
    private var displayLink: CADisplayLink?
    private var activity: NSObjectProtocol?
    private var settingsWindow: NSWindow?
    private var cancellables: Set<AnyCancellable> = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.mainMenu = MainMenu.build(target: self)

        dockTile = DockTileController()
        preferences.$dockFramesPerSecond
            .sink { [weak self] fps in self?.dockTile.framesPerSecond = fps }
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

        // Developer aid: SULTANA_REPLAY=/path/to/log.jsonl replays a recording
        // at launch instead of connecting to the bridge.
        if let path = ProcessInfo.processInfo.environment["SULTANA_REPLAY"], !path.isEmpty {
            startReplay(URL(fileURLWithPath: path))
        } else {
            connectLive()
        }
        if preferences.endpoint.isComplete || replay.isPlaying {
            // macOS activates a freshly launched app, and an active app can't
            // bounce. With no window to show, hand focus straight back.
            DispatchQueue.main.async { self.yieldFocusIfWindowless() }
        } else {
            showSettings(nil)
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        socket.stop()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }

    /// Clicking the Dock icon. Until the robot window exists (Phase 3), this
    /// opens Settings when there is something to fix, and otherwise gives
    /// focus back so the next turn can still bounce the icon.
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !status.isConnected && !replay.isPlaying {
            showSettings(nil)
        } else {
            yieldFocusIfWindowless()
        }
        return false
    }

    /// Hides the app (which re-activates whatever was in front before) when
    /// it has no visible windows. The Dock tile keeps animating.
    private func yieldFocusIfWindowless() {
        guard NSApp.isActive, !NSApp.windows.contains(where: { $0.isVisible }) else { return }
        NSApp.hide(nil)
    }

    func applicationDockMenu(_ sender: NSApplication) -> NSMenu? {
        let menu = NSMenu()
        let statusItem = NSMenuItem(title: status.text, action: nil, keyEquivalent: "")
        statusItem.isEnabled = false
        menu.addItem(statusItem)
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
        displayLink = link
    }

    @objc private func displayLinkFired(_ link: CADisplayLink) {
        replay.tick()
        // The firmware throttles its own rendering to 33 ms (16 ms while the
        // code streams show), so ticking at the display rate is harmless.
        if engine.tick() { dockTile.markDirty() }
        dockTile.pushIfDue(from: engine)
        DebugDump.shared?.tick(engine: engine, dockTile: dockTile, status: status.text)
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

    // MARK: Actions

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

    private func startReplay(_ url: URL) {
        do {
            let entries = try ReplayController.load(url)
            socket.stop()
            engine.reset()
            replay.start(entries, name: url.lastPathComponent)
            updateStatus()
            DebugDump.shared?.event("replay started \(url.lastPathComponent)")
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
        DebugDump.shared?.event("replay finished")
        engine.reset()
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
        if menuItem.action == #selector(stopReplay(_:)) { return replay.isPlaying }
        return true
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
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        window.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        main.addItem(withTitle: "Window", action: nil, keyEquivalent: "").submenu = window
        NSApp.windowsMenu = window

        return main
    }
}
