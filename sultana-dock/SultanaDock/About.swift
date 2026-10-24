import AppKit

/// The standard About panel, with credits for the third-party code the app
/// ships. The licence texts are in Contents/Resources/Licenses (copied there
/// by bridge/embed-bridge.sh). Credits are built here rather than read from a
/// Credits.html, whose text the panel draws black even in dark mode.
enum AboutPanel {
    static let projectURL = URL(string: "https://github.com/lachlansleight/claude-code-wrapper")!

    private static let thirdParty: [(name: String, by: String, licence: String)] = [
        ("Node.js", "runs the built-in bridge", "MIT License"),
        ("ws", "the bridge's WebSocket server", "MIT License"),
        ("ArduinoJson", "by Benoît Blanchon, in the robot's firmware", "MIT License"),
        ("TFT_eSPI", "by Bodmer, drawing code ported from it, parts from Adafruit_GFX", "FreeBSD and BSD licences"),
    ]

    static func show() {
        NSApp.activate()
        NSApp.orderFrontStandardAboutPanel(options: [.credits: credits()])
    }

    private static func credits() -> NSAttributedString {
        let body = NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)
        let bold = NSFont.boldSystemFont(ofSize: NSFont.smallSystemFontSize)
        let centred = NSMutableParagraphStyle()
        centred.alignment = .center
        centred.paragraphSpacing = 4
        let plain: [NSAttributedString.Key: Any] = [
            .font: body, .foregroundColor: NSColor.labelColor, .paragraphStyle: centred,
        ]
        let text = NSMutableAttributedString()
        func append(_ string: String, _ extra: [NSAttributedString.Key: Any] = [:]) {
            text.append(NSAttributedString(string: string, attributes: plain.merging(extra) { $1 }))
        }

        append("The robot that shows what your coding agent is doing.\n")
        append("github.com/lachlansleight/claude-code-wrapper\n\n", [.link: projectURL])
        append("Includes\n", [.font: bold])
        for item in thirdParty {
            append(item.name, [.font: bold])
            append(", \(item.by): \(item.licence)\n", [.foregroundColor: NSColor.secondaryLabelColor])
        }
        if let licenses = Bundle.main.url(forResource: "Licenses", withExtension: nil) {
            append("\n")
            append("Show the licence texts", [.link: licenses])
        }
        return text
    }
}
