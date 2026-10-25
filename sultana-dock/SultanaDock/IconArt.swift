import CoreGraphics

/// Sultana's app icon: the robot's face filling a standard macOS squircle.
/// The firmware's round screen is drawn larger than the squircle and
/// cropped by it, so its black reaches every edge and corner and none of
/// the body shows around it. What's lost is the screen's rim: the mood
/// ring, except for slivers in the corners.
///
/// Shared by the live Dock tile (DockTile.swift) and the static app icon
/// (tools/make_app_icon.swift), so the two always match. CoreGraphics only.
/// All lengths are on Apple's 1024-point icon grid and scale with the size.
enum IconArt {
    static let grid: CGFloat = 1024
    /// The squircle: 824 points square, centred, with continuous corners.
    static let shapeSide: CGFloat = 824
    static let cornerRadius: CGFloat = 185.4
    /// The screen's diameter as a multiple of the squircle's side. The
    /// corners are 0.615 sides from the centre, so anything over 1.23
    /// covers the squircle; this crops a little further, as in the design,
    /// showing the screen out to 78% of its radius at the middle of each
    /// edge and 96% in the corners.
    static let screenScale: CGFloat = 1.285
    static var screenDiameter: CGFloat { shapeSide * screenScale }

    /// The drop shadow under the squircle, as on standard macOS icons.
    static let shadowOffsetY: CGFloat = -10
    static let shadowBlur: CGFloat = 20
    static let shadowAlpha: CGFloat = 0.28

    /// The screen is drawn with nearest-neighbour pixels only when enlarged
    /// at least this much; below it, it's smoothed. At 1.1x, nearest
    /// neighbour doubles every tenth pixel, which shows on the eyes' thin
    /// outlines.
    static let crispUpscale: CGFloat = 2

    // MARK: Geometry (in a size x size square, origin bottom-left)

    static func shapeRect(size: CGFloat) -> CGRect {
        let side = shapeSide * size / grid
        return CGRect(x: (size - side) / 2, y: (size - side) / 2, width: side, height: side)
    }

    static func screenRect(size: CGFloat) -> CGRect {
        let side = screenDiameter * size / grid
        return CGRect(x: (size - side) / 2, y: (size - side) / 2, width: side, height: side)
    }

    static func shapePath(size: CGFloat) -> CGPath {
        squircle(in: shapeRect(size: size), radius: cornerRadius * size / grid)
    }

    /// A rounded rect with Apple's continuous ("squircle") corners: each
    /// corner's curve starts 1.528665 radii from the corner, as UIKit's
    /// rounded rects and macOS icon shapes do.
    static func squircle(in rect: CGRect, radius: CGFloat) -> CGPath {
        let reach: CGFloat = 1.52866483
        let r = min(radius, min(rect.width, rect.height) / 2 / reach)
        // Corners in drawing order, each with unit vectors pointing back
        // along the edge we arrive on (a) and along the edge we leave on (b).
        let corners: [(CGPoint, CGVector, CGVector)] = [
            (CGPoint(x: rect.maxX, y: rect.maxY), CGVector(dx: -1, dy: 0), CGVector(dx: 0, dy: -1)),
            (CGPoint(x: rect.maxX, y: rect.minY), CGVector(dx: 0, dy: 1), CGVector(dx: -1, dy: 0)),
            (CGPoint(x: rect.minX, y: rect.minY), CGVector(dx: 1, dy: 0), CGVector(dx: 0, dy: 1)),
            (CGPoint(x: rect.minX, y: rect.maxY), CGVector(dx: 0, dy: -1), CGVector(dx: 1, dy: 0)),
        ]
        func point(_ corner: (CGPoint, CGVector, CGVector), _ u: CGFloat, _ v: CGFloat) -> CGPoint {
            let (c, a, b) = corner
            return CGPoint(x: c.x + (a.dx * u + b.dx * v) * r, y: c.y + (a.dy * u + b.dy * v) * r)
        }
        let path = CGMutablePath()
        path.move(to: point(corners[3], 0, reach))
        for corner in corners {
            path.addLine(to: point(corner, reach, 0))
            path.addCurve(
                to: point(corner, 0.66993427, 0.06549600), control1: point(corner, 1.08849323, 0),
                control2: point(corner, 0.86840689, 0))
            path.addLine(to: point(corner, 0.63149399, 0.07491100))
            path.addCurve(
                to: point(corner, 0.07491100, 0.63149399), control1: point(corner, 0.37282392, 0.16905899),
                control2: point(corner, 0.16905899, 0.37282392))
            path.addCurve(
                to: point(corner, 0, reach),
                control1: point(corner, 0, 0.86840689), control2: point(corner, 0, 1.08849323))
        }
        path.closeSubpath()
        return path
    }

    // MARK: Drawing

    /// The body: the shadow and the squircle, black like the screen that
    /// covers it (and all that shows before the first frame). Static, so
    /// the Dock tile caches it.
    static func drawBody(in context: CGContext, size: CGFloat) {
        let scale = size / grid
        context.saveGState()
        context.setShadow(
            offset: CGSize(width: 0, height: shadowOffsetY * scale), blur: shadowBlur * scale,
            color: CGColor(gray: 0, alpha: shadowAlpha))
        context.addPath(shapePath(size: size))
        context.setFillColor(CGColor(gray: 0, alpha: 1))
        context.fillPath()
        context.restoreGState()
    }

    /// The face: the screen, cropped to the squircle. `pixelScale` is device
    /// pixels per point, used to decide between crisp OLED pixels and
    /// smoothing (thin lines would drop out or go uneven otherwise).
    static func drawFace(_ screen: CGImage?, in context: CGContext, size: CGFloat, pixelScale: CGFloat) {
        guard let screen else { return }
        let screenRect = screenRect(size: size)
        context.saveGState()
        context.addPath(shapePath(size: size))
        context.clip()
        context.addEllipse(in: screenRect)
        context.clip()
        context.interpolationQuality =
            screenRect.width * pixelScale >= CGFloat(screen.width) * crispUpscale ? .none : .high
        context.draw(screen, in: screenRect)
        context.restoreGState()
    }

    /// The whole icon.
    static func draw(_ screen: CGImage?, in context: CGContext, size: CGFloat, pixelScale: CGFloat) {
        drawBody(in: context, size: size)
        drawFace(screen, in: context, size: size, pixelScale: pixelScale)
    }
}
