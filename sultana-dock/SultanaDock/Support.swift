import Foundation
import os

/// Logging. Read with:
///   log stream --predicate 'subsystem == "com.lachlansleight.sultana.dock"'
enum Log {
    static let subsystem = Bundle.main.bundleIdentifier ?? "com.lachlansleight.sultana.dock"
    static let app = Logger(subsystem: subsystem, category: "app")
    static let bridge = Logger(subsystem: subsystem, category: "bridge")
    static let core = Logger(subsystem: subsystem, category: "core")
    static let dock = Logger(subsystem: subsystem, category: "dock")
}

/// Where the app is running from.
enum AppLocation {
    /// Somewhere that won't last: a disk image, or the random read-only copy
    /// macOS runs a quarantined app from (App Translocation). A plugin
    /// marketplace or login item registered from here would break once the
    /// image is ejected, so Settings asks for a move to Applications first.
    static var isTemporary: Bool {
        let url = Bundle.main.bundleURL
        if url.path.contains("/AppTranslocation/") { return true }
        return (try? url.resourceValues(forKeys: [.volumeIsReadOnlyKey]))?.volumeIsReadOnly ?? false
    }
}

/// Monotonic milliseconds since launch. This is the clock handed to
/// SultanaCore, which turns it into the firmware's millis(). It stops while
/// the Mac sleeps, so waking doesn't look like a long gap to the firmware.
enum MonotonicClock {
    private static let origin = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)

    static func nowMs() -> UInt32 {
        UInt32(truncatingIfNeeded: (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) &- origin) / 1_000_000)
    }
}
