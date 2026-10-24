import Foundation

/// Runs the bridge embedded in the app (Contents/Helpers/sultana-bridge, a
/// Node runtime, running Contents/Resources/bridge/bridge.mjs; see
/// sultana-dock/bridge/embed-bridge.sh) as a child process and keeps it
/// running.
///
/// The bridge runs with --managed: it prints one JSON line on stdout once
/// it's listening, and exits when its stdin closes. The app holds the write
/// end of that pipe, so however the app ends (quit, crash, force quit) the
/// bridge follows and frees the port. Exit status 3 means the port was taken,
/// which is retried every 10 s; any other exit restarts it after 1, 2, 4… up
/// to 30 s. Its stderr goes to ~/Library/Logs/Sultana/bridge.log.
@MainActor
final class BuiltInBridge {
    struct Configuration: Equatable {
        /// Address to listen on: 127.0.0.1, or 0.0.0.0 to let robots connect.
        var host: String
        var port: Int
        var token: String
        var turnLogs: Bool
    }

    enum Status: Equatable {
        case stopped
        case starting
        case running
        case portInUse
        /// It exited or couldn't launch, and will be restarted.
        case failed(String)
        /// This build has no bridge to run.
        case unavailable
    }

    static let exitPortInUse: Int32 = 3
    static let portInUseRetryDelay: TimeInterval = 10
    static let maxRestartDelay: TimeInterval = 30
    /// A bridge that ran this long before exiting restarts without backoff.
    static let healthyUptime: TimeInterval = 60
    static let logRotationBytes = 2_000_000

    static let logURL = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("Logs/Sultana/bridge.log")
    /// The bridge's working directory; turn logs go in logs/ under it.
    static let dataDirectory = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("Sultana/Bridge", isDirectory: true)

    static var runtimeURL: URL? {
        let url = Bundle.main.bundleURL.appendingPathComponent("Contents/Helpers/sultana-bridge")
        return FileManager.default.isExecutableFile(atPath: url.path) ? url : nil
    }
    static var scriptURL: URL? { Bundle.main.url(forResource: "bridge", withExtension: "mjs", subdirectory: "bridge") }

    var onStatusChange: ((Status) -> Void)?

    private(set) var status: Status = .stopped {
        didSet {
            guard status != oldValue else { return }
            Log.bridge.info("built-in bridge: \(String(describing: self.status), privacy: .public)")
            onStatusChange?(status)
        }
    }
    private(set) var configuration: Configuration?

    private var process: Process?
    /// Signalled from Foundation's thread when `process` exits, so stopping
    /// can wait for it without the main run loop.
    private var processExited: DispatchSemaphore?
    /// Our end of the bridge's stdin. Never written; held so it stays open.
    private var stdin: Pipe?
    private var stdoutBuffer = Data()
    private var launchedAt = Date.distantPast
    private var failures = 0
    private var restartTimer: Timer?

    /// Runs the bridge with this configuration, restarting it if it was
    /// running with another. Does nothing if it already runs with this one.
    func start(_ configuration: Configuration) {
        guard configuration != self.configuration || status == .stopped else { return }
        stopAndWait()
        self.configuration = configuration
        failures = 0
        launch()
    }

    /// Retries now rather than at the next 10 s mark, for when whatever held
    /// the port looks to have gone.
    func retryIfPortInUse() {
        guard status == .portInUse, restartTimer != nil else { return }
        restartTimer?.invalidate()
        restartTimer = nil
        launch()
    }

    /// Stops the bridge and waits briefly for it to exit, so the port is free
    /// when this returns.
    func stopAndWait(timeout: TimeInterval = 1) {
        configuration = nil
        restartTimer?.invalidate()
        restartTimer = nil
        stdin = nil
        if let running = process, let exited = processExited {
            process = nil
            processExited = nil
            running.terminate()
            if exited.wait(timeout: .now() + timeout) == .timedOut {
                kill(running.processIdentifier, SIGKILL)
            }
        }
        status = .stopped
    }

    private func launch() {
        guard let configuration else { return }
        guard let runtime = Self.runtimeURL, let script = Self.scriptURL else {
            status = .unavailable
            return
        }
        try? FileManager.default.createDirectory(at: Self.dataDirectory, withIntermediateDirectories: true)

        let child = Process()
        child.executableURL = runtime
        child.arguments =
            [script.path, "--managed", "--host", configuration.host, "--port", String(configuration.port)]
            + (configuration.turnLogs ? ["--turn-logs", "--data-dir", Self.dataDirectory.path] : [])
        // Nothing from the environment the app was started from (a shell's
        // BRIDGE_LOG_FILE or Firebase settings) reaches the bridge.
        child.environment = [
            "BRIDGE_TOKEN": configuration.token,
            "HOME": NSHomeDirectory(),
            "PATH": "/usr/bin:/bin",
            "TMPDIR": NSTemporaryDirectory(),
        ]
        child.currentDirectoryURL = Self.dataDirectory
        let stdinPipe = Pipe()
        let stdoutPipe = Pipe()
        child.standardInput = stdinPipe
        child.standardOutput = stdoutPipe
        child.standardError =
            Self.openLog(noting: "starting \(child.arguments?.joined(separator: " ") ?? "")") ?? FileHandle.nullDevice
        stdoutPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            if data.isEmpty { handle.readabilityHandler = nil }
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.received(data, from: child) }
            }
        }
        let exited = DispatchSemaphore(value: 0)
        child.terminationHandler = { [weak self] ended in
            exited.signal()
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.ended(ended) }
            }
        }

        stdoutBuffer.removeAll()
        do {
            try child.run()
        } catch {
            Log.bridge.error("can't launch the bridge: \(error.localizedDescription, privacy: .public)")
            failures += 1
            status = .failed("couldn't start: \(error.localizedDescription)")
            scheduleRestart(after: restartDelay())
            return
        }
        process = child
        processExited = exited
        stdin = stdinPipe
        launchedAt = Date()
        status = .starting
    }

    /// Watches stdout for the line the bridge prints once it's listening.
    private func received(_ data: Data, from child: Process) {
        guard child === process else { return }
        stdoutBuffer.append(data)
        while let newline = stdoutBuffer.firstIndex(of: UInt8(ascii: "\n")) {
            let line = stdoutBuffer[stdoutBuffer.startIndex..<newline]
            stdoutBuffer.removeSubrange(stdoutBuffer.startIndex...newline)
            if let message = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
                message["event"] as? String == "listening"
            {
                status = .running
            }
        }
    }

    private func ended(_ child: Process) {
        guard child === process else { return }  // stopped on purpose
        process = nil
        processExited = nil
        stdin = nil
        let code = child.terminationStatus
        if child.terminationReason == .exit && code == Self.exitPortInUse {
            status = .portInUse
            scheduleRestart(after: Self.portInUseRetryDelay)
            return
        }
        if Date().timeIntervalSince(launchedAt) >= Self.healthyUptime { failures = 0 }
        failures += 1
        let reason =
            child.terminationReason == .uncaughtSignal ? "was stopped by signal \(code)" : "exited with status \(code)"
        try? Self.openLog(noting: "the bridge \(reason)")?.close()
        status = .failed(reason)
        scheduleRestart(after: restartDelay())
    }

    private func restartDelay() -> TimeInterval {
        min(Self.maxRestartDelay, pow(2, Double(max(failures, 1) - 1)))
    }

    private func scheduleRestart(after delay: TimeInterval) {
        restartTimer?.invalidate()
        restartTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                self.restartTimer = nil
                self.launch()
            }
        }
    }

    /// The bridge log, opened for appending with a dated note, after moving
    /// it to bridge.log.1 if it has grown too big.
    private static func openLog(noting note: String) -> FileHandle? {
        let fileManager = FileManager.default
        let url = logURL
        try? fileManager.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let size = (try? fileManager.attributesOfItem(atPath: url.path))?[.size] as? Int, size > logRotationBytes {
            let previous = url.appendingPathExtension("1")
            try? fileManager.removeItem(at: previous)
            try? fileManager.moveItem(at: url, to: previous)
        }
        if !fileManager.fileExists(atPath: url.path) {
            fileManager.createFile(atPath: url.path, contents: nil)
        }
        guard let handle = try? FileHandle(forWritingTo: url) else { return nil }
        handle.seekToEndOfFile()
        let stamp = ISO8601DateFormatter().string(from: Date())
        handle.write(Data("[\(stamp)] [sultana-dock] \(note)\n".utf8))
        return handle
    }
}
