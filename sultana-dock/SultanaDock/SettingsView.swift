import AppKit
import ServiceManagement
import SwiftUI

/// Bridge and behaviour settings.
struct SettingsView: View {
    @ObservedObject var preferences: AppPreferences
    @ObservedObject var status: AppStatus
    @ObservedObject var plugin: ClaudePlugin
    let onConnect: () -> Void

    @State private var host = ""
    @State private var portText = ""
    @State private var token = ""
    @State private var importError: String?
    @State private var loginStatus = LoginItem.status
    @State private var loginError: String?

    var body: some View {
        Form {
            claudeCodeSection

            Section {
                Picker("Use", selection: $preferences.bridgeMode) {
                    ForEach(BridgeMode.allCases) { Text($0.label).tag($0) }
                }
                .disabled(preferences.environmentMode != nil)
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
            } footer: {
                VStack(alignment: .leading, spacing: 4) {
                    Text(status.text).foregroundStyle(status.isConnected ? Color.green : Color.secondary)
                    Text(
                        preferences.mode == .builtIn
                            ? "Sultana Dock runs the bridge on port \(String(AppPreferences.builtInPort)), where the sultana Claude Code plugin sends its hooks."
                            : "Connect to a bridge you run yourself (node plugin/dist/index.js), here or on another computer."
                    )
                    .foregroundStyle(.secondary)
                }
                .font(.callout)
            }

            Section("General") {
                Toggle("Open at login", isOn: Binding(
                    get: { loginStatus == .enabled || loginStatus == .requiresApproval },
                    set: setOpenAtLogin))
                if loginStatus == .requiresApproval {
                    HStack {
                        Text("Allow Sultana Dock under Login Items to finish.")
                            .foregroundStyle(.secondary).font(.callout)
                        Spacer()
                        Button("Open Login Items") { SMAppService.openSystemSettingsLoginItems() }
                    }
                }
                if let loginError {
                    Text(loginError).foregroundStyle(.red).font(.callout)
                }
                Toggle("Show the robot window", isOn: $preferences.robotWindowVisible)
                Toggle("Keep the robot window on top", isOn: $preferences.robotWindowOnTop)
                HStack {
                    ColorPicker("Arm colour", selection: armColor, supportsOpacity: false)
                    if preferences.armColorHex.uppercased() != RobotLayout.defaultArmColorHex {
                        Button("Reset") { preferences.armColorHex = RobotLayout.defaultArmColorHex }
                    }
                }
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
        .frame(width: 440)
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
            if plugin.lastError != nil || (plugin.hasLookedForCLI && plugin.cli == nil) {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Or run these in a terminal:").font(.callout)
                    Text(plugin.manualCommands).font(.system(.callout, design: .monospaced)).textSelection(.enabled)
                    Button("Copy Commands") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(plugin.manualCommands, forType: .string)
                    }
                }
            }
        } header: {
            Text("Claude Code")
        } footer: {
            Text(pluginFooterText).foregroundStyle(.secondary).font(.callout)
        }
    }

    private var pluginButton: some View {
        Button(pluginButtonTitle) { plugin.install() }
            .disabled(plugin.isWorking || plugin.marketplaceURL == nil)
    }

    private var pluginStateText: String {
        switch plugin.state {
        case .notInstalled: "Not installed"
        case .installed: "Installed"
        case .outdated: "Installed from another version of Sultana Dock"
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

    private var pluginFooterText: String {
        let what = "The plugin sends Claude Code's activity to Sultana."
        guard plugin.hasLookedForCLI else { return what }
        guard let cli = plugin.cli else {
            return "\(what) Couldn't find the claude command: install Claude Code, or run the commands above."
        }
        return "\(what) Sultana Dock installs it with \(ClaudePlugin.tildePath(cli))."
    }

    @ViewBuilder private var builtInBridgeSettings: some View {
        Toggle("Let robots on my network connect", isOn: $preferences.allowNetworkRobots)
        if preferences.allowNetworkRobots {
            Text("Robots on the same network find this Mac by themselves, and connect with this token.")
                .foregroundStyle(.secondary).font(.callout)
        }
        HStack {
            SecureField("Token", text: $token, prompt: Text("Robots connect with this"))
            Button("Copy") {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(token, forType: .string)
            }
            .disabled(token.isEmpty)
        }
        HStack {
            Button("Show Log") { NSWorkspace.shared.open(BuiltInBridge.logURL) }
            Spacer()
            Button("Save Token", action: saveToken)
                .disabled(token.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty || token == preferences.token)
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

    /// The arm colour setting as a SwiftUI colour, stored as "#RRGGBB" (sRGB).
    private var armColor: Binding<Color> {
        Binding(
            get: { Color(cgColor: CGColor.fromHex(preferences.armColorHex) ?? CGColor(gray: 0.5, alpha: 1)) },
            set: { newValue in
                guard let srgb = NSColor(newValue).usingColorSpace(.sRGB) else { return }
                let channel = { (value: CGFloat) in Int((min(max(value, 0), 1) * 255).rounded()) }
                preferences.armColorHex = String(
                    format: "#%02X%02X%02X", channel(srgb.redComponent), channel(srgb.greenComponent),
                    channel(srgb.blueComponent))
            })
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

/// Connection status shown in the settings window and the Dock menu.
@MainActor
final class AppStatus: ObservableObject {
    @Published var text = "Starting…"
    @Published var isConnected = false
    /// When the bridge last passed on an event from Claude Code.
    @Published var lastClaudeHook: Date?
}
