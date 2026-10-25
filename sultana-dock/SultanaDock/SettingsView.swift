import AppKit
import ServiceManagement
import SwiftUI

enum SettingsTab: String, CaseIterable {
    case general, setup, bridge
}

/// Settings, in three tabs: General (start-up, the robot window, the Dock),
/// Setup (Claude Code's plugin, pairing a robot) and Bridge.
struct SettingsView: View {
    @ObservedObject var preferences: AppPreferences
    @ObservedObject var status: AppStatus
    @ObservedObject var plugin: ClaudePlugin
    @ObservedObject var robots: RobotMonitor
    let onConnect: () -> Void

    @State private var host = ""
    @State private var portText = ""
    @State private var token = ""
    @State private var revealToken = false
    @State private var importError: String?
    @State private var loginStatus = LoginItem.status
    @State private var loginError: String?

    var body: some View {
        TabView(selection: $status.settingsTab) {
            generalTab.tabItem { Text("General") }.tag(SettingsTab.general)
            setupTab.tabItem { Text("Setup") }.tag(SettingsTab.setup)
            bridgeTab.tabItem { Text("Bridge") }.tag(SettingsTab.bridge)
        }
        .frame(width: 460)
        .fixedSize()
        .onAppear {
            host = preferences.host
            portText = String(preferences.port)
            token = preferences.token
            plugin.refresh()
        }
        // Claude Code's settings may have changed while the window was away.
        .onReceive(NotificationCenter.default.publisher(for: NSWindow.didBecomeKeyNotification)) { _ in
            plugin.refresh()
        }
        // The token is made on the built-in bridge's first run, maybe after
        // this window opened.
        .onChange(of: preferences.token) { _, newToken in token = newToken }
    }

    // MARK: Tabs

    private var setupTab: some View {
        Form {
            if AppLocation.isTemporary {
                Section {
                    Text("Sultana is running from the disk image. Drag it to your Applications folder and open it from there, then come back here.")
                        .foregroundStyle(.orange)
                }
            }
            claudeCodeSection
            robotSection
        }
        .formStyle(.grouped)
        .onAppear { robots.start() }
        .onDisappear { robots.stop() }
    }

    private var bridgeTab: some View {
        Form {
            Section {
                Picker("Use", selection: $preferences.bridgeMode) {
                    ForEach(BridgeMode.allCases) { Text($0.label).tag($0) }
                }
                .disabled(preferences.environmentMode != nil)
                if preferences.mode == .builtIn {
                    LabeledContent("Bridge URL") {
                        Text(bridgeURL).font(.system(.body, design: .monospaced)).textSelection(.enabled)
                    }
                }
                LabeledContent("Status") {
                    Text(status.summary).foregroundStyle(status.isConnected ? Color.green : Color.secondary)
                }
                if let detail = status.detail {
                    Text(detail).foregroundStyle(.secondary).font(.callout)
                }
                switch preferences.mode {
                case .builtIn: builtInBridgeSettings
                case .external: externalBridgeSettings
                }
                if let importError {
                    Text(importError).foregroundStyle(.red).font(.callout)
                }
                if preferences.hasEnvironmentOverride {
                    Text("SULTANA_BRIDGE_* environment variables are set and take precedence over these.")
                        .foregroundStyle(.secondary).font(.callout)
                }
            } header: {
                Text("Bridge")
            }
        }
        .formStyle(.grouped)
    }

    /// The built-in bridge's address, where the plugin's hooks go.
    private var bridgeURL: String {
        "http://\(preferences.endpoint.host):\(String(preferences.endpoint.port))"
    }

    private var generalTab: some View {
        Form {
            Section("Start-up") {
                Toggle("Open at login", isOn: Binding(
                    get: { loginStatus == .enabled || loginStatus == .requiresApproval },
                    set: setOpenAtLogin))
                .disabled(AppLocation.isTemporary)
                if loginStatus == .requiresApproval {
                    HStack {
                        Text("Allow Sultana under Login Items to finish.")
                            .foregroundStyle(.secondary).font(.callout)
                        Spacer()
                        Button("Open Login Items") { SMAppService.openSystemSettingsLoginItems() }
                    }
                }
                if let loginError {
                    Text(loginError).foregroundStyle(.red).font(.callout)
                }
            }

            Section("Robot window") {
                Toggle("Show the robot on the desktop", isOn: $preferences.robotWindowVisible)
                Picker("Size", selection: robotSize) {
                    ForEach(RobotSize.allCases) { Text($0.label).tag($0) }
                }
                if preferences.robotSize == .custom {
                    RobotSizeSlider(width: $preferences.robotCustomWidth)
                }
                Toggle("Keep the robot on top of other windows", isOn: $preferences.robotWindowOnTop)
                colorRow("Body colour", \.bodyColorHex, default: RobotLayout.defaultBodyColorHex)
                colorRow("Arm colour", \.armColorHex, default: RobotLayout.defaultArmColorHex)
            }

            Section("Dock") {
                Picker("When a turn ends", selection: $preferences.turnEndBounce) {
                    ForEach(TurnEndBounce.allCases) { Text($0.label).tag($0) }
                }
                Toggle("Bounce when an agent needs permission", isOn: $preferences.attentionBounce)
                Picker("Dock icon frame rate", selection: $preferences.dockFramesPerSecond) {
                    ForEach(AppPreferences.dockFrameRates, id: \.self) { Text("\($0) fps").tag($0) }
                }
            }
        }
        .formStyle(.grouped)
    }

    // MARK: Setup ▸ Claude Code

    private var claudeCodeSection: some View {
        Section {
            LabeledContent("Sultana plugin") {
                Text(pluginStateText).foregroundStyle(plugin.state == .installed ? Color.green : Color.secondary)
            }
            HStack {
                if plugin.state == .notInstalled {
                    pluginButton.buttonStyle(.borderedProminent)
                } else {
                    pluginButton
                }
                if plugin.isWorking { ProgressView().controlSize(.small) }
                Spacer()
                if plugin.state != .notInstalled {
                    Button("Remove") { plugin.remove() }.disabled(plugin.isWorking)
                }
            }
            if let lastHook = status.lastClaudeHook {
                (Text("Last hook from Claude Code ") + Text(lastHook, style: .relative) + Text(" ago"))
                    .foregroundStyle(.secondary).font(.callout)
            } else if plugin.state != .notInstalled {
                Text("No hooks from Claude Code yet. Plugins load when a session starts: start a new one, or run /reload-plugins in one that's open.")
                    .foregroundStyle(.secondary).font(.callout)
            }
            ForEach(plugin.duplicateSenders, id: \.self) { warning in
                Text("Every hook will arrive twice. \(warning)").foregroundStyle(.orange).font(.callout)
            }
            if let error = plugin.lastError {
                Text(error).foregroundStyle(.red).font(.callout).textSelection(.enabled)
            }
            if plugin.lastError != nil || cliMissing {
                VStack(alignment: .leading, spacing: 6) {
                    Text(
                        plugin.lastError == nil
                            ? "Couldn't find the claude command. Install Claude Code, or run these in a terminal:"
                            : "Or run these in a terminal:"
                    )
                    .font(.callout)
                    Text(plugin.manualCommands).font(.system(.callout, design: .monospaced)).textSelection(.enabled)
                    Button("Copy Commands") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(plugin.manualCommands, forType: .string)
                    }
                }
            }
        } header: {
            Text("Claude Code")
        }
    }

    private var cliMissing: Bool { plugin.hasLookedForCLI && plugin.cli == nil }

    private var pluginButton: some View {
        Button(pluginButtonTitle) { plugin.install() }
            .disabled(plugin.isWorking || plugin.marketplaceURL == nil || AppLocation.isTemporary)
    }

    private var pluginStateText: String {
        switch plugin.state {
        case .notInstalled: "Not installed"
        case .installed: "Installed"
        case .outdated: "Installed from another version of Sultana"
        case .disabled: "Installed, but turned off"
        }
    }

    private var pluginButtonTitle: String {
        switch plugin.state {
        case .notInstalled: "Install Plugin"
        case .installed: "Reinstall"
        case .outdated: "Update Plugin"
        case .disabled: "Turn On"
        }
    }

    // MARK: Setup ▸ Robot

    /// Pairing: the steps for the robot's setup portal (hold BOOT at power-on,
    /// join its robot-XXXX network, open the page its screen shows) and the
    /// values to type there, with a live list of connected robots.
    private var robotSection: some View {
        Section {
            if preferences.mode == .builtIn {
                Toggle("Let robots on my network connect", isOn: $preferences.allowNetworkRobots)
            }
            LabeledContent("Robot") {
                Text(robotStatusText).foregroundStyle(connectedRobots.isEmpty ? Color.secondary : Color.green)
            }
            DisclosureGroup("How to set up a robot", isExpanded: $status.robotStepsExpanded) {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(Array(robotSteps.enumerated()), id: \.offset) { index, step in
                        HStack(alignment: .firstTextBaseline, spacing: 6) {
                            Text("\(index + 1).").monospacedDigit()
                            Text(step).fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
                .font(.callout)
                .padding(.vertical, 4)
                LabeledContent("Bridge host") {
                    HStack {
                        Text(robotHost).font(.system(.body, design: .monospaced)).textSelection(.enabled).fixedSize()
                        copyButton(robotHost)
                    }
                }
                LabeledContent("Port") {
                    Text(String(preferences.endpoint.port)).font(.system(.body, design: .monospaced))
                }
                robotTokenRow
            }
        } header: {
            Text("Robot")
        }
    }

    private var connectedRobots: [RobotMonitor.Robot] {
        if case .connected(let list) = robots.status { return list }
        return []
    }

    private var robotStatusText: String {
        switch robots.status {
        case .checking: return "Checking…"
        case .unavailable(let reason): return reason
        case .connected(let list):
            if list.count == 1 { return "Connected at \(list[0].address)" }
            if list.count > 1 { return "\(list.count) connected: \(list.map(\.address).joined(separator: ", "))" }
            if preferences.mode == .builtIn && !preferences.allowNetworkRobots { return "Can't connect until you let it" }
            return "None connected"
        }
    }

    private var robotSteps: [String] {
        var steps: [String] = []
        if preferences.mode == .builtIn && !preferences.allowNetworkRobots {
            steps.append("Turn on Let robots on my network connect, above.")
        }
        steps += [
            "Hold the robot's BOOT button while you plug it in, until its screen says CONFIG MODE.",
            "On your phone or this Mac, join the robot-… WiFi network it shows, then open the address under it in a browser (usually http://192.168.4.1).",
            "Enter your WiFi network and password, and the bridge host, port and token below. Save, and the robot restarts and connects.",
        ]
        return steps
    }

    /// What the robot's portal calls the bridge host: this Mac, unless an
    /// external bridge runs on another computer.
    private var robotHost: String {
        let host = preferences.endpoint.host
        let local = ["127.0.0.1", "localhost", "::1"].contains(host)
        return local ? (LocalNetwork.ipv4Address() ?? "this Mac's IP address") : host
    }

    @ViewBuilder private var robotTokenRow: some View {
        if preferences.mode == .builtIn {
            LabeledContent("Token") {
                HStack {
                    if revealToken {
                        TextField("Token", text: $token, prompt: Text("Not set"))
                            .font(.system(.body, design: .monospaced)).labelsHidden()
                    } else {
                        SecureField("Token", text: $token, prompt: Text("Not set")).labelsHidden()
                    }
                    Button(revealToken ? "Hide" : "Show") { revealToken.toggle() }
                    copyButton(token)
                }
            }
            if token != preferences.token {
                HStack {
                    Text("To use your robot's existing token, paste it here.").foregroundStyle(.secondary).font(.callout)
                    Spacer()
                    Button("Save Token", action: saveToken)
                        .disabled(token.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
            }
        } else {
            LabeledContent("Token") {
                HStack {
                    Text("Your bridge's token (Bridge tab)").foregroundStyle(.secondary)
                    copyButton(preferences.endpoint.token)
                }
            }
        }
    }

    private func copyButton(_ text: String) -> some View {
        Button("Copy") {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(text, forType: .string)
        }
        .disabled(text.isEmpty)
    }

    // MARK: Bridge

    @ViewBuilder private var builtInBridgeSettings: some View {
        HStack {
            Button("Show Log") { NSWorkspace.shared.open(BuiltInBridge.logURL) }
            Spacer()
        }
    }

    @ViewBuilder private var externalBridgeSettings: some View {
        TextField("Host", text: $host, prompt: Text("127.0.0.1"))
        TextField("Port", text: $portText, prompt: Text("8787"))
        SecureField("Token", text: $token, prompt: Text("BRIDGE_TOKEN"))
        HStack {
            Button("Import from .env…", action: importDotEnv)
            Spacer()
            Button("Save and Connect", action: save)
                .keyboardShortcut(.defaultAction)
                .disabled(Int(portText) == nil || host.isEmpty || token.isEmpty)
        }
    }

    /// Choosing Custom starts from the current preset's width, so the robot
    /// doesn't jump.
    private var robotSize: Binding<RobotSize> {
        Binding(
            get: { preferences.robotSize },
            set: { newSize in
                if newSize == .custom, let width = preferences.robotSize.width {
                    preferences.robotCustomWidth = Double(width)
                }
                preferences.robotSize = newSize
            })
    }

    /// A colour setting stored as "#RRGGBB" (sRGB), with Reset once changed.
    private func colorRow(
        _ title: String, _ setting: ReferenceWritableKeyPath<AppPreferences, String>, default defaultHex: String
    ) -> some View {
        let color = Binding<Color>(
            get: { Color(cgColor: CGColor.fromHex(preferences[keyPath: setting]) ?? CGColor(gray: 0.5, alpha: 1)) },
            set: { newValue in
                guard let srgb = NSColor(newValue).usingColorSpace(.sRGB) else { return }
                let channel = { (value: CGFloat) in Int((min(max(value, 0), 1) * 255).rounded()) }
                preferences[keyPath: setting] = String(
                    format: "#%02X%02X%02X", channel(srgb.redComponent), channel(srgb.greenComponent),
                    channel(srgb.blueComponent))
            })
        return HStack {
            ColorPicker(title, selection: color, supportsOpacity: false)
            if preferences[keyPath: setting].uppercased() != defaultHex {
                Button("Reset") { preferences[keyPath: setting] = defaultHex }
            }
        }
    }

    private func setOpenAtLogin(_ enabled: Bool) {
        do {
            try LoginItem.setEnabled(enabled)
            loginError = nil
        } catch {
            loginError = error.localizedDescription
        }
        loginStatus = LoginItem.status
    }

    private func save() {
        guard let port = Int(portText) else { return }
        preferences.saveConnection(host: host, port: port, token: token)
        onConnect()
    }

    /// The built-in bridge restarts with the new token; robots need it too.
    private func saveToken() {
        preferences.saveToken(token)
        onConnect()
    }

    private func importDotEnv() {
        let panel = NSOpenPanel()
        panel.message = "Choose the bridge's .env file (for example plugin/src/.env)."
        panel.showsHiddenFiles = true
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            let env = try DotEnv(contentsOf: url)
            if let value = env.token { token = value }
            if let value = env.port { portText = String(value) }
            if let value = env.connectHost { host = value }
            importError = env.token == nil ? "No BRIDGE_TOKEN in that file." : nil
        } catch {
            importError = error.localizedDescription
        }
    }
}

/// The custom robot width, from half of Small to twice Large, with tick
/// marks at the presets. Sliders only space ticks evenly and these aren't,
/// so they're drawn underneath, inset like the slider's track.
struct RobotSizeSlider: View {
    @Binding var width: Double

    /// Where the track starts and ends inside the slider: half the knob.
    private static let trackInset: CGFloat = 10
    private static let range = Double(RobotSize.widthRange.lowerBound)...Double(RobotSize.widthRange.upperBound)

    var body: some View {
        VStack(spacing: 2) {
            Slider(value: $width, in: Self.range) { Text("Width") }
                .labelsHidden()
            GeometryReader { geometry in
                let track = geometry.size.width - 2 * Self.trackInset
                ForEach(RobotSize.presets) { preset in
                    let fraction = (Double(preset.width ?? 0) - Self.range.lowerBound)
                        / (Self.range.upperBound - Self.range.lowerBound)
                    VStack(spacing: 1) {
                        Rectangle().fill(.secondary).frame(width: 1, height: 4)
                        Text(preset.label).font(.caption2).foregroundStyle(.secondary).fixedSize()
                    }
                    .position(x: Self.trackInset + track * CGFloat(fraction), y: 10)
                }
            }
            .frame(height: 20)
        }
    }
}

/// Connection status shown in the settings window and the Dock menu.
@MainActor
final class AppStatus: ObservableObject {
    /// "Connected", "Port in use" and the like.
    @Published var summary = "Starting…"
    /// A sentence more, when the summary isn't the whole story.
    @Published var detail: String?
    @Published var isConnected = false
    /// When the bridge last passed on an event from Claude Code.
    @Published var lastClaudeHook: Date?
    /// The Settings tab showing; the app picks one when it opens Settings.
    @Published var settingsTab: SettingsTab = .general
    /// Setup ▸ Robot's steps; folded again whenever Settings closes.
    @Published var robotStepsExpanded = false
}
