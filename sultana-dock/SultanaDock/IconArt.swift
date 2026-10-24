import CoreGraphics

/// Sultana Dock's icon: the robot's body as a standard macOS squircle,
/// white fading to light grey toward its edges, with the face (the
/// firmware's OLED screen) set into it as large as fits, inside the same
/// thin #ECECEC border as the desktop robot.
///
/// Shared by the live Dock tile (DockTile.swift) and the static app icon
/// (tools/make_app_icon.swift), so the two always match. CoreGraphics only.
/// All lengths are on Apple's 1024-point icon grid and scale with the size.
enum IconArt {
    static let grid: CGFloat = 1024
    /// The squircle: 824 points square, centred, with continuous corners.
    static let shapeSide: CGFloat = 824
    static let cornerRadius: CGFloat = 185.4
    /// Body visible between the face and the squircle's straight edges.
    static let faceMargin: CGFloat = 40
    static var faceDiameter: CGFloat { shapeSide - 2 * faceMargin }
    /// Same proportion as the desktop robot's face border (4 of 199.84).
    static var faceBorderWidth: CGFloat { faceDiameter * 4 / 199.84 }
    static let faceBorderColor = CGColor(gray: 0xEC / 255, alpha: 1)

    /// Radial body shading from the centre: white until just outside the
    /// face, then light grey toward the edges and corners. Locations are in
    /// points from the centre on the 1024 grid.
    static let bodyStops: [(distance: CGFloat, gray: CGFloat)] = [
        (0, 1),
        (376, 1),
        (412, 0xDC / 255.0),
        (506, 0xC4 / 255.0),
    ]

    /// The drop shadow under the squircle, as on standard macOS icons.
    static let shadowOffsetY: CGFloat = -10
    static let shadowBlur: CGFloat = 20
    static let shadowAlpha: CGFloat = 0.28

    // MARK: Geometry (in a size x size square, origin bottom-left)

    static func shapeRect(size: CGFloat) -> CGRect {
        let side = shapeSide * size / grid
        return CGRect(x: (size - side) / 2, y: (size - side) / 2, width: side, height: side)
    }

    static func faceRect(size: CGFloat) -> CGRect {
        let side = faceDiameter * size / grid
        return CGRect(x: (size - side) / 2, y: (size - side) / 2, width: side, height: side)
    }

    static func screenRect(size: CGFloat) -> CGRect {
        let inset = faceBorderWidth * size / grid
        return faceRect(size: size).insetBy(dx: inset, dy: inset)
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

    /// The body: shadow, squircle and its shading. Static, so the Dock tile
    /// caches it.
    static func drawBody(in context: CGContext, size: CGFloat) {
        let scale = size / grid
        let shape = squircle(in: shapeRect(size: size), radius: cornerRadius * scale)

        context.saveGState()
        context.setShadow(
            offset: CGSize(width: 0, height: shadowOffsetY * scale), blur: shadowBlur * scale,
            color: CGColor(gray: 0, alpha: shadowAlpha))
        context.addPath(shape)
        context.setFillColor(CGColor(gray: 1, alpha: 1))
        context.fillPath()
        context.restoreGState()

        context.saveGState()
        context.addPath(shape)
        context.clip()
        let endRadius = bodyStops.last!.distance * scale
        let colors = bodyStops.map { CGColor(gray: $0.gray, alpha: 1) } as CFArray
        let locations = bodyStops.map { $0.distance * scale / endRadius }
        if let gradient = CGGradient(
            colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors, locations: locations)
        {
            let centre = CGPoint(x: size / 2, y: size / 2)
            context.drawRadialGradient(
                gradient, startCenter: centre, startRadius: 0, endCenter: centre, endRadius: endRadius,
                options: [.drawsAfterEndLocation])
        }
        context.restoreGState()
    }

    /// The face: border ring and the screen. `pixelScale` is device pixels
    /// per point, used to keep the OLED pixels crisp when upscaling and
    /// smooth them when downscaling (thin lines would drop out otherwise).
    static func drawFace(_ screen: CGImage?, in context: CGContext, size: CGFloat, pixelScale: CGFloat) {
        context.setFillColor(faceBorderColor)
        context.fillEllipse(in: faceRect(size: size))

        let screenRect = screenRect(size: size)
        context.saveGState()
        context.addEllipse(in: screenRect)
        context.clip()
        context.setFillColor(CGColor(gray: 0, alpha: 1))
        context.fill(screenRect)
        if let screen {
            context.interpolationQuality =
                screenRect.width * pixelScale >= CGFloat(screen.width) ? .none : .high
            context.draw(screen, in: screenRect)
        }
        context.restoreGState()
    }

    /// The whole icon.
    static func draw(_ screen: CGImage?, in context: CGContext, size: CGFloat, pixelScale: CGFloat) {
        drawBody(in: context, size: size)
        drawFace(screen, in: context, size: size, pixelScale: pixelScale)
    }
}
