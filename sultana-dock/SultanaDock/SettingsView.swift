import AppKit
import SwiftUI

/// Connection and behaviour settings.
struct SettingsView: View {
    @ObservedObject var preferences: AppPreferences
    @ObservedObject var status: AppStatus
    let onConnect: () -> Void

    @State private var host = ""
    @State private var portText = ""
    @State private var token = ""
    @State private var importError: String?

    var body: some View {
        Form {
            Section {
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
                Text(status.text).foregroundStyle(status.isConnected ? Color.green : Color.secondary)
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
        }
    }

    private func save() {
        guard let port = Int(portText) else { return }
        preferences.saveConnection(host: host, port: port, token: token)
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
}
