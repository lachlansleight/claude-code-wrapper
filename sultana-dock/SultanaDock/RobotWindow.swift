import AppKit
import QuartzCore

/// The robot drawing, in design points: the units of the Figma frame
/// "SultanaExample" (file SCRATCHPAD, node 3189:9), so numbers copy straight
/// across from the design. Positions are relative to the body's centre,
/// with y pointing up.
///
/// Back to front: a body disc whose radial gradient forms a slight ring at
/// the rim, the two arms, then the face (the firmware's screen inside a thin
/// border), so the arms look like they come out of a thin body.
struct RobotLayout {
    static let standard = RobotLayout()

    /// Room for the body, the arms, their swing and the shadow.
    var canvasSize = CGSize(width: 312, height: 236)
    var aspect: CGFloat { canvasSize.width / canvasSize.height }

    /// The body disc. White out to 94% of its radius, then fading to grey at
    /// the rim (the frame's radial fill).
    var bodyDiameter: CGFloat = 215.5
    var bodyGradient: [(location: CGFloat, color: CGColor)] = [
        (0, CGColor(gray: 1, alpha: 1)),
        (0.94231, CGColor(gray: 1, alpha: 1)),
        (0.97115, CGColor(gray: 217 / 255, alpha: 1)),
        (1, CGColor(gray: 179 / 255, alpha: 1)),
    ]

    /// The face disc in front of the arms: the screen inside a thin border.
    var faceDiameter: CGFloat = 199.84
    var faceBorderWidth: CGFloat = 4
    var faceBorderColor = CGColor(gray: 0xEC / 255, alpha: 1)
    var screenDiameter: CGFloat { faceDiameter - 2 * faceBorderWidth }

    /// The right arm's outline exactly as in the design's SVG ("Vector 31"):
    /// an 88.7439 x 23.1544 box with y pointing down. Its root is hidden
    /// behind the face.
    static let armOutline: CGPath = {
        let path = CGMutablePath()
        path.move(to: CGPoint(x: 83.7439, y: 0))
        path.addLine(to: CGPoint(x: 0, y: 0))
        path.addLine(to: CGPoint(x: 0, y: 23.1544))
        path.addLine(to: CGPoint(x: 84.3906, y: 12.1452))
        path.addCurve(
            to: CGPoint(x: 88.7439, y: 7.18723), control1: CGPoint(x: 86.8809, y: 11.8203),
            control2: CGPoint(x: 88.7439, y: 9.69863))
        path.addLine(to: CGPoint(x: 88.7439, y: 5))
        path.addCurve(
            to: CGPoint(x: 83.7439, y: 0), control1: CGPoint(x: 88.7439, y: 2.23858),
            control2: CGPoint(x: 86.5053, y: 0))
        path.closeSubpath()
        return path
    }()
    /// Top-left corner of the right arm's box relative to the body centre
    /// (y up): its root's lower edge sits on the centre line. The left arm
    /// mirrors it. (In the design the left arm is 2 points further out; the
    /// robot's gear mirrors the arms, so they're drawn symmetric here.)
    var armBoxOrigin = CGPoint(x: 58.2646, y: 23.1544)
    /// The right arm's pivot: where it emerges from the face's border, half
    /// way through its thickness there. The design shows only the rest pose,
    /// so this is a choice; change it to move the hinge.
    var armPivot = CGPoint(x: 99.75, y: 14.28)
    static let defaultArmColorHex = "#8EE270"

    /// Right arm angle at servo offset 0, degrees anticlockwise (0 = as drawn).
    var restAngleDegrees: CGFloat = 0
    /// +1 if a positive firmware offset raises the arms.
    var upSign: CGFloat = 1
    /// The physical servo lags the angle the firmware commands; the drawn
    /// arms follow at this rate. The SG92R is rated at about 0.1 s per 60°.
    var armDegreesPerSecond: CGFloat = 500

    /// Soft drop shadow under the body and arms, lit from above. Not in the
    /// design; it keeps the white body visible on light desktops.
    var shadowRadius: CGFloat = 5
    var shadowOffset = CGSize(width: 0, height: -3)
    var shadowOpacity: Float = 0.3
}

/// Draws the robot with Core Animation layers on a transparent background.
/// Dragging it moves the window, pinching resizes it, right-clicking shows a
/// menu.
final class RobotView: NSView {
    let geometry: RobotLayout
    var menuProvider: (() -> NSMenu?)?
    var onMagnify: ((CGFloat) -> Void)?

    /// The arms' fill (a setting).
    var armColor: CGColor = CGColor.fromHex(RobotLayout.defaultArmColorHex)! {
        didSet {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            for arm in [rightArmShape, leftArmShape] { arm.fillColor = armColor }
            CATransaction.commit()
        }
    }

    private let bodyShadow = CALayer()
    private let body = CAGradientLayer()
    private let rightArm = CALayer()
    private let leftArm = CALayer()
    private let rightArmShape = CAShapeLayer()
    private let leftArmShape = CAShapeLayer()
    private let screen = CALayer()
    private let faceBorder = CAShapeLayer()
    private var rightArmPath: CGPath?
    private var leftArmPath: CGPath?
    private(set) var drawnAngle: CGFloat = 0
    private var lastArmUpdate: UInt32?

    init(geometry: RobotLayout = .standard) {
        self.geometry = geometry
        super.init(frame: .zero)
        wantsLayer = true
        layerContentsRedrawPolicy = .never
        let root = CALayer()
        root.backgroundColor = .clear
        layer = root

        body.type = .radial
        body.startPoint = CGPoint(x: 0.5, y: 0.5)
        body.endPoint = CGPoint(x: 1, y: 1)
        body.colors = geometry.bodyGradient.map(\.color)
        body.locations = geometry.bodyGradient.map { NSNumber(value: Double($0.location)) }
        body.masksToBounds = true

        for shadowed in [bodyShadow, rightArm, leftArm] {
            shadowed.shadowColor = CGColor(gray: 0, alpha: 1)
            shadowed.shadowOpacity = geometry.shadowOpacity
        }
        rightArm.addSublayer(rightArmShape)
        leftArm.addSublayer(leftArmShape)
        for arm in [rightArmShape, leftArmShape] { arm.fillColor = armColor }

        screen.backgroundColor = CGColor(gray: 0, alpha: 1)
        screen.magnificationFilter = .nearest
        screen.minificationFilter = .trilinear
        screen.masksToBounds = true
        screen.contentsGravity = .resize
        faceBorder.fillColor = nil
        faceBorder.strokeColor = geometry.faceBorderColor

        for part in [bodyShadow, body, rightArm, leftArm, screen, faceBorder] {
            root.addSublayer(part)
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { false }
    override var isOpaque: Bool { false }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    override var mouseDownCanMoveWindow: Bool { true }

    override func mouseDown(with event: NSEvent) {
        window?.performDrag(with: event)
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        menuProvider?()
    }

    override func magnify(with event: NSEvent) {
        onMagnify?(event.magnification)
    }

    // MARK: Geometry

    /// The canvas: the largest rect with the layout's aspect ratio, centred
    /// in the view.
    private var canvas: CGRect {
        let width = min(bounds.width, bounds.height * geometry.aspect)
        let height = width / geometry.aspect
        return CGRect(x: bounds.midX - width / 2, y: bounds.midY - height / 2, width: width, height: height)
    }
    /// View points per design point.
    private var scale: CGFloat { canvas.width / geometry.canvasSize.width }
    /// Body-relative design points (y up) to view points.
    private var designToView: CGAffineTransform {
        CGAffineTransform(translationX: canvas.midX, y: canvas.midY).scaledBy(x: scale, y: scale)
    }
    private func viewPoint(_ p: CGPoint) -> CGPoint { p.applying(designToView) }
    private func disc(diameter: CGFloat) -> CGRect {
        let side = diameter * scale
        return CGRect(x: canvas.midX - side / 2, y: canvas.midY - side / 2, width: side, height: side)
    }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)

        let bodyRect = disc(diameter: geometry.bodyDiameter)
        body.frame = bodyRect
        body.cornerRadius = bodyRect.width / 2
        bodyShadow.frame = bounds
        bodyShadow.shadowPath = CGPath(ellipseIn: bodyRect, transform: nil)
        bodyShadow.shadowRadius = geometry.shadowRadius * scale

        rightArmPath = armPath(mirrored: false)
        leftArmPath = armPath(mirrored: true)
        configureArm(rightArm, shape: rightArmShape, path: rightArmPath!, mirrored: false)
        configureArm(leftArm, shape: leftArmShape, path: leftArmPath!, mirrored: true)

        let screenRect = disc(diameter: geometry.screenDiameter)
        screen.frame = screenRect
        screen.cornerRadius = screenRect.width / 2
        screen.contentsScale = window?.backingScaleFactor ?? 2

        let border = geometry.faceBorderWidth * scale
        faceBorder.frame = bounds
        faceBorder.lineWidth = border
        faceBorder.path = CGPath(
            ellipseIn: disc(diameter: geometry.faceDiameter).insetBy(dx: border / 2, dy: border / 2), transform: nil)

        applyArmAngle()
        CATransaction.commit()
    }

    /// The arm outline in view coordinates, unrotated.
    private func armPath(mirrored: Bool) -> CGPath {
        let origin = geometry.armBoxOrigin
        // SVG box (y down) to body-relative design points (y up), mirrored for the left arm.
        var transform = CGAffineTransform(
            a: mirrored ? -1 : 1, b: 0, c: 0, d: -1, tx: mirrored ? -origin.x : origin.x, ty: origin.y
        ).concatenating(designToView)
        return RobotLayout.armOutline.copy(using: &transform) ?? RobotLayout.armOutline
    }

    private func pivot(mirrored: Bool) -> CGPoint {
        viewPoint(CGPoint(x: mirrored ? -geometry.armPivot.x : geometry.armPivot.x, y: geometry.armPivot.y))
    }

    /// Each arm is a full-view layer anchored at its pivot and rotated as a whole.
    private func configureArm(_ arm: CALayer, shape: CAShapeLayer, path: CGPath, mirrored: Bool) {
        arm.transform = CATransform3DIdentity
        arm.frame = bounds
        let pivot = pivot(mirrored: mirrored)
        if bounds.width > 0, bounds.height > 0 {
            arm.anchorPoint = CGPoint(x: pivot.x / bounds.width, y: pivot.y / bounds.height)
        }
        arm.position = pivot
        shape.frame = CGRect(origin: .zero, size: bounds.size)
        shape.path = path
        arm.shadowPath = path
        arm.shadowRadius = geometry.shadowRadius * scale
    }

    // MARK: Updates

    /// A new frame from the firmware.
    func show(frame image: CGImage) {
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        screen.contents = image
        CATransaction.commit()
    }

    /// Move the drawn arms toward the firmware's commanded offset, no faster
    /// than the servo would.
    func updateArms(commandedDegrees: Int, nowMs: UInt32) {
        let target = CGFloat(commandedDegrees)
        if let last = lastArmUpdate {
            let maxStep = geometry.armDegreesPerSecond * CGFloat(nowMs &- last) / 1000
            drawnAngle += max(-maxStep, min(maxStep, target - drawnAngle))
        } else {
            drawnAngle = target
        }
        lastArmUpdate = nowMs
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        applyArmAngle()
        CATransaction.commit()
    }

    private var armRadians: CGFloat {
        (geometry.restAngleDegrees + geometry.upSign * drawnAngle) * .pi / 180
    }

    private func applyArmAngle() {
        let radians = armRadians
        rightArm.transform = CATransform3DMakeRotation(radians, 0, 0, 1)
        leftArm.transform = CATransform3DMakeRotation(-radians, 0, 0, 1)
        // Shadow offsets are in each arm's own (rotated) space; counter-rotate
        // them so the light stays overhead.
        let offset = CGSize(width: geometry.shadowOffset.width * scale, height: geometry.shadowOffset.height * scale)
        rightArm.shadowOffset = Self.rotate(offset, by: -radians)
        leftArm.shadowOffset = Self.rotate(offset, by: radians)
        bodyShadow.shadowOffset = offset
    }

    private static func rotate(_ v: CGSize, by radians: CGFloat) -> CGSize {
        CGSize(width: v.width * cos(radians) - v.height * sin(radians),
               height: v.width * sin(radians) + v.height * cos(radians))
    }

    /// True if the point (view coordinates) is on the robot itself: the body
    /// or an arm at its current angle. Everything else in the window lets
    /// clicks through.
    func robotContains(_ p: CGPoint) -> Bool {
        let body = disc(diameter: geometry.bodyDiameter)
        if hypot(p.x - body.midX, p.y - body.midY) <= body.width / 2 { return true }
        for (path, mirrored, angle) in [(rightArmPath, false, armRadians), (leftArmPath, true, -armRadians)] {
            guard let path else { continue }
            // Undo the arm's rotation and test against the unrotated outline.
            let pivot = pivot(mirrored: mirrored)
            let dx = p.x - pivot.x, dy = p.y - pivot.y
            let local = CGPoint(x: pivot.x + dx * cos(-angle) - dy * sin(-angle),
                                y: pivot.y + dx * sin(-angle) + dy * cos(-angle))
            if path.contains(local) { return true }
        }
        return false
    }

    /// The view as drawn (transparent background), for debugging.
    func snapshotPNG() -> Data? {
        guard let root = layer, bounds.width > 0,
            let rep = NSBitmapImageRep(
                bitmapDataPlanes: nil, pixelsWide: Int(bounds.width * 2), pixelsHigh: Int(bounds.height * 2),
                bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                bytesPerRow: 0, bitsPerPixel: 0),
            let context = NSGraphicsContext(bitmapImageRep: rep)
        else { return nil }
        context.cgContext.scaleBy(x: 2, y: 2)
        root.render(in: context.cgContext)
        return rep.representation(using: .png, properties: [:])
    }
}

extension CGColor {
    /// "#RRGGBB" (sRGB) to a colour; nil if it doesn't parse.
    static func fromHex(_ hex: String) -> CGColor? {
        let digits = hex.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: "#", with: "")
        guard digits.count == 6, let value = UInt32(digits, radix: 16) else { return nil }
        return CGColor(
            srgbRed: CGFloat((value >> 16) & 0xFF) / 255, green: CGFloat((value >> 8) & 0xFF) / 255,
            blue: CGFloat(value & 0xFF) / 255, alpha: 1)
    }
}

/// The optional robot window: just the robot, floating on the desktop. A
/// borderless, transparent panel with no title bar or buttons, which never
/// activates the app (so bounces keep working) and passes clicks outside the
/// robot's shape through to whatever is behind it. It can float above
/// everything, full-screen apps and all Spaces included.
@MainActor
final class RobotWindowController: NSObject {
    /// Size presets, in points (the window's width).
    static let sizes: [(name: String, side: CGFloat)] = [("Small", 220), ("Medium", 320), ("Large", 460)]
    static let sideRange: ClosedRange<CGFloat> = 120...900
    private static let autosaveName = "SultanaRobotWindow"

    let view: RobotView
    private let panel: NSPanel
    private let aspect: CGFloat
    var onVisibilityChange: ((Bool) -> Void)?

    var isVisible: Bool { panel.isVisible }
    /// False while the window is closed or completely covered.
    var needsUpdates: Bool { panel.isVisible && panel.occlusionState.contains(.visible) }
    /// The window's width in points.
    var side: CGFloat { panel.frame.width }

    var keepsOnTop = false {
        didSet {
            panel.level = keepsOnTop ? .floating : .normal
            panel.collectionBehavior = keepsOnTop ? [.canJoinAllSpaces, .fullScreenAuxiliary] : []
        }
    }

    init(layout: RobotLayout = .standard) {
        view = RobotView(geometry: layout)
        aspect = layout.aspect
        panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 320 / layout.aspect),
            styleMask: [.borderless, .nonactivatingPanel], backing: .buffered, defer: true)
        super.init()
        panel.isOpaque = false
        panel.backgroundColor = .clear
        panel.hasShadow = false  // the robot draws its own, shaped shadow
        panel.hidesOnDeactivate = false
        panel.isMovableByWindowBackground = true
        panel.isReleasedWhenClosed = false
        panel.contentView = view
        panel.center()
        panel.setFrameAutosaveName(Self.autosaveName)
        view.onMagnify = { [weak self] magnification in self?.resize(by: 1 + magnification) }
    }

    /// Shows the window without activating the app.
    func show() {
        keepOnScreen()
        panel.orderFrontRegardless()
        onVisibilityChange?(true)
    }

    func hide() {
        panel.orderOut(nil)
        onVisibilityChange?(false)
    }

    /// Resizes about the window's centre, within `sideRange`.
    func setSide(_ newSide: CGFloat) {
        let clamped = min(max(newSide, Self.sideRange.lowerBound), Self.sideRange.upperBound)
        let frame = panel.frame
        let height = clamped / aspect
        panel.setFrame(
            NSRect(x: frame.midX - clamped / 2, y: frame.midY - height / 2, width: clamped, height: height),
            display: true)
    }

    func resize(by factor: CGFloat) {
        setSide(side * factor)
    }

    /// Only the robot catches the mouse: anywhere else in the (square,
    /// transparent) window, clicks fall through to the windows behind. Call
    /// often (every display tick); it's a point-in-shape test.
    func updateMousePassThrough() {
        guard panel.isVisible else { return }
        // Keep catching events for the rest of a press that began on the robot.
        if NSEvent.pressedMouseButtons != 0, !panel.ignoresMouseEvents { return }
        let inWindow = panel.convertPoint(fromScreen: NSEvent.mouseLocation)
        let onRobot = view.robotContains(view.convert(inWindow, from: nil))
        if panel.ignoresMouseEvents == onRobot { panel.ignoresMouseEvents = !onRobot }
    }

    /// A borderless window doesn't get pulled back on screen by AppKit, so
    /// recentre it if its saved position is on a display that's gone.
    private func keepOnScreen() {
        let frame = panel.frame
        if !NSScreen.screens.contains(where: { $0.visibleFrame.intersects(frame) }) {
            panel.center()
        }
    }
}
