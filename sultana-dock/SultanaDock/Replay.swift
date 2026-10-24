import Foundation

/// Plays a recorded bridge session (JSONL, as written by
/// scripts/record-ws.mjs and read by robot_v3/host's sultana-replay) into
/// the app in real time, instead of the live bridge.
///
/// Two clocks:
/// - `.wall`: entries are delivered at their `t` offsets and the app keeps
///   ticking the firmware on its own clock, as it does live.
/// - `.log`: the replay owns the firmware's clock. It advances it in 1 ms
///   steps from 0, delivering due entries before each tick, which is
///   exactly what sultana-replay does. With the same seed, every frame is
///   byte-identical to the CLI's (checked with the frame hash).
@MainActor
final class ReplayController {
    enum Clock: String {
        case wall, log
    }

    struct Entry {
        enum Kind {
            case frame(String)
            case connected(Bool)
        }
        let t: UInt32
        let kind: Kind
    }

    struct LoadError: LocalizedError {
        let line: Int
        let message: String
        var errorDescription: String? { "Line \(line): \(message)" }
    }

    /// How long to keep going after the last entry, so overlays finish.
    static let tailMs: UInt32 = 4000

    var onFrame: ((String) -> Void)?
    var onConnected: ((Bool) -> Void)?
    var onFinish: (() -> Void)?

    private(set) var isPlaying = false
    private(set) var name = ""
    private(set) var clock: Clock = .wall
    /// The simulated time of the next step, in log-clock mode.
    private var logClockMs: UInt32 = 0
    private var entries: [Entry] = []
    private var next = 0
    private var startMs: UInt32 = 0
    private var endMs: UInt32 = 0

    static func load(_ url: URL) throws -> [Entry] {
        let text = try String(contentsOf: url, encoding: .utf8)
        var entries: [Entry] = []
        for (index, raw) in text.split(separator: "\n", omittingEmptySubsequences: false).enumerated() {
            let line = raw.trimmingCharacters(in: .whitespacesAndNewlines)
            if line.isEmpty || line.hasPrefix("#") { continue }
            guard let object = try? JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any] else {
                throw LoadError(line: index + 1, message: "not a JSON object")
            }
            guard let t = (object["t"] as? NSNumber)?.uint32Value else {
                throw LoadError(line: index + 1, message: "missing \"t\"")
            }
            if let last = entries.last, t < last.t {
                throw LoadError(line: index + 1, message: "\"t\" goes backwards")
            }
            if let connected = object["connected"] as? Bool {
                entries.append(Entry(t: t, kind: .connected(connected)))
            } else if let frame = object["frame"], JSONSerialization.isValidJSONObject(frame),
                let data = try? JSONSerialization.data(withJSONObject: frame),
                let frameText = String(data: data, encoding: .utf8)
            {
                entries.append(Entry(t: t, kind: .frame(frameText)))
            } else if let frameText = object["text"] as? String {
                entries.append(Entry(t: t, kind: .frame(frameText)))
            } else {
                throw LoadError(line: index + 1, message: "needs \"frame\", \"text\" or \"connected\"")
            }
        }
        return entries
    }

    /// Starts playing. Like sultana-replay, the socket counts as connected at
    /// t=0 unless the log's first entry says otherwise.
    func start(_ entries: [Entry], name: String, clock: Clock = .wall) {
        self.entries = entries
        self.name = name
        self.clock = clock
        logClockMs = 0
        next = 0
        startMs = MonotonicClock.nowMs()
        endMs = (entries.last?.t ?? 0) + Self.tailMs
        isPlaying = true
        if case .connected = entries.first?.kind, entries.first?.t == 0 {
        } else {
            onConnected?(true)
        }
        Log.app.info("replay started: \(name, privacy: .public), \(entries.count) entries, \(clock.rawValue) clock")
    }

    /// Log-clock mode: run the firmware up to the wall time elapsed since
    /// the replay started, one millisecond at a time. Returns whether the
    /// screen changed. The caller must not tick the engine itself.
    func advanceLogClock(engine: SultanaEngine) -> Bool {
        guard isPlaying, clock == .log else { return false }
        let target = min(MonotonicClock.nowMs() &- startMs, endMs)
        var changed = false
        while isPlaying, logClockMs <= target {
            engine.manualClockMs = logClockMs
            while next < entries.count, entries[next].t <= logClockMs {
                deliver(entries[next])
                next += 1
            }
            if engine.tick() { changed = true }
            if logClockMs == endMs {
                stop()
            } else {
                logClockMs += 1
            }
        }
        return changed
    }

    /// Wall-clock mode: deliver the entries that are due.
    func tick() {
        guard isPlaying, clock == .wall else { return }
        let elapsed = MonotonicClock.nowMs() &- startMs
        while next < entries.count, entries[next].t <= elapsed {
            deliver(entries[next])
            next += 1
        }
        if elapsed >= endMs { stop() }
    }

    private func deliver(_ entry: Entry) {
        switch entry.kind {
        case .frame(let text): onFrame?(text)
        case .connected(let connected): onConnected?(connected)
        }
    }

    func stop() {
        guard isPlaying else { return }
        isPlaying = false
        Log.app.info("replay finished: \(self.name, privacy: .public)")
        onFinish?()
    }
}
