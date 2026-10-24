import Foundation

/// The sultana Claude Code plugin: whether it's installed, and installing it
/// from the copy inside the app (Contents/Resources/ClaudePlugin, a
/// one-plugin marketplace made by bridge/embed-bridge.sh) with the user's own
/// `claude` command.
///
/// State comes from Claude Code's files in ~/.claude (or CLAUDE_CONFIG_DIR):
/// plugins/installed_plugins.json and settings.json. Installing runs
///
///   claude plugin validate <the app's ClaudePlugin folder>   (else stop)
///   claude plugin uninstall sultana@sultana        (fails if not installed)
///   claude plugin marketplace remove sultana       (fails if not added)
///   claude plugin marketplace add <the app's ClaudePlugin folder>
///   claude plugin install sultana@sultana
///
/// so one action installs, updates, re-enables, and repairs a marketplace
/// left pointing at a moved or deleted app. Validating first means a broken
/// copy stops the install before anything is removed.
@MainActor
final class ClaudePlugin: ObservableObject {
    enum State: Equatable {
        case notInstalled
        case installed
        /// Installed from another build of the app (or elsewhere).
        case outdated
        /// Installed but turned off in Claude Code.
        case disabled
    }

    static let pluginID = "sultana@sultana"
    static let marketplaceName = "sultana"

    @Published private(set) var state: State = .notInstalled
    /// Other things in Claude Code's settings that send hooks to the bridge,
    /// so every hook would arrive twice. Human-readable.
    @Published private(set) var duplicateSenders: [String] = []
    @Published private(set) var isWorking = false
    @Published private(set) var lastError: String?
    /// The claude command, once looked for; nil while looking or if not found.
    @Published private(set) var cli: URL?
    @Published private(set) var hasLookedForCLI = false

    let marketplaceURL: URL?
    let bundledVersion: String?
    private let configDirectory: URL
    private let environment: [String: String]
    private var isLookingForCLI = false

    init(
        marketplaceURL: URL? = Bundle.main.url(forResource: "ClaudePlugin", withExtension: nil),
        environment: [String: String] = ProcessInfo.processInfo.environment
    ) {
        self.marketplaceURL = marketplaceURL
        self.environment = environment
        let home = URL(fileURLWithPath: NSHomeDirectory(), isDirectory: true)
        configDirectory = environment["CLAUDE_CONFIG_DIR"].flatMap { $0.isEmpty ? nil : URL(fileURLWithPath: $0) }
            ?? home.appendingPathComponent(".claude", isDirectory: true)
        bundledVersion = marketplaceURL.flatMap {
            Self.readJSON($0.appendingPathComponent("sultana-plugin/.claude-plugin/plugin.json"))?["version"] as? String
        }
    }

    /// The same steps as the Install button, for pasting into a terminal.
    var manualCommands: String {
        let path = marketplaceURL?.path ?? "/Applications/Sultana.app/Contents/Resources/ClaudePlugin"
        return "claude plugin marketplace add \"\(path)\"\nclaude plugin install \(Self.pluginID)"
    }

    /// Re-reads Claude Code's files, and looks for the claude command the
    /// first time.
    func refresh() {
        let installed = Self.readJSON(configDirectory.appendingPathComponent("plugins/installed_plugins.json"))
        let entries = (installed?["plugins"] as? [String: Any])?[Self.pluginID] as? [[String: Any]]
        let settings = Self.readJSON(configDirectory.appendingPathComponent("settings.json"))
        let enabledPlugins = settings?["enabledPlugins"] as? [String: Any] ?? [:]

        if let entry = entries?.first {
            let version = entry["version"] as? String
            if enabledPlugins[Self.pluginID] as? Bool != true {
                state = .disabled
            } else if let bundledVersion, version != bundledVersion {
                state = .outdated
            } else {
                state = .installed
            }
        } else {
            state = .notInstalled
        }

        var duplicates: [String] = []
        if enabledPlugins.contains(where: { $0.key.hasPrefix("claude-code-bridge@") && $0.value as? Bool == true }) {
            duplicates.append("The older claude-code-bridge plugin is turned on too. Uninstall it with /plugin in Claude Code.")
        }
        if let hooks = settings?["hooks"], let data = try? JSONSerialization.data(withJSONObject: hooks),
            let text = String(data: data, encoding: .utf8),
            text.contains("claude-hook-forward") || text.contains("/hooks/claude")
        {
            duplicates.append(
                "Hooks in \(Self.tildePath(configDirectory.appendingPathComponent("settings.json"))) also send to the bridge. Remove them.")
        }
        duplicateSenders = duplicates

        lookForCLI()
    }

    func install() {
        guard let marketplaceURL else {
            lastError = "This build of Sultana doesn't include the plugin."
            return
        }
        run([
            Step(["plugin", "validate", marketplaceURL.path], mustSucceed: true),
            Step(["plugin", "uninstall", Self.pluginID], mustSucceed: false),
            Step(["plugin", "marketplace", "remove", Self.marketplaceName], mustSucceed: false),
            Step(["plugin", "marketplace", "add", marketplaceURL.path], mustSucceed: true),
            Step(["plugin", "install", Self.pluginID], mustSucceed: true),
        ])
    }

    func remove() {
        run([
            Step(["plugin", "uninstall", Self.pluginID], mustSucceed: true),
            Step(["plugin", "marketplace", "remove", Self.marketplaceName], mustSucceed: false),
        ])
    }

    // MARK: Running claude

    private struct Step {
        var arguments: [String]
        var mustSucceed: Bool
        init(_ arguments: [String], mustSucceed: Bool) {
            self.arguments = arguments
            self.mustSucceed = mustSucceed
        }
    }

    private func run(_ steps: [Step]) {
        guard !isWorking else { return }
        isWorking = true
        lastError = nil
        let environment = environment
        let knownCLI = cli
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let found = knownCLI ?? Self.findCLI(environment: environment)
            var error: String?
            if let found {
                for step in steps {
                    let (status, output) = Self.runCLI(found, step.arguments, environment: environment)
                    if status != 0 && step.mustSucceed {
                        let detail = output.isEmpty ? "exit status \(status)" : output
                        error = "claude \(step.arguments.joined(separator: " ")) failed: \(detail)"
                        break
                    }
                }
            } else {
                error = "Couldn't find the claude command."
            }
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self else { return }
                    self.cli = found
                    self.hasLookedForCLI = true
                    self.isWorking = false
                    self.lastError = error
                    self.refresh()
                }
            }
        }
    }

    private func lookForCLI() {
        guard !hasLookedForCLI, !isLookingForCLI else { return }
        isLookingForCLI = true
        let environment = environment
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let found = Self.findCLI(environment: environment)
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self else { return }
                    self.isLookingForCLI = false
                    self.cli = found
                    self.hasLookedForCLI = true
                }
            }
        }
    }

    /// Apps don't get the shell's PATH, so look where Claude Code's installers
    /// put it, then ask the user's login shell.
    nonisolated static func findCLI(environment: [String: String]) -> URL? {
        let fileManager = FileManager.default
        let home = NSHomeDirectory()
        var candidates = [
            "\(home)/.local/bin/claude",  // native installer
            "\(home)/.claude/local/claude",  // older local install
            "/opt/homebrew/bin/claude",
            "/usr/local/bin/claude",
            "\(home)/.npm-global/bin/claude",
            "\(home)/.bun/bin/claude",
            "\(home)/.volta/bin/claude",
        ]
        let nvmVersions = "\(home)/.nvm/versions/node"
        if let versions = try? fileManager.contentsOfDirectory(atPath: nvmVersions) {
            let newestFirst = versions.sorted { $0.compare($1, options: .numeric) == .orderedDescending }
            candidates += newestFirst.map { "\(nvmVersions)/\($0)/bin/claude" }
        }
        if let path = candidates.first(where: { fileManager.isExecutableFile(atPath: $0) }) {
            return URL(fileURLWithPath: path)
        }
        return claudeFromLoginShell()
    }

    /// `command -v claude` in an interactive login shell, which loads the
    /// user's PATH setup (nvm and the like). Gives up after 5 s.
    nonisolated static func claudeFromLoginShell() -> URL? {
        guard let entry = getpwuid(getuid()), let shellPath = entry.pointee.pw_shell else { return nil }
        let marker = "__sultana_claude__"
        let (_, output) = runProcess(
            URL(fileURLWithPath: String(cString: shellPath)), ["-ilc", "printf '\\n\(marker)%s\\n' \"$(command -v claude)\""],
            environment: ["HOME": NSHomeDirectory(), "USER": NSUserName(), "LOGNAME": NSUserName(), "TERM": "dumb"],
            timeout: 5)
        guard let line = output.split(separator: "\n").last(where: { $0.hasPrefix(marker) }) else { return nil }
        let path = String(line.dropFirst(marker.count))
        return path.hasPrefix("/") && FileManager.default.isExecutableFile(atPath: path) ? URL(fileURLWithPath: path) : nil
    }

    nonisolated private static func runCLI(_ cli: URL, _ arguments: [String], environment: [String: String])
        -> (Int32, String)
    {
        var environment = environment
        // An npm-installed claude is a Node script, so its own directory (where
        // that Node lives) goes first.
        environment["PATH"] = "\(cli.deletingLastPathComponent().path):/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
        environment["NO_COLOR"] = "1"
        environment["TERM"] = "dumb"
        return runProcess(cli, arguments, environment: environment, timeout: 60)
    }

    /// Runs a command to the end, returning its exit status and its stdout
    /// and stderr together, trimmed and without terminal escapes.
    nonisolated private static func runProcess(
        _ executable: URL, _ arguments: [String], environment: [String: String], timeout: TimeInterval
    ) -> (Int32, String) {
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        process.environment = environment
        process.standardInput = FileHandle.nullDevice
        let output = Pipe()
        process.standardOutput = output
        process.standardError = output
        do {
            try process.run()
        } catch {
            return (-1, error.localizedDescription)
        }
        DispatchQueue.global().asyncAfter(deadline: .now() + timeout) {
            if process.isRunning { process.terminate() }
        }
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        let text = String(decoding: data, as: UTF8.self)
            .replacingOccurrences(of: "\u{1B}\\[[0-9;?]*[A-Za-z]", with: "", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return (process.terminationStatus, text)
    }

    nonisolated private static func readJSON(_ url: URL) -> [String: Any]? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
    }

    nonisolated static func tildePath(_ url: URL) -> String {
        let home = NSHomeDirectory()
        return url.path.hasPrefix(home + "/") ? "~" + url.path.dropFirst(home.count) : url.path
    }
}
