import AppKit

/// Developer aid: when SULTANA_DEBUG_DIR is set, writes the Dock tile as
/// drawn (dock.png), the robot window while it's open (robot.png), the
/// Settings window while it's open (settings.png; SULTANA_DEBUG_SETTINGS=1
/// opens it at launch) and a state snapshot (state.json) once a second, and
/// appends connection, bridge, bounce and replay events to events.log.
@MainActor
final class DebugDump {
    static let shared: DebugDump? = {
        guard let dir = ProcessInfo.processInfo.environment["SULTANA_DEBUG_DIR"], !dir.isEmpty else { return nil }
        return DebugDump(directory: URL(fileURLWithPath: dir, isDirectory: true))
    }()

    private let directory: URL
    private var lastWriteMs: UInt32 = 0
    private var lastDockPushCount: UInt64 = 0

    private init(directory: URL) {
        self.directory = directory
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        try? Data().write(to: directory.appendingPathComponent("events.log"))
        Log.app.info("debug dump enabled: \(directory.path, privacy: .public)")
    }

    func event(_ line: String) {
        let stamp = String(format: "%9.3f", Double(MonotonicClock.nowMs()) / 1000)
        guard let data = "\(stamp) \(line)\n".data(using: .utf8),
            let handle = try? FileHandle(forWritingTo: directory.appendingPathComponent("events.log"))
        else { return }
        handle.seekToEndOfFile()
        handle.write(data)
        try? handle.close()
    }

    func tick(
        engine: SultanaEngine, dockTile: DockTileController, robotWindow: RobotWindowController,
        settingsWindow: NSWindow?, status: String
    ) {
        let now = MonotonicClock.nowMs()
        guard now &- lastWriteMs >= 1000 else { return }
        let elapsed = lastWriteMs == 0 ? 1 : Double(now &- lastWriteMs) / 1000
        let dockFps = Double(dockTile.pushCount - lastDockPushCount) / elapsed
        lastWriteMs = now
        lastDockPushCount = dockTile.pushCount

        let state = """
            {"status": \(Self.quote(status)), "dock_pushes": \(dockTile.pushCount), \
            "dock_fps": \(String(format: "%.1f", dockFps)), "core": \(engine.stateJSON())}

            """
        try? state.data(using: .utf8)?.write(to: directory.appendingPathComponent("state.json"))
        if let png = dockTile.snapshotPNG() {
            try? png.write(to: directory.appendingPathComponent("dock.png"))
        }
        if robotWindow.isVisible, let png = robotWindow.view.snapshotPNG() {
            try? png.write(to: directory.appendingPathComponent("robot.png"))
        }
        if let view = settingsWindow?.contentView, settingsWindow?.isVisible == true,
            let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds)
        {
            view.cacheDisplay(in: view.bounds, to: rep)
            try? rep.representation(using: .png, properties: [:])?.write(
                to: directory.appendingPathComponent("settings.png"))
        }
    }

    private static func quote(_ s: String) -> String {
        let data = try? JSONSerialization.data(withJSONObject: [s], options: [.fragmentsAllowed])
        let array = data.flatMap { String(data: $0, encoding: .utf8) } ?? "[\"\"]"
        return String(array.dropFirst().dropLast())
    }
}
