import AppKit
import QuartzCore

/// Where everything sits in the robot drawing, in unit coordinates: the
/// canvas is 1 wide and 1/aspect tall, with y pointing up. This is the one
/// place to change when the real body and arm art arrives (PLAN.md Phase 5).
///
/// The placeholder follows the face editor's arm overlay: two palms orbit
/// the round screen, at 3 and 9 o'clock when the servo offset is 0, and a
/// positive offset raises both. The canvas leaves a margin around the robot
/// for its shadow and the arms' swing.
struct RobotLayout {
    static let placeholder = RobotLayout()

    /// Canvas width / height. The window keeps this aspect ratio.
    var aspect: CGFloat = 1
    var center = CGPoint(x: 0.5, y: 0.5)
    /// The body (bezel) around the screen.
    var bodyDiameter: CGFloat = 0.68
    var screenDiameter: CGFloat = 0.57
    /// The right arm's pivot. The left arm mirrors it about the centre line.
    var armPivot = CGPoint(x: 0.5, y: 0.5)
    /// Where the arm becomes visible (inside the body's edge) and where its
    /// palm is, measured from the pivot.
    var armStart: CGFloat = 0.32
    var armLength: CGFloat = 0.395
    var armThickness: CGFloat = 0.028
    /// Palm size: radial depth by tangential length.
    var palmSize = CGSize(width: 0.07, height: 0.13)
    /// Right arm angle at servo offset 0, in degrees anticlockwise from
    /// pointing right.
    var restAngleDegrees: CGFloat = 0
    /// +1 if a positive firmware offset raises the arms.
    var upSign: CGFloat = 1

    /// The physical servo lags the angle the firmware commands; the drawn
    /// arms follow at this rate. The SG92R is rated at about 0.1 s per 60°.
    var armDegreesPerSecond: CGFloat = 500

    /// Soft drop shadow under the body and arms, lit from above.
    var shadowRadius: CGFloat = 0.02
    var shadowOffset = CGSize(width: 0, height: -0.012)
    var shadowOpacity: Float = 0.45
}

/// Draws the robot with Core Animation layers on a transparent background:
/// body, round screen (the firmware's frame, nearest-neighbour so panel
/// pixels stay crisp) and two arms that rotate about their pivots. Dragging
/// it moves the window, pinching resizes it, right-clicking shows a menu.
final class RobotView: NSView {
    let geometry: RobotLayout
    var menuProvider: (() -> NSMenu?)?
    var onMagnify: ((CGFloat) -> Void)?

    private let body = CAShapeLayer()
    private let screen = CALayer()
    private let rightArm = CALayer()
    private let leftArm = CALayer()
    private(set) var drawnAngle: CGFloat = 0
    private var lastArmUpdate: UInt32?

    static let bodyColor = CGColor(srgbRed: 0x23 / 255, green: 0x23 / 255, blue: 0x26 / 255, alpha: 1)
    static let bodyRimColor = CGColor(srgbRed: 0x3a / 255, green: 0x3a / 255, blue: 0x40 / 255, alpha: 1)
    static let armColor = CGColor(srgbRed: 0x4a / 255, green: 0x4a / 255, blue: 0x52 / 255, alpha: 1)
    static let palmColor = CGColor(srgbRed: 228 / 255, green: 232 / 255, blue: 242 / 255, alpha: 1)
    static let palmEdgeColor = CGColor(srgbRed: 24 / 255, green: 28 / 255, blue: 40 / 255, alpha: 1)

    init(geometry: RobotLayout = .placeholder) {
        self.geometry = geometry
        super.init(frame: .zero)
        wantsLayer = true
        layerContentsRedrawPolicy = .never
        let root = CALayer()
        root.backgroundColor = .clear
        layer = root

        body.fillColor = Self.bodyColor
        body.strokeColor = Self.bodyRimColor
        screen.backgroundColor = CGColor(gray: 0, alpha: 1)
        screen.magnificationFilter = .nearest
        screen.minificationFilter = .trilinear
        screen.masksToBounds = true
        screen.contentsGravity = .resize
        for shadowed in [body, rightArm, leftArm] as [CALayer] {
            shadowed.shadowColor = CGColor(gray: 0, alpha: 1)
            shadowed.shadowOpacity = geometry.shadowOpacity
        }
        root.addSublayer(body)
        root.addSublayer(screen)
        root.addSublayer(rightArm)
        root.addSublayer(leftArm)
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

    /// The canvas: the largest rect with the layout's aspect ratio, centred
    /// in the view.
    private var canvas: CGRect {
        let width = min(bounds.width, bounds.height * geometry.aspect)
        let height = width / geometry.aspect
        return CGRect(x: bounds.midX - width / 2, y: bounds.midY - height / 2, width: width, height: height)
    }
    /// Unit coordinates to view points.
    private var unit: CGFloat { canvas.width }
    private func point(_ p: CGPoint) -> CGPoint {
        CGPoint(x: canvas.minX + p.x * unit, y: canvas.minY + p.y * unit)
    }

    override func layout() {
        super.layout()
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        let center = point(geometry.center)

        let bodyRadius = geometry.bodyDiameter * unit / 2
        body.frame = bounds
        body.lineWidth = max(1, unit * 0.006)
        let bodyPath = CGPath(
            ellipseIn: CGRect(x: center.x - bodyRadius, y: center.y - bodyRadius, width: bodyRadius * 2,
                              height: bodyRadius * 2), transform: nil)
        body.path = bodyPath
        body.shadowPath = bodyPath
        body.shadowRadius = geometry.shadowRadius * unit
        body.shadowOffset = CGSize(width: geometry.shadowOffset.width * unit, height: geometry.shadowOffset.height * unit)

        let screenSide = geometry.screenDiameter * unit
        screen.frame = CGRect(x: center.x - screenSide / 2, y: center.y - screenSide / 2, width: screenSide,
                              height: screenSide)
        screen.cornerRadius = screenSide / 2
        screen.contentsScale = window?.backingScaleFactor ?? 2

        configureArm(rightArm, mirrored: false)
        configureArm(leftArm, mirrored: true)
        applyArmAngle()
        CATransaction.commit()
    }

    private func pivot(mirrored: Bool) -> CGPoint {
        point(mirrored ? CGPoint(x: 1 - geometry.armPivot.x, y: geometry.armPivot.y) : geometry.armPivot)
    }

    /// Each arm is a full-canvas layer anchored at its pivot, drawn pointing
    /// straight out (right, or left when mirrored) and rotated as a whole.
    private func configureArm(_ arm: CALayer, mirrored: Bool) {
        arm.sublayers?.forEach { $0.removeFromSuperlayer() }
        arm.transform = CATransform3DIdentity
        arm.frame = bounds
        let pivot = pivot(mirrored: mirrored)
        if bounds.width > 0, bounds.height > 0 {
            arm.anchorPoint = CGPoint(x: pivot.x / bounds.width, y: pivot.y / bounds.height)
        }
        arm.position = pivot
        let direction: CGFloat = mirrored ? -1 : 1

        let start = geometry.armStart * unit
        let end = (geometry.armLength - geometry.palmSize.width / 2) * unit
        let thickness = geometry.armThickness * unit
        let stubRect = CGRect(
            x: direction > 0 ? pivot.x + start : pivot.x - end, y: pivot.y - thickness / 2,
            width: end - start, height: thickness)
        let stubPath = CGPath(
            roundedRect: stubRect, cornerWidth: thickness / 2, cornerHeight: thickness / 2, transform: nil)

        let depth = geometry.palmSize.width * unit
        let length = geometry.palmSize.height * unit
        let palmCenterX = pivot.x + direction * geometry.armLength * unit
        let palmRect = CGRect(x: palmCenterX - depth / 2, y: pivot.y - length / 2, width: depth, height: length)
        let palmPath = CGPath(
            roundedRect: palmRect, cornerWidth: depth * 0.45, cornerHeight: depth * 0.45, transform: nil)

        let stub = CAShapeLayer()
        stub.path = stubPath
        stub.fillColor = Self.armColor
        let palm = CAShapeLayer()
        palm.path = palmPath
        palm.fillColor = Self.palmColor
        palm.strokeColor = Self.palmEdgeColor
        palm.lineWidth = max(1, unit * 0.004)
        for part in [stub, palm] {
            part.frame = CGRect(origin: .zero, size: bounds.size)
            arm.addSublayer(part)
        }

        let shadowPath = CGMutablePath()
        shadowPath.addPath(stubPath)
        shadowPath.addPath(palmPath)
        arm.shadowPath = shadowPath
        arm.shadowRadius = geometry.shadowRadius * unit
    }

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
        let offset = CGSize(width: geometry.shadowOffset.width * unit, height: geometry.shadowOffset.height * unit)
        rightArm.shadowOffset = Self.rotate(offset, by: -radians)
        leftArm.shadowOffset = Self.rotate(offset, by: radians)
    }

    private static func rotate(_ v: CGSize, by radians: CGFloat) -> CGSize {
        CGSize(width: v.width * cos(radians) - v.height * sin(radians),
               height: v.width * sin(radians) + v.height * cos(radians))
    }

    /// True if the point (view coordinates) is on the robot itself: the body
    /// or a palm. Everything else in the window should let clicks through.
    func robotContains(_ p: CGPoint) -> Bool {
        let center = point(geometry.center)
        let bodyRadius = geometry.bodyDiameter * unit / 2
        if hypot(p.x - center.x, p.y - center.y) <= bodyRadius { return true }
        let palmReach = max(geometry.palmSize.width, geometry.palmSize.height) / 2 * unit
        for (mirrored, angle) in [(false, armRadians), (true, -armRadians)] {
            let pivot = pivot(mirrored: mirrored)
            let direction: CGFloat = mirrored ? -1 : 1
            let local = CGPoint(x: direction * geometry.armLength * unit, y: 0)
            let palm = CGPoint(x: pivot.x + local.x * cos(angle) - local.y * sin(angle),
                               y: pivot.y + local.x * sin(angle) + local.y * cos(angle))
            if hypot(p.x - palm.x, p.y - palm.y) <= palmReach { return true }
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

/// The optional robot window: just the robot, floating on the desktop. A
/// borderless, transparent panel with no title bar or buttons, which never
/// activates the app (so bounces keep working) and passes clicks outside the
/// robot's shape through to whatever is behind it. It can float above
/// everything, full-screen apps and all Spaces included.
@MainActor
final class RobotWindowController: NSObject {
    /// Size presets, in points (the window's width).
    static let sizes: [(name: String, side: CGFloat)] = [("Small", 200), ("Medium", 320), ("Large", 480)]
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

    init(layout: RobotLayout = .placeholder) {
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
