import CoreGraphics
import Foundation
import Security
import ServiceManagement

enum TurnEndBounce: String, CaseIterable, Identifiable {
    case off, once, untilActive
    var id: String { rawValue }
    var label: String {
        switch self {
        case .off: "Don't bounce"
        case .once: "Bounce once"
        case .untilActive: "Bounce until I switch to it"
        }
    }
}

/// User settings. The bridge token lives in the Keychain; everything else in
/// UserDefaults. SULTANA_BRIDGE_HOST / _PORT / _TOKEN environment variables
/// override the stored connection settings without changing them (handy
/// when running from Xcode).
@MainActor
final class AppPreferences: ObservableObject {
    private enum Key {
        static let host = "bridgeHost"
        static let port = "bridgePort"
        static let turnEndBounce = "turnEndBounce"
        static let attentionBounce = "attentionBounce"
        static let dockFramesPerSecond = "dockFramesPerSecond"
        static let robotWindowVisible = "robotWindowVisible"
        static let robotWindowOnTop = "robotWindowOnTop"
        static let armColorHex = "armColorHex"
    }

    static let dockFrameRates = [30, 15, 10]

    private let defaults = UserDefaults.standard

    @Published var host: String { didSet { defaults.set(host, forKey: Key.host) } }
    @Published var port: Int { didSet { defaults.set(port, forKey: Key.port) } }
    @Published private(set) var token: String
    @Published var turnEndBounce: TurnEndBounce {
        didSet { defaults.set(turnEndBounce.rawValue, forKey: Key.turnEndBounce) }
    }
    @Published var attentionBounce: Bool { didSet { defaults.set(attentionBounce, forKey: Key.attentionBounce) } }
    @Published var dockFramesPerSecond: Int {
        didSet { defaults.set(dockFramesPerSecond, forKey: Key.dockFramesPerSecond) }
    }
    @Published var robotWindowVisible: Bool {
        didSet { defaults.set(robotWindowVisible, forKey: Key.robotWindowVisible) }
    }
    @Published var robotWindowOnTop: Bool {
        didSet { defaults.set(robotWindowOnTop, forKey: Key.robotWindowOnTop) }
    }
    /// The robot's arm colour, "#RRGGBB".
    @Published var armColorHex: String {
        didSet { defaults.set(armColorHex, forKey: Key.armColorHex) }
    }

    let environmentHost: String?
    let environmentPort: Int?
    let environmentToken: String?
    var hasEnvironmentOverride: Bool { environmentHost != nil || environmentPort != nil || environmentToken != nil }

    init() {
        host = defaults.string(forKey: Key.host) ?? "127.0.0.1"
        let storedPort = defaults.integer(forKey: Key.port)
        port = storedPort == 0 ? 8787 : storedPort
        turnEndBounce = TurnEndBounce(rawValue: defaults.string(forKey: Key.turnEndBounce) ?? "") ?? .once
        attentionBounce = defaults.object(forKey: Key.attentionBounce) as? Bool ?? true
        let fps = defaults.integer(forKey: Key.dockFramesPerSecond)
        dockFramesPerSecond = Self.dockFrameRates.contains(fps) ? fps : 30
        robotWindowVisible = defaults.bool(forKey: Key.robotWindowVisible)
        robotWindowOnTop = defaults.bool(forKey: Key.robotWindowOnTop)
        let storedArm = defaults.string(forKey: Key.armColorHex) ?? ""
        armColorHex = CGColor.fromHex(storedArm) == nil ? RobotLayout.defaultArmColorHex : storedArm

        let env = ProcessInfo.processInfo.environment
        environmentHost = env["SULTANA_BRIDGE_HOST"].flatMap { $0.isEmpty ? nil : $0 }
        environmentPort = env["SULTANA_BRIDGE_PORT"].flatMap { Int($0) }
        environmentToken = env["SULTANA_BRIDGE_TOKEN"].flatMap { $0.isEmpty ? nil : $0 }
        // Skip the Keychain entirely when the environment supplies a token:
        // ad-hoc signed debug builds get a Keychain prompt after every rebuild.
        token = environmentToken == nil ? (Keychain.readToken() ?? "") : ""
    }

    /// The endpoint to connect to, environment overrides applied.
    var endpoint: BridgeEndpoint {
        BridgeEndpoint(
            host: environmentHost ?? host, port: environmentPort ?? port, token: environmentToken ?? token)
    }

    func saveConnection(host: String, port: Int, token: String) {
        self.host = host.trimmingCharacters(in: .whitespacesAndNewlines)
        self.port = port
        let trimmed = token.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed != self.token {
            if trimmed.isEmpty { Keychain.deleteToken() } else { Keychain.writeToken(trimmed) }
            self.token = trimmed
        }
    }
}

/// Open at login, through SMAppService (macOS 13+). Registers the app at
/// its current location, so register a copy in /Applications rather than a
/// build in DerivedData.
enum LoginItem {
    static var status: SMAppService.Status { SMAppService.mainApp.status }
    static var isEnabled: Bool { status == .enabled }

    static func setEnabled(_ enabled: Bool) throws {
        if enabled {
            try SMAppService.mainApp.register()
        } else {
            try SMAppService.mainApp.unregister()
        }
    }
}

/// The bridge token, stored as a generic password in the login Keychain.
enum Keychain {
    private static let service = "com.lachlansleight.sultana.dock"
    private static let account = "bridge-token"

    private static var baseQuery: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: account]
    }

    static func readToken() -> String? {
        var query = baseQuery
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess, let data = result as? Data else {
            if status != errSecItemNotFound { Log.app.error("keychain read failed: \(status)") }
            return nil
        }
        return String(data: data, encoding: .utf8)
    }

    @discardableResult
    static func writeToken(_ token: String) -> Bool {
        let data = Data(token.utf8)
        let update = SecItemUpdate(baseQuery as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if update == errSecSuccess { return true }
        var add = baseQuery
        add[kSecValueData as String] = data
        add[kSecAttrLabel as String] = "Sultana Dock bridge token"
        let status = SecItemAdd(add as CFDictionary, nil)
        if status != errSecSuccess { Log.app.error("keychain write failed: \(status)") }
        return status == errSecSuccess
    }

    static func deleteToken() {
        SecItemDelete(baseQuery as CFDictionary)
    }
}

/// Values worth importing from the bridge's .env file.
struct DotEnv {
    var token: String?
    var port: Int?
    var host: String?

    init(contentsOf url: URL) throws {
        let text = try String(contentsOf: url, encoding: .utf8)
        for raw in text.split(whereSeparator: \.isNewline) {
            let line = raw.trimmingCharacters(in: .whitespaces)
            guard !line.hasPrefix("#"), let eq = line.firstIndex(of: "=") else { continue }
            let key = line[..<eq].trimmingCharacters(in: .whitespaces)
            var value = line[line.index(after: eq)...].trimmingCharacters(in: .whitespaces)
            if value.count >= 2, let first = value.first, first == "\"" || first == "'", value.last == first {
                value = String(value.dropFirst().dropLast())
            }
            switch key {
            case "BRIDGE_TOKEN": token = value
            case "BRIDGE_PORT": port = Int(value)
            case "BRIDGE_HOST": host = value
            default: break
            }
        }
    }

    /// A host to connect to: the bind address, unless it binds everywhere.
    var connectHost: String? {
        guard let host, !host.isEmpty else { return nil }
        return host == "0.0.0.0" || host == "::" ? "127.0.0.1" : host
    }
}
