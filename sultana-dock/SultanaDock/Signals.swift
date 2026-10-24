import AppKit

/// Things worth a Dock bounce, read from raw bridge frames *before* the
/// firmware's single-session latch sees them, so a turn ending in any
/// session counts, not just the one on the face.
enum BridgeSignal: Equatable {
    case turnEnded(sessionID: String?, cause: String?)
    case needsAttention(sessionID: String?)

    private struct Frame: Decodable {
        let type: String?
        let session_id: String?
        let event: Event?
    }

    private struct Event: Decodable {
        let kind: String?
        let cause: String?
        let text: String?
    }

    static func from(frame text: String) -> BridgeSignal? {
        guard let data = text.data(using: .utf8),
            let frame = try? JSONDecoder().decode(Frame.self, from: data),
            frame.type == "agent_event", let kind = frame.event?.kind
        else { return nil }
        switch kind {
        case "turn.ended":
            return .turnEnded(sessionID: frame.session_id, cause: frame.event?.cause)
        case "permission.requested":
            // Codex and OpenCode report permission prompts this way.
            return .needsAttention(sessionID: frame.session_id)
        case "notification":
            // Claude's permission prompt (same test as the firmware's
            // AttractingAttention trigger in EventRouter.cpp).
            if frame.event?.text?.hasPrefix("Claude needs") == true {
                return .needsAttention(sessionID: frame.session_id)
            }
            return nil
        default:
            return nil
        }
    }
}

/// Bounces the Dock icon for signals, per the user's settings.
///
/// macOS ignores requests while the app is frontmost, and ends a critical
/// (bounce-until-active) request by itself when the app is activated, so
/// nothing here needs cancelling. Note that request ids are 0-based indexes
/// into the app's outstanding requests and get reused: 0 is a valid id, not
/// a failure.
@MainActor
final class AttentionController {
    func handle(_ signal: BridgeSignal, preferences: AppPreferences) {
        switch signal {
        case .turnEnded(let session, let cause):
            let reason = "turn ended (\(cause ?? "?"), \(session ?? "no session"))"
            switch preferences.turnEndBounce {
            case .off: return
            case .once: request(.informationalRequest, reason: reason)
            case .untilActive: request(.criticalRequest, reason: reason)
            }
        case .needsAttention(let session):
            guard preferences.attentionBounce else { return }
            request(.informationalRequest, reason: "needs attention (\(session ?? "no session"))")
        }
    }

    private func request(_ type: NSApplication.RequestUserAttentionType, reason: String) {
        let id = NSApp.requestUserAttention(type)
        let kind = type == .criticalRequest ? "critical" : "informational"
        Log.app.info("bounce \(kind, privacy: .public): \(reason, privacy: .public) (request \(id))")
        DebugDump.shared?.event(
            "bounce \(kind): \(reason) request=\(id) active=\(NSApp.isActive) hidden=\(NSApp.isHidden) "
                + "front=\(NSWorkspace.shared.frontmostApplication?.localizedName ?? "?")")
    }
}
