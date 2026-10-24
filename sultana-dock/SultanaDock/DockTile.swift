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

/// The app icon, live: the robot's body as a macOS squircle with the
/// firmware's screen set into it (IconArt.swift, shared with the static
/// icon). The body never changes, so it's rendered once per tile size and
/// only the screen is redrawn per frame.
final class FaceTileView: NSView {
    var image: CGImage?
    private var body: (pixels: Int, image: CGImage)?

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }
        let side = min(bounds.width, bounds.height)
        let scale = window?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2
        let tile = CGRect(x: bounds.midX - side / 2, y: bounds.midY - side / 2, width: side, height: side)

        if let body = bodyImage(pixels: Int((side * scale).rounded())) {
            context.interpolationQuality = .high
            context.draw(body, in: tile)
        }
        context.saveGState()
        context.translateBy(x: tile.minX, y: tile.minY)
        IconArt.drawFace(image, in: context, size: side, pixelScale: scale)
        context.restoreGState()
    }

    private func bodyImage(pixels: Int) -> CGImage? {
        if let body, body.pixels == pixels { return body.image }
        guard pixels > 0,
            let context = CGContext(
                data: nil, width: pixels, height: pixels, bitsPerComponent: 8, bytesPerRow: 0,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
        else { return nil }
        IconArt.drawBody(in: context, size: CGFloat(pixels))
        guard let image = context.makeImage() else { return nil }
        body = (pixels, image)
        return image
    }
}
