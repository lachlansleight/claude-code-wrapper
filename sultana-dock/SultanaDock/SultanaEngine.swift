import CoreGraphics
import Foundation

/// The robot_v3 firmware's behaviour, face and motion code, running
/// in-process through SultanaCore (robot_v3/host/core/sultana_core.h).
///
/// Feed it the bridge's WebSocket text frames with `inject`, call `tick`
/// often, and read back the screen with `makeImage` and the arm angle with
/// `armOffsetDegrees`. The core is a single global instance and must only be
/// used from the main thread, so this class is too.
@MainActor
final class SultanaEngine {
    static let screenWidth = 240
    static let screenHeight = 240
    private static let pixelCount = screenWidth * screenHeight

    /// Frames the firmware sends back to the bridge (`request_sessions`).
    var onSend: ((String) -> Void)?

    /// Increments whenever the screen's pixels change.
    private(set) var changeCount: UInt64 = 0

    /// When set, every call hands the firmware this time instead of the
    /// monotonic clock. Exact (log-clock) replays drive time from the log.
    var manualClockMs: UInt32?

    /// When non-nil, every frame the firmware pushes is folded into this
    /// FNV-1a hash, exactly like `sultana-replay --frame-hash`, so a replay in
    /// the app can be checked against the CLI frame for frame.
    var frameHash: UInt64?
    static let frameHashSeed: UInt64 = 1_469_598_103_934_665_603

    /// Frames pushed since the last reset (sultana_core_frame_count).
    var framesPushed: UInt32 { sultana_core_frame_count() }

    private(set) var seed: UInt32
    private var now: UInt32 { manualClockMs ?? MonotonicClock.nowMs() }
    private var lastFrame = [UInt16](repeating: 0, count: SultanaEngine.pixelCount)
    private let bitmap: CGContext

    init(seed: UInt32 = UInt32.random(in: 1...UInt32.max)) {
        self.seed = seed
        bitmap = CGContext(
            data: nil, width: Self.screenWidth, height: Self.screenHeight, bitsPerComponent: 8,
            bytesPerRow: Self.screenWidth * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

        let me = Unmanaged.passUnretained(self).toOpaque()
        sultana_core_set_send_callback({ json, user in
            guard let json, let user else { return }
            let engine = Unmanaged<SultanaEngine>.fromOpaque(user).takeUnretainedValue()
            let text = String(cString: json)
            MainActor.assumeIsolated { engine.onSend?(text) }
        }, me)
        sultana_core_set_log_callback({ line, _ in
            guard let line else { return }
            Log.core.debug("\(String(cString: line), privacy: .public)")
        }, nil)
        sultana_core_init(MonotonicClock.nowMs(), seed)
    }

    /// Back to the state the robot boots into: asleep, disconnected.
    /// Optionally with a new RNG seed.
    func reset(seed newSeed: UInt32? = nil) {
        if let newSeed { seed = newSeed }
        sultana_core_init(now, seed)
        for i in lastFrame.indices { lastFrame[i] = 0 }
        changeCount += 1
    }

    func setConnected(_ connected: Bool) {
        sultana_core_set_connected(connected, now)
    }

    /// One received WebSocket text frame. False if it wasn't JSON.
    @discardableResult
    func inject(_ text: String) -> Bool {
        var text = text
        return text.withUTF8 { utf8 in
            utf8.withMemoryRebound(to: CChar.self) { chars in
                sultana_core_inject(chars.baseAddress, chars.count, now)
            }
        }
    }

    /// One iteration of the firmware's loop(). True if the screen changed.
    func tick() -> Bool {
        guard sultana_core_tick(now), let frame = sultana_core_frame565() else {
            return false
        }
        let bytes = Self.pixelCount * MemoryLayout<UInt16>.size
        if var hash = frameHash {
            let raw = UnsafeRawBufferPointer(start: frame, count: bytes)
            for byte in raw { hash = (hash ^ UInt64(byte)) &* 1_099_511_628_211 }
            frameHash = hash
        }
        let changed = lastFrame.withUnsafeMutableBytes { last -> Bool in
            if memcmp(last.baseAddress!, frame, bytes) == 0 { return false }
            memcpy(last.baseAddress!, frame, bytes)
            return true
        }
        if changed { changeCount += 1 }
        return changed
    }

    /// The current screen as a 240x240 image (square; the panel is round).
    func makeImage() -> CGImage? {
        guard let data = bitmap.data else { return nil }
        sultana_core_frame_rgba(data.assumingMemoryBound(to: UInt8.self))
        return bitmap.makeImage()
    }

    /// The arm offset the firmware last commanded, in degrees (±45). The
    /// robot's gear mirrors it: one arm at +θ, the other at −θ.
    var armOffsetDegrees: Int { Int(sultana_core_arm_offset_deg()) }

    /// Debug snapshot: verb, expression, valence/activation, latch, …
    func stateJSON() -> String {
        var buffer = [CChar](repeating: 0, count: 1024)
        sultana_core_state_json(&buffer, buffer.count)
        return String(cString: buffer)
    }
}
