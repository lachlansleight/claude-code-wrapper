// sultana-replay: run a recorded bridge WebSocket log through SultanaCore and
// print what the robot would do.
//
//   sultana-replay LOG.jsonl [--seed N] [--trace] [--frames DIR]
//                  [--frame-every MS] [--step MS] [--tail MS] [--final-state]
//                  [--logs] [--frame-hash]
//
// Log format: one JSON object per line, one of
//   {"t": <ms since recording start>, "frame": { ...WebSocket text frame... }}
//   {"t": <ms>, "text": "<raw WebSocket text frame>"}
//   {"t": <ms>, "connected": true|false}
// Lines starting with '#' and blank lines are ignored. The socket counts as
// connected at t=0 unless the log says otherwise first.
//
// The clock comes from the log, advanced in --step increments (default 1 ms,
// close to the device's loop rate), so output depends only on the log and
// --seed. Frames due at a given time are injected before that time's tick,
// as on the device.

#include <ArduinoJson.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <string>
#include <vector>

#include "png.h"
#include "sultana_core.h"

namespace {

struct Entry {
  uint32_t t = 0;
  enum Kind { Frame, Connected } kind = Frame;
  std::string text;     // Frame
  bool connected = false;  // Connected
  std::string label;    // for --trace
  int line = 0;
};

struct Options {
  const char* log = nullptr;
  uint32_t seed = 1;
  bool trace = false;
  const char* framesDir = nullptr;
  uint32_t frameEvery = 250;
  uint32_t step = 1;
  uint32_t tail = 4000;
  bool finalState = false;
  bool logs = false;
  bool frameHash = false;
};

void usage() {
  fprintf(stderr,
          "usage: sultana-replay LOG.jsonl [--seed N] [--trace] [--frames DIR]\n"
          "                      [--frame-every MS] [--step MS] [--tail MS]\n"
          "                      [--final-state] [--logs] [--frame-hash]\n");
}

bool parseU32(const char* s, uint32_t* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  errno = 0;
  const unsigned long v = strtoul(s, &end, 10);
  if (errno || *end) return false;
  *out = (uint32_t)v;
  return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto value = [&](uint32_t* out) {
      if (i + 1 >= argc || !parseU32(argv[i + 1], out)) {
        fprintf(stderr, "%s needs a number\n", a);
        return false;
      }
      ++i;
      return true;
    };
    if (!strcmp(a, "--seed")) {
      if (!value(&o.seed)) return false;
    } else if (!strcmp(a, "--trace")) {
      o.trace = true;
    } else if (!strcmp(a, "--frames")) {
      if (i + 1 >= argc) return false;
      o.framesDir = argv[++i];
    } else if (!strcmp(a, "--frame-every")) {
      if (!value(&o.frameEvery) || o.frameEvery == 0) return false;
    } else if (!strcmp(a, "--step")) {
      if (!value(&o.step) || o.step == 0) return false;
    } else if (!strcmp(a, "--tail")) {
      if (!value(&o.tail)) return false;
    } else if (!strcmp(a, "--final-state")) {
      o.finalState = true;
    } else if (!strcmp(a, "--logs")) {
      o.logs = true;
    } else if (!strcmp(a, "--frame-hash")) {
      o.frameHash = true;
    } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
      return false;
    } else if (a[0] == '-') {
      fprintf(stderr, "unknown option %s\n", a);
      return false;
    } else if (!o.log) {
      o.log = a;
    } else {
      fprintf(stderr, "unexpected argument %s\n", a);
      return false;
    }
  }
  return o.log != nullptr;
}

std::string labelFor(JsonVariantConst frame) {
  const char* type = frame["type"] | "?";
  std::string s = type;
  if (!strcmp(type, "agent_event")) {
    s += ' ';
    s += (const char*)(frame["event"]["kind"] | "?");
    const char* actKind = frame["event"]["activity"]["kind"] | "";
    if (*actKind) {
      s += " (";
      s += actKind;
      s += ')';
    }
    const char* text = frame["event"]["text"] | "";
    if (*text && !strcmp(frame["event"]["kind"] | "", "notification")) {
      s += " \"";
      s.append(text, strnlen(text, 40));
      s += '"';
    }
    const char* sid = frame["session_id"] | "";
    if (*sid) {
      s += " [";
      s += sid;
      s += ']';
    }
  } else if (!strcmp(type, "active_sessions")) {
    s += " [";
    bool first = true;
    for (JsonVariantConst id : frame["session_ids"].as<JsonArrayConst>()) {
      if (!first) s += ',';
      s += (const char*)(id | "");
      first = false;
    }
    s += ']';
  }
  return s;
}

bool loadLog(const char* path, std::vector<Entry>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
    return false;
  }
  std::string line;
  int lineNo = 0;
  int c;
  bool ok = true;
  auto flush = [&]() {
    ++lineNo;
    size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos || line[start] == '#') {
      line.clear();
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, line)) {
      fprintf(stderr, "%s:%d: not valid JSON\n", path, lineNo);
      ok = false;
      line.clear();
      return;
    }
    Entry e;
    e.line = lineNo;
    if (!doc["t"].is<uint32_t>()) {
      fprintf(stderr, "%s:%d: missing \"t\"\n", path, lineNo);
      ok = false;
      line.clear();
      return;
    }
    e.t = doc["t"].as<uint32_t>();
    if (!out.empty() && e.t < out.back().t) {
      fprintf(stderr, "%s:%d: \"t\" goes backwards\n", path, lineNo);
      ok = false;
    }
    if (doc["connected"].is<bool>()) {
      e.kind = Entry::Connected;
      e.connected = doc["connected"].as<bool>();
      e.label = e.connected ? "(socket connected)" : "(socket disconnected)";
    } else if (doc["frame"].is<JsonObjectConst>()) {
      serializeJson(doc["frame"], e.text);
      e.label = labelFor(doc["frame"]);
    } else if (doc["text"].is<const char*>()) {
      e.text = doc["text"].as<const char*>();
      JsonDocument inner;
      e.label = deserializeJson(inner, e.text) ? "(invalid JSON frame)" : labelFor(inner.as<JsonVariantConst>());
    } else {
      fprintf(stderr, "%s:%d: needs \"frame\", \"text\" or \"connected\"\n", path, lineNo);
      ok = false;
      line.clear();
      return;
    }
    out.push_back(std::move(e));
    line.clear();
  };
  while ((c = fgetc(f)) != EOF) {
    if (c == '\n') flush();
    else line += (char)c;
  }
  if (!line.empty()) flush();
  fclose(f);
  return ok;
}

struct TraceState {
  std::string effectiveVerb, expression, latch, pending;
  bool operator!=(const TraceState& o) const {
    return effectiveVerb != o.effectiveVerb || expression != o.expression || latch != o.latch ||
           pending != o.pending;
  }
};

void quietLog(const char*, void*) {}

void sendToStdout(const char* json, void* user) {
  const bool trace = *(const bool*)user;
  if (trace) printf("%28s  >> %s\n", "", json);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  if (!parseArgs(argc, argv, o)) {
    usage();
    return 2;
  }
  std::vector<Entry> entries;
  if (!loadLog(o.log, entries)) return 1;
  if (o.framesDir) {
#ifdef _WIN32
    _mkdir(o.framesDir);
#else
    mkdir(o.framesDir, 0755);
#endif
  }

  if (!o.logs) sultana_core_set_log_callback(&quietLog, nullptr);
  sultana_core_set_send_callback(&sendToStdout, &o.trace);
  sultana_core_init(0, o.seed);

  bool sawExplicitConnect = !entries.empty() && entries.front().t == 0 &&
                            entries.front().kind == Entry::Connected;
  if (!sawExplicitConnect) sultana_core_set_connected(true, 0);

  const uint32_t end = (entries.empty() ? 0 : entries.back().t) + o.tail;
  size_t next = 0;
  TraceState last;
  bool haveLast = false;
  std::vector<uint8_t> rgba(SULTANA_FRAME_RGBA_BYTES);
  char stateBuf[1024];
  int frameFiles = 0;
  uint64_t frameHash = 1469598103934665603ull;

  for (uint32_t t = 0; t <= end; t += o.step) {
    while (next < entries.size() && entries[next].t <= t) {
      const Entry& e = entries[next++];
      if (o.trace) printf("t=%7u ms  << %s\n", t, e.label.c_str());
      if (e.kind == Entry::Connected) {
        sultana_core_set_connected(e.connected, t);
      } else if (!sultana_core_inject(e.text.data(), e.text.size(), t)) {
        fprintf(stderr, "%s:%d: frame is not valid JSON (dropped, as the device would)\n", o.log, e.line);
      }
    }

    if (sultana_core_tick(t) && o.frameHash) {
      // FNV-1a over every pushed frame: a cheap "did anything on screen change" check.
      const uint8_t* p = (const uint8_t*)sultana_core_frame565();
      for (size_t i = 0; i < SULTANA_SCREEN_WIDTH * SULTANA_SCREEN_HEIGHT * 2; ++i) {
        frameHash = (frameHash ^ p[i]) * 1099511628211ull;
      }
    }

    if (o.trace) {
      sultana_core_state_json(stateBuf, sizeof(stateBuf));
      JsonDocument st;
      deserializeJson(st, stateBuf);
      TraceState cur{st["effective_verb"] | "", st["expression"] | "", st["latched_session"] | "",
                     st["pending_permission"] | ""};
      if (!haveLast || cur != last) {
        printf("t=%7u ms     verb=%-20s expr=%-26s v=%+.2f a=%.2f arm=%+3d latch=%s%s%s\n", t,
               cur.effectiveVerb.c_str(), cur.expression.c_str(), (double)(st["valence"] | 0.0f),
               (double)(st["activation"] | 0.0f), (int)(st["arm_deg"] | 0),
               cur.latch.empty() ? "-" : cur.latch.c_str(), cur.pending.empty() ? "" : " pending=",
               cur.pending.c_str());
        last = cur;
        haveLast = true;
      }
    }

    if (o.framesDir && t % o.frameEvery == 0 && sultana_core_frame_count() > 0) {
      sultana_core_frame_rgba(rgba.data());
      char path[1024];
      snprintf(path, sizeof(path), "%s/frame_%07u.png", o.framesDir, t);
      if (!writePngRgb(path, rgba.data(), SULTANA_SCREEN_WIDTH, SULTANA_SCREEN_HEIGHT)) {
        fprintf(stderr, "cannot write %s\n", path);
        return 1;
      }
      ++frameFiles;
    }
  }

  if (o.frameHash) printf("frame-hash %016llx (%u frames)\n", (unsigned long long)frameHash, sultana_core_frame_count());
  if (o.finalState) {
    sultana_core_state_json(stateBuf, sizeof(stateBuf));
    printf("%s\n", stateBuf);
  }
  if (o.trace) {
    printf("end t=%u ms, %u frames pushed%s", end, sultana_core_frame_count(), frameFiles ? ", " : "\n");
    if (frameFiles) printf("%d PNGs written to %s\n", frameFiles, o.framesDir);
  } else if (frameFiles) {
    fprintf(stderr, "%d PNGs written to %s\n", frameFiles, o.framesDir);
  }
  return 0;
}
