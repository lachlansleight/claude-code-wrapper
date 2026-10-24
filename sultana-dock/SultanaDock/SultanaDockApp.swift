import AppKit

/// Sultana: the Sultana robot's face, live in the macOS Dock and on the desktop.
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
