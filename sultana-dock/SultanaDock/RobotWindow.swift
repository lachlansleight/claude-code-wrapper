import AppKit
import QuartzCore

/// Where everything sits in the robot drawing, in unit coordinates: the
/// canvas is 1 wide and 1/aspect tall, with y pointing up. This is the one
/// place to change when the real body and arm art arrives (PLAN.md Phase 5).
///
/// The placeholder follows the face editor's arm overlay: two palms orbit
/// the round screen, at 3 and 9 o'clock when the servo offset is 0, and a
/// positive offset raises both.
struct RobotLayout {
    static let placeholder = RobotLayout()

    /// Canvas width / height. The window keeps this aspect ratio.
    var aspect: CGFloat = 1
    var center = CGPoint(x: 0.5, y: 0.5)
    /// The body (bezel) around the screen.
    var bodyDiameter: CGFloat = 0.74
    var screenDiameter: CGFloat = 0.62
    /// The right arm's pivot. The left arm mirrors it about the centre line.
    var armPivot = CGPoint(x: 0.5, y: 0.5)
    /// Where the arm becomes visible (the body's edge) and where its palm is,
    /// measured from the pivot.
    var armStart: CGFloat = 0.35
    var armLength: CGFloat = 0.43
    var armThickness: CGFloat = 0.03
    /// Palm size: radial depth by tangential length.
    var palmSize = CGSize(width: 0.075, height: 0.14)
    /// Right arm angle at servo offset 0, in degrees anticlockwise from
    /// pointing right.
    var restAngleDegrees: CGFloat = 0
    /// +1 if a positive firmware offset raises the arms.
    var upSign: CGFloat = 1

    /// The physical servo lags the angle the firmware commands; the drawn
    /// arms follow at this rate. The SG92R is rated at about 0.1 s per 60°.
    var armDegreesPerSecond: CGFloat = 500
}

/// Draws the robot with Core Animation layers: body, round screen (the
/// firmware's frame, nearest-neighbour so panel pixels stay crisp) and two
/// arms that rotate about their pivots.
final class RobotView: NSView {
    let geometry: RobotLayout

    private let body = CAShapeLayer()
    private let screen = CALayer()
    private let rightArm = CALayer()
    private let leftArm = CALayer()
    private var drawnAngle: CGFloat = 0
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
        layer = root

        body.fillColor = Self.bodyColor
        body.strokeColor = Self.bodyRimColor
        screen.backgroundColor = CGColor(gray: 0, alpha: 1)
        screen.magnificationFilter = .nearest
        screen.minificationFilter = .trilinear
        screen.masksToBounds = true
        screen.contentsGravity = .resize
        root.addSublayer(body)
        root.addSublayer(screen)
        root.addSublayer(rightArm)
        root.addSublayer(leftArm)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { false }

    /// The canvas: the largest rect with the layout's aspect ratio, centred
    /// in the view (a restored window frame isn't held to the aspect ratio).
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
        body.path = CGPath(
            ellipseIn: CGRect(x: center.x - bodyRadius, y: center.y - bodyRadius, width: bodyRadius * 2,
                              height: bodyRadius * 2), transform: nil)

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

    /// Each arm is a full-canvas layer anchored at its pivot, drawn pointing
    /// straight out (right, or left when mirrored) and rotated as a whole.
    private func configureArm(_ arm: CALayer, mirrored: Bool) {
        arm.sublayers?.forEach { $0.removeFromSuperlayer() }
        arm.transform = CATransform3DIdentity
        arm.frame = bounds
        let pivotUnit = mirrored ? CGPoint(x: 1 - geometry.armPivot.x, y: geometry.armPivot.y) : geometry.armPivot
        let pivot = point(pivotUnit)
        if bounds.width > 0, bounds.height > 0 {
            arm.anchorPoint = CGPoint(x: pivot.x / bounds.width, y: pivot.y / bounds.height)
        }
        arm.position = pivot
        let direction: CGFloat = mirrored ? -1 : 1

        let stub = CAShapeLayer()
        let start = geometry.armStart * unit
        let end = (geometry.armLength - geometry.palmSize.width / 2) * unit
        let thickness = geometry.armThickness * unit
        let stubRect = CGRect(
            x: direction > 0 ? pivot.x + start : pivot.x - end, y: pivot.y - thickness / 2,
            width: end - start, height: thickness)
        stub.path = CGPath(roundedRect: stubRect, cornerWidth: thickness / 2, cornerHeight: thickness / 2,
                           transform: nil)
        stub.fillColor = Self.armColor

        let palm = CAShapeLayer()
        let depth = geometry.palmSize.width * unit
        let length = geometry.palmSize.height * unit
        let palmCenterX = pivot.x + direction * geometry.armLength * unit
        let palmRect = CGRect(x: palmCenterX - depth / 2, y: pivot.y - length / 2, width: depth, height: length)
        palm.path = CGPath(roundedRect: palmRect, cornerWidth: depth * 0.45, cornerHeight: depth * 0.45,
                           transform: nil)
        palm.fillColor = Self.palmColor
        palm.strokeColor = Self.palmEdgeColor
        palm.lineWidth = max(1, unit * 0.004)

        for part in [stub, palm] {
            part.frame = CGRect(origin: .zero, size: bounds.size)
            arm.addSublayer(part)
        }
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

    private func applyArmAngle() {
        let degrees = geometry.restAngleDegrees + geometry.upSign * drawnAngle
        let radians = degrees * .pi / 180
        rightArm.transform = CATransform3DMakeRotation(radians, 0, 0, 1)
        leftArm.transform = CATransform3DMakeRotation(-radians, 0, 0, 1)
    }

    /// The view as drawn, for debugging.
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

/// The optional robot window: a resizable panel that keeps the robot's
/// aspect ratio, can float above everything (full-screen apps and all
/// Spaces included), and never activates the app, so bounces keep working.
@MainActor
final class RobotWindowController: NSObject, NSWindowDelegate {
    let view: RobotView
    private let panel: NSPanel
    var onVisibilityChange: ((Bool) -> Void)?

    var isVisible: Bool { panel.isVisible }
    /// False while the window is closed or completely covered.
    var needsUpdates: Bool { panel.isVisible && panel.occlusionState.contains(.visible) }

    var keepsOnTop = false {
        didSet {
            panel.level = keepsOnTop ? .floating : .normal
            panel.collectionBehavior = keepsOnTop ? [.canJoinAllSpaces, .fullScreenAuxiliary] : []
        }
    }

    init(layout: RobotLayout = .placeholder) {
        view = RobotView(geometry: layout)
        panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 320 / layout.aspect),
            styleMask: [.titled, .closable, .resizable, .nonactivatingPanel, .fullSizeContentView],
            backing: .buffered, defer: true)
        super.init()
        panel.title = "Sultana"
        panel.titleVisibility = .hidden
        panel.titlebarAppearsTransparent = true
        panel.hidesOnDeactivate = false
        panel.becomesKeyOnlyIfNeeded = true
        panel.isMovableByWindowBackground = true
        panel.isReleasedWhenClosed = false
        panel.contentAspectRatio = NSSize(width: layout.aspect, height: 1)
        panel.contentMinSize = NSSize(width: 140, height: 140 / layout.aspect)
        panel.contentView = view
        panel.delegate = self
        panel.center()
        panel.setFrameAutosaveName("SultanaRobotWindow")
    }

    /// Shows the window without activating the app.
    func show() {
        panel.orderFrontRegardless()
        onVisibilityChange?(true)
    }

    func hide() {
        panel.orderOut(nil)
        onVisibilityChange?(false)
    }

    func windowWillClose(_ notification: Notification) {
        onVisibilityChange?(false)
    }
}
