import AppKit

/// Draws the robot's face into the app's Dock tile. The firmware renders
/// at its own cadence; the tile is only redrawn when the pixels changed,
/// and no more often than the frame-rate cap, because every redraw is a
/// round trip to the Dock process.
@MainActor
final class DockTileController {
    let view: FaceTileView
    var framesPerSecond = 30

    private var dirty = true
    private var lastPushMs: UInt32 = 0
    private(set) var pushCount: UInt64 = 0

    init() {
        let size = NSApp.dockTile.size
        view = FaceTileView(frame: NSRect(origin: .zero, size: size))
        NSApp.dockTile.contentView = view
        Log.dock.info("dock tile size \(size.width)x\(size.height) pt")
    }

    func markDirty() { dirty = true }

    /// Redraws the tile with the latest frame if it changed and the cap allows.
    func pushIfDue(_ image: CGImage?) {
        guard dirty, let image else { return }
        let now = MonotonicClock.nowMs()
        let interval = UInt32(1000 / max(1, framesPerSecond))
        guard pushCount == 0 || now &- lastPushMs >= interval else { return }
        view.image = image
        NSApp.dockTile.display()
        lastPushMs = now
        dirty = false
        pushCount += 1
    }

    /// The tile exactly as drawn, for debugging.
    func snapshotPNG() -> Data? {
        guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return nil }
        view.cacheDisplay(in: view.bounds, to: rep)
        return rep.representation(using: .png, properties: [:])
    }
}

/// The round 240x240 screen in a thin bezel, scaled to the tile. The
/// framing matches the static app icon (tools/make_app_icon.py) and Apple's
/// icon grid, so the tile sits at the same size as its neighbours. The Dock
/// shows tiles smaller than the 240-pixel panel, so the face is smoothed
/// when downscaled (thin lines would drop out otherwise) and kept
/// pixel-sharp when upscaled.
final class FaceTileView: NSView {
    static let screenFraction: CGFloat = 0.80
    static let bezelFraction: CGFloat = 0.86
    static let bezelColor = CGColor(srgbRed: 0x23 / 255, green: 0x23 / 255, blue: 0x26 / 255, alpha: 1)

    var image: CGImage?

    override func draw(_ dirtyRect: NSRect) {
        guard let image, let context = NSGraphicsContext.current?.cgContext else { return }
        let tile = min(bounds.width, bounds.height)
        func centred(_ fraction: CGFloat) -> CGRect {
            let side = tile * fraction
            return CGRect(x: bounds.midX - side / 2, y: bounds.midY - side / 2, width: side, height: side)
        }
        context.setFillColor(Self.bezelColor)
        context.fillEllipse(in: centred(Self.bezelFraction))

        let screen = centred(Self.screenFraction)
        let scale = window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2
        context.interpolationQuality = screen.width * scale >= CGFloat(image.width) ? .none : .high
        context.saveGState()
        context.addEllipse(in: screen)
        context.clip()
        context.draw(image, in: screen)
        context.restoreGState()
    }
}
