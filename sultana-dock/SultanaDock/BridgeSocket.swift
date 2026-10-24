import Foundation

/// Where the bridge is and how to authenticate.
struct BridgeEndpoint: Equatable {
    var host: String
    var port: Int
    var token: String

    var isComplete: Bool { !host.isEmpty && (1...65535).contains(port) && !token.isEmpty && url != nil }
    var displayName: String { "\(host):\(port)" }

    var url: URL? {
        var components = URLComponents()
        components.scheme = "ws"
        components.host = host
        components.port = port
        components.path = "/ws"
        return components.url
    }
}

@MainActor
protocol BridgeSocketDelegate: AnyObject {
    func bridgeSocket(_ socket: BridgeSocket, didChangeStatus status: BridgeSocket.Status)
    func bridgeSocketDidOpen(_ socket: BridgeSocket)
    func bridgeSocketDidClose(_ socket: BridgeSocket)
    func bridgeSocket(_ socket: BridgeSocket, didReceive text: String)
}

/// WebSocket client for the bridge, with the robot's connection policy
/// (robot_v3/src/bridge/BridgeClient.cpp): a ping every 15 s, a 3 s pong
/// timeout, two misses and the socket is dropped, reconnect after 2 s.
/// Everything happens on the main thread.
@MainActor
final class BridgeSocket: NSObject {
    enum Status: Equatable {
        case stopped
        case notConfigured
        case connecting
        case connected
        case unauthorized
        case waitingToRetry(String)
    }

    static let heartbeatInterval: TimeInterval = 15
    static let heartbeatTimeout: TimeInterval = 3
    static let heartbeatMaxMisses = 2
    static let reconnectDelay: TimeInterval = 2
    static let unauthorizedRetryDelay: TimeInterval = 15

    weak var delegate: BridgeSocketDelegate?

    private(set) var status: Status = .stopped {
        didSet {
            guard status != oldValue else { return }
            Log.bridge.info("status: \(String(describing: self.status), privacy: .public)")
            delegate?.bridgeSocket(self, didChangeStatus: status)
        }
    }
    private(set) var endpoint: BridgeEndpoint?
    var isOpen: Bool { openTask != nil }

    private var session: URLSession!
    private var task: URLSessionWebSocketTask?
    private var openTask: URLSessionWebSocketTask?
    private var reconnectTimer: Timer?
    private var heartbeatTimer: Timer?
    private var pongTimer: Timer?
    private var missedPongs = 0

    override init() {
        super.init()
        session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: .main)
    }

    func start(_ endpoint: BridgeEndpoint) {
        stop()
        self.endpoint = endpoint
        guard endpoint.isComplete else {
            status = .notConfigured
            return
        }
        connect()
    }

    func stop() {
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        let current = task
        task = nil
        finishOpen()
        current?.cancel(with: .goingAway, reason: nil)
        status = .stopped
    }

    /// Cuts short a wait to reconnect, for when the bridge has just come up.
    func retryNow() {
        guard reconnectTimer != nil else { return }
        reconnectTimer?.invalidate()
        reconnectTimer = nil
        connect()
    }

    func send(_ text: String) {
        guard let openTask else { return }
        openTask.send(.string(text)) { error in
            if let error { Log.bridge.warning("send failed: \(error.localizedDescription, privacy: .public)") }
        }
    }

    // MARK: Connection lifecycle

    private func connect() {
        guard let endpoint, let url = endpoint.url else { return }
        status = .connecting
        var request = URLRequest(url: url)
        request.setValue("Bearer \(endpoint.token)", forHTTPHeaderField: "Authorization")
        request.timeoutInterval = 5
        let newTask = session.webSocketTask(with: request)
        newTask.maximumMessageSize = 16 << 20
        task = newTask
        newTask.resume()
        receive(on: newTask)
    }

    private func receive(on socketTask: URLSessionWebSocketTask) {
        socketTask.receive { [weak self] result in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self, socketTask === self.task else { return }
                    switch result {
                    case .success(.string(let text)):
                        self.delegate?.bridgeSocket(self, didReceive: text)
                        self.receive(on: socketTask)
                    case .success(.data(let data)):
                        if let text = String(data: data, encoding: .utf8) {
                            self.delegate?.bridgeSocket(self, didReceive: text)
                        }
                        self.receive(on: socketTask)
                    case .success:
                        self.receive(on: socketTask)
                    case .failure:
                        break  // didCompleteWithError reports the end of the task.
                    }
                }
            }
        }
    }

    fileprivate func taskDidOpen(_ socketTask: URLSessionWebSocketTask) {
        guard socketTask === task else { return }
        openTask = socketTask
        missedPongs = 0
        status = .connected
        startHeartbeat()
        delegate?.bridgeSocketDidOpen(self)
    }

    fileprivate func taskDidEnd(_ endedTask: URLSessionTask, error: Error?) {
        guard endedTask === task else { return }
        task = nil
        finishOpen()
        let httpStatus = (endedTask.response as? HTTPURLResponse)?.statusCode
        if httpStatus == 401 {
            status = .unauthorized
            scheduleReconnect(after: Self.unauthorizedRetryDelay)
        } else {
            status = .waitingToRetry(error?.localizedDescription ?? "connection closed")
            scheduleReconnect(after: Self.reconnectDelay)
        }
    }

    private func finishOpen() {
        stopHeartbeat()
        if openTask != nil {
            openTask = nil
            delegate?.bridgeSocketDidClose(self)
        }
    }

    private func scheduleReconnect(after delay: TimeInterval) {
        reconnectTimer?.invalidate()
        reconnectTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.connect() }
        }
    }

    // MARK: Heartbeat

    private func startHeartbeat() {
        stopHeartbeat()
        heartbeatTimer = Timer.scheduledTimer(withTimeInterval: Self.heartbeatInterval, repeats: true) {
            [weak self] _ in
            MainActor.assumeIsolated { self?.sendHeartbeat() }
        }
    }

    private func stopHeartbeat() {
        heartbeatTimer?.invalidate()
        heartbeatTimer = nil
        pongTimer?.invalidate()
        pongTimer = nil
    }

    private func sendHeartbeat() {
        guard let socketTask = openTask else { return }
        pongTimer?.invalidate()
        pongTimer = Timer.scheduledTimer(withTimeInterval: Self.heartbeatTimeout, repeats: false) {
            [weak self] _ in
            MainActor.assumeIsolated { self?.pongMissed(on: socketTask) }
        }
        socketTask.sendPing { [weak self] error in
            DispatchQueue.main.async {
                MainActor.assumeIsolated {
                    guard let self, socketTask === self.openTask, error == nil else { return }
                    self.pongTimer?.invalidate()
                    self.pongTimer = nil
                    self.missedPongs = 0
                }
            }
        }
    }

    private func pongMissed(on socketTask: URLSessionWebSocketTask) {
        guard socketTask === openTask else { return }
        missedPongs += 1
        Log.bridge.warning("missed pong \(self.missedPongs)/\(Self.heartbeatMaxMisses)")
        if missedPongs >= Self.heartbeatMaxMisses {
            socketTask.cancel(with: .goingAway, reason: nil)
        }
    }
}

extension BridgeSocket: URLSessionWebSocketDelegate {
    // The session's delegate queue is the main queue.
    nonisolated func urlSession(
        _ session: URLSession, webSocketTask: URLSessionWebSocketTask, didOpenWithProtocol protocol: String?
    ) {
        MainActor.assumeIsolated { taskDidOpen(webSocketTask) }
    }

    nonisolated func urlSession(
        _ session: URLSession, webSocketTask: URLSessionWebSocketTask,
        didCloseWith closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?
    ) {
        MainActor.assumeIsolated { taskDidEnd(webSocketTask, error: nil) }
    }

    nonisolated func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        MainActor.assumeIsolated { taskDidEnd(task, error: error) }
    }
}
