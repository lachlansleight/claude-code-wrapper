import AppKit

/// Sultana Dock: the Sultana robot's face, live in the macOS Dock.
@main
@MainActor
enum SultanaDockApp {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.setActivationPolicy(.regular)
        withExtendedLifetime(delegate) { app.run() }
    }
}
