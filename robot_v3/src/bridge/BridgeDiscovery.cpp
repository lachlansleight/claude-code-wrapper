#include "BridgeDiscovery.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <errno.h>
#include <lwip/sockets.h>

#include "../core/DebugLog.h"

namespace BridgeDiscovery {

namespace {

static constexpr uint32_t kLastKnownTimeoutMs = 1500;
static constexpr uint32_t kSweepTimeoutMs = 500;
static constexpr uint32_t kHealthTimeoutMs = 1000;
static constexpr uint32_t kPollMs = 20;
// Must stay below lwIP's ARP_TABLE_SIZE (10), see BridgeDiscovery.h.
static constexpr size_t kMaxInFlight = 8;

struct Probe {
  int fd = -1;
  uint8_t host = 0;
  uint32_t startedMs = 0;
};

// Start a non-blocking connect. Returns the socket (connect in progress
// or already complete) or -1. @p outOfSockets distinguishes "lwIP has no
// socket to give us right now" from "this host failed outright".
int startConnect(const IPAddress& ip, uint16_t port, bool& outOfSockets) {
  outOfSockets = false;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    outOfSockets = true;
    return -1;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = (uint32_t)ip;
  addr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
    outOfSockets = (errno == ENOBUFS || errno == ENOMEM);
    close(fd);
    return -1;
  }
  return fd;
}

// Call once select() reports the socket writable: a non-blocking connect
// signals both success and failure that way, SO_ERROR tells them apart.
bool connectSucceeded(int fd) {
  int err = 0;
  socklen_t len = sizeof(err);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return false;
  return err == 0;
}

// Ask a connected socket for /api/health and check the reply looks like
// the bridge: HTTP 200 with a JSON body carrying "ok": true.
bool isBridge(int fd) {
  // HTTP/1.0 so the reply is never chunked and the server closes after it.
  static const char kRequest[] = "GET /api/health HTTP/1.0\r\nHost: bridge\r\n\r\n";
  if (send(fd, kRequest, sizeof(kRequest) - 1, 0) < 0) return false;

  char buf[1024];
  size_t len = 0;
  const uint32_t t0 = millis();
  while (len < sizeof(buf) - 1) {
    const uint32_t elapsed = millis() - t0;
    if (elapsed >= kHealthTimeoutMs) break;
    const uint32_t remaining = kHealthTimeoutMs - elapsed;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    struct timeval tv;
    tv.tv_sec = remaining / 1000;
    tv.tv_usec = (remaining % 1000) * 1000;
    if (select(fd + 1, &rset, nullptr, nullptr, &tv) <= 0) break;

    const int n = recv(fd, buf + len, sizeof(buf) - 1 - len, 0);
    if (n <= 0) break;
    len += (size_t)n;
  }
  buf[len] = '\0';

  if (strncmp(buf, "HTTP/1.", 7) != 0) return false;
  const char* status = strchr(buf, ' ');
  if (!status || atoi(status + 1) != 200) return false;
  const char* body = strstr(buf, "\r\n\r\n");
  if (!body) return false;

  JsonDocument doc;
  if (deserializeJson(doc, body + 4)) return false;
  return doc["ok"] | false;
}

bool probe(const IPAddress& ip, uint16_t port, uint32_t timeoutMs) {
  bool outOfSockets = false;
  const int fd = startConnect(ip, port, outOfSockets);
  if (fd < 0) return false;

  fd_set wset;
  FD_ZERO(&wset);
  FD_SET(fd, &wset);
  struct timeval tv;
  tv.tv_sec = timeoutMs / 1000;
  tv.tv_usec = (timeoutMs % 1000) * 1000;
  const bool ok = select(fd + 1, nullptr, &wset, nullptr, &tv) > 0 && connectSucceeded(fd) &&
                  isBridge(fd);
  close(fd);
  return ok;
}

bool resolve(const char* host, IPAddress& out) {
  if (!host || !*host) return false;
  if (out.fromString(host)) return true;
  return WiFi.hostByName(host, out) == 1;
}

bool sweep(const IPAddress& self, uint16_t port, IPAddress& outIp, ProgressHandler onProgress) {
  Probe probes[kMaxInFlight];
  size_t inFlight = 0;
  uint16_t nextHost = 1;
  uint16_t scanned = 0;
  uint16_t reported = 0;
  bool found = false;

  if (onProgress) onProgress(0, kSweepHosts);

  while (!found && (nextHost <= kSweepHosts || inFlight > 0)) {
    // Top up the in-flight window.
    bool starved = false;
    for (Probe& p : probes) {
      while (p.fd < 0 && nextHost <= kSweepHosts && !starved) {
        const uint8_t host = (uint8_t)nextHost;
        if (host == self[3]) {
          ++nextHost;
          ++scanned;
          continue;
        }
        bool outOfSockets = false;
        const int fd =
            startConnect(IPAddress(self[0], self[1], self[2], host), port, outOfSockets);
        if (fd < 0 && outOfSockets && inFlight > 0) {
          // Retry this host once an in-flight probe frees its socket.
          starved = true;
          break;
        }
        ++nextHost;
        if (fd < 0) {
          ++scanned;
          continue;
        }
        p.fd = fd;
        p.host = host;
        p.startedMs = millis();
        ++inFlight;
      }
    }

    if (inFlight > 0) {
      fd_set wset;
      FD_ZERO(&wset);
      int maxFd = -1;
      for (const Probe& p : probes) {
        if (p.fd < 0) continue;
        FD_SET(p.fd, &wset);
        if (p.fd > maxFd) maxFd = p.fd;
      }
      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = kPollMs * 1000;
      const int ready = select(maxFd + 1, nullptr, &wset, nullptr, &tv);
      // Sampled before any health check: a slow check on one socket must
      // not expire the others before they get their own select() verdict.
      const uint32_t now = millis();

      for (Probe& p : probes) {
        if (p.fd < 0) continue;
        const bool done = ready > 0 && FD_ISSET(p.fd, &wset);
        if (!done && now - p.startedMs < kSweepTimeoutMs) continue;

        if (done && connectSucceeded(p.fd) && isBridge(p.fd)) {
          outIp = IPAddress(self[0], self[1], self[2], p.host);
          found = true;
        }
        close(p.fd);
        p.fd = -1;
        --inFlight;
        ++scanned;
        if (found) break;
      }
    }

    if (onProgress && scanned != reported) {
      reported = scanned;
      onProgress(scanned, kSweepHosts);
    }
  }

  for (Probe& p : probes) {
    if (p.fd >= 0) close(p.fd);
  }
  return found;
}

}  // namespace

bool find(const char* lastKnownHost, uint16_t port, String& outHost,
          ProgressHandler onProgress) {
  IPAddress lastKnown;
  if (resolve(lastKnownHost, lastKnown)) {
    LOG_INFO("bridge discovery: trying last known %s:%u", lastKnownHost, (unsigned)port);
    if (probe(lastKnown, port, kLastKnownTimeoutMs)) {
      LOG_INFO("bridge discovery: found at last known %s:%u", lastKnownHost, (unsigned)port);
      outHost = lastKnownHost;
      return true;
    }
  }

  const IPAddress self = WiFi.localIP();
  if (self == IPAddress(0, 0, 0, 0)) {
    LOG_WARN("bridge discovery: no local IP, cannot scan");
    return false;
  }

  LOG_INFO("bridge discovery: scanning %u.%u.%u.1-%u port %u", self[0], self[1], self[2],
           (unsigned)kSweepHosts, (unsigned)port);
  const uint32_t t0 = millis();
  IPAddress foundIp;
  if (!sweep(self, port, foundIp, onProgress)) {
    LOG_WARN("bridge discovery: no bridge found (%lums)", (unsigned long)(millis() - t0));
    return false;
  }

  outHost = foundIp.toString();
  LOG_INFO("bridge discovery: found at %s:%u (%lums)", outHost.c_str(), (unsigned)port,
           (unsigned long)(millis() - t0));
  return true;
}

}  // namespace BridgeDiscovery
