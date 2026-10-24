import Foundation

/// Robots connected to the bridge, from its GET /api/clients: a robot is a
/// WebSocket client whose User-Agent is arduinoWebSockets' fixed
/// `arduino-WebSocket-Client`. Polled every 3 s while Settings shows it.
@MainActor
final class RobotMonitor: ObservableObject {
    struct Robot: Equatable, Identifiable {
        let id: String
        let address: String
        let connectedAt: Date
    }

    enum Status: Equatable {
        case checking
        case connected([Robot])
        case unavailable(String)
    }

    static let pollInterval: TimeInterval = 3

    @Published private(set) var status: Status = .checking

    private let preferences: AppPreferences
    private var timer: Timer?

    init(preferences: AppPreferences) {
        self.preferences = preferences
    }

    func start() {
        guard timer == nil else { return }
        poll()
        timer = Timer.scheduledTimer(withTimeInterval: Self.pollInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.poll() }
        }
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    private struct ClientList: Decodable {
        struct Client: Decodable {
            let client_id: String
            let address: String
            let user_agent: String
            let connected_at: Double
        }
        let clients: [Client]
    }

    private func poll() {
        let endpoint = preferences.endpoint
        var components = URLComponents()
        components.scheme = "http"
        components.host = endpoint.host
        components.port = endpoint.port
        components.path = "/api/clients"
        guard endpoint.isComplete, let url = components.url else {
            status = .unavailable("Set up the bridge first.")
            return
        }
        var request = URLRequest(url: url, timeoutInterval: 2)
        request.setValue("Bearer \(endpoint.token)", forHTTPHeaderField: "Authorization")
        URLSession.shared.dataTask(with: request) { [weak self] data, response, error in
            let httpStatus = (response as? HTTPURLResponse)?.statusCode
            let status: Status
            if let data, httpStatus == 200, let list = try? JSONDecoder().decode(ClientList.self, from: data) {
                status = .connected(
                    list.clients
                        .filter { $0.user_agent.contains("arduino-WebSocket") }
                        .map {
                            Robot(
                                id: $0.client_id, address: $0.address,
                                connectedAt: Date(timeIntervalSince1970: $0.connected_at / 1000))
                        })
            } else if httpStatus == 404 {
                status = .unavailable("This bridge is too old to list robots.")
            } else if httpStatus == 401 {
                status = .unavailable("The bridge rejected the token.")
            } else {
                status = .unavailable("Can't reach the bridge.")
            }
            DispatchQueue.main.async {
                MainActor.assumeIsolated { self?.status = status }
            }
        }.resume()
    }
}

enum LocalNetwork {
    /// This Mac's IPv4 address on its local network (Wi-Fi or Ethernet), the
    /// bridge host a robot's setup portal asks for. en0 first.
    static func ipv4Address() -> String? {
        var head: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&head) == 0, let first = head else { return nil }
        defer { freeifaddrs(head) }
        var found: [(interface: String, address: String)] = []
        for pointer in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let entry = pointer.pointee
            let flags = Int32(entry.ifa_flags)
            guard let address = entry.ifa_addr, address.pointee.sa_family == UInt8(AF_INET),
                flags & IFF_UP != 0, flags & IFF_RUNNING != 0, flags & IFF_LOOPBACK == 0
            else { continue }
            // Wi-Fi and Ethernet are en*; skip VPN tunnels, bridges and AirDrop.
            let interface = String(cString: entry.ifa_name)
            guard interface.hasPrefix("en") else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            guard
                getnameinfo(
                    address, socklen_t(address.pointee.sa_len), &host, socklen_t(host.count), nil, 0, NI_NUMERICHOST)
                    == 0
            else { continue }
            let text = String(cString: host)
            if text.hasPrefix("169.254.") { continue }  // self-assigned: no network
            found.append((interface, text))
        }
        return found.min { $0.interface.compare($1.interface, options: .numeric) == .orderedAscending }?.address
    }
}
