// SunUsbLink.h — the page's WebSocket + HTTP traffic carried over the USB serial port.
//
//   browser ── http://localhost:8080 ──▶ firmware/tools/usb_bridge.py ── USB (CH340, 115200) ──▶ ESP32-GATEWAY
//
// Runs alongside Wi-Fi, never instead of it: the same handlers serve both, so the page works identically over SUN-DISC
// (http://192.168.4.1) and over the cable. One message per line; anything the ESP prints that doesn't start with '@'
// is a log line and the bridge shows it as such.
//
//   host → ESP                              ESP → host
//   ?                  hello / keepalive    @hello <firmware>
//   T <text>           WebSocket text       @ws <text>                 WebSocket text (to every client)
//   B <base64>         WebSocket binary     @bin <base64>              WebSocket binary
//   G <path>           HTTP GET             @get <path> <code> <base64 body>
//   P <path> <base64>  HTTP POST            @post <path> <code>
//
// The host says "?" every few seconds; while it does (active()), the firmware streams status to it as well.
// Binary frames are dropped rather than queued when the TX buffer is full, so a live mirror can never stall a task.
//
// Usage: usblink::begin("gateway_rig_test"); register onText / onBinary / onGet / onPost; call usblink::poll() from one
// task every few ms; send with usblink::text() / usblink::binary() wherever the firmware calls ws.textAll / ws.binaryAll.
#pragma once
#include <Arduino.h>
#include "mbedtls/base64.h"

namespace usblink {

using TextFn    = void (*)(const String &msg);
using BinaryFn  = void (*)(const uint8_t *data, size_t len);
using GetFn     = String (*)();
using PostFn    = void (*)(const String &body);
using ConnectFn = void (*)();

static constexpr uint32_t ACTIVE_MS  = 10000;      // host counts as connected for this long after its last line
static constexpr size_t   MAX_LINE   = 4096;       // longest accepted host line (a base64 config import fits)
static constexpr int      MAX_ROUTES = 6;

namespace detail {
struct Route { const char *path; GetFn get; PostFn post; const char *type; };
inline const char *&firmware()   { static const char *f = "unknown"; return f; }
inline TextFn    &textFn()       { static TextFn f = nullptr; return f; }
inline BinaryFn  &binaryFn()     { static BinaryFn f = nullptr; return f; }
inline ConnectFn &connectFn()    { static ConnectFn f = nullptr; return f; }
inline Route     *routes()       { static Route r[MAX_ROUTES] = {}; return r; }
inline uint32_t  &lastSeen()     { static uint32_t t = 0; return t; }

inline String encode(const uint8_t *data, size_t len) {
  size_t n = 0;
  mbedtls_base64_encode(nullptr, 0, &n, data, len);
  String out; out.reserve(n);
  char *buf = (char *)malloc(n + 1); if (!buf) return out;
  if (mbedtls_base64_encode((unsigned char *)buf, n + 1, &n, data, len) == 0) { buf[n] = 0; out = buf; }
  free(buf);
  return out;
}
inline size_t decode(const String &b64, uint8_t *out, size_t cap) {
  size_t n = 0;
  return mbedtls_base64_decode(out, cap, &n, (const unsigned char *)b64.c_str(), b64.length()) == 0 ? n : 0;
}
// One write per line: the UART driver serialises whole writes, so a line can't be split by a log print from another task.
inline bool writeLine(const String &line, bool dropIfBusy) {
  if (dropIfBusy && Serial.availableForWrite() < (int)line.length() + 1) return false;
  String l = line; l += '\n';
  Serial.write((const uint8_t *)l.c_str(), l.length());
  return true;
}
inline Route *findRoute(const String &path) {
  for (int i = 0; i < MAX_ROUTES; i++) if (routes()[i].path && path == routes()[i].path) return &routes()[i];
  return nullptr;
}
inline Route *addRoute(const char *path) {
  for (int i = 0; i < MAX_ROUTES; i++) { Route &r = routes()[i]; if (!r.path || !strcmp(r.path, path)) { r.path = path; return &r; } }
  return nullptr;
}

inline void handleLine(const String &s) {
  const bool wasActive = lastSeen() && millis() - lastSeen() < ACTIVE_MS;
  lastSeen() = millis(); if (!lastSeen()) lastSeen() = 1;
  if (s == "?") {
    writeLine(String("@hello ") + firmware(), false);
    if (!wasActive && connectFn()) connectFn()();
    return;
  }
  if (s.length() < 2 || s[1] != ' ') return;
  const String arg = s.substring(2);
  switch (s[0]) {
    case 'T': if (textFn()) textFn()(arg); break;
    case 'B': {
      if (!binaryFn()) break;
      size_t cap = arg.length() * 3 / 4 + 3; uint8_t *buf = (uint8_t *)malloc(cap); if (!buf) break;
      size_t n = decode(arg, buf, cap); if (n) binaryFn()(buf, n);
      free(buf); break;
    }
    case 'G': {
      Route *r = findRoute(arg);
      if (r && r->get) { String body = r->get(); writeLine("@get " + arg + " 200 " + encode((const uint8_t *)body.c_str(), body.length()), false); }
      else writeLine("@get " + arg + " 404 ", false);
      break;
    }
    case 'P': {
      int sp = arg.indexOf(' '); String path = sp < 0 ? arg : arg.substring(0, sp);
      Route *r = findRoute(path);
      if (!r || !r->post) { writeLine("@post " + path + " 404", false); break; }
      String b64 = sp < 0 ? "" : arg.substring(sp + 1);
      size_t cap = b64.length() * 3 / 4 + 3; uint8_t *buf = (uint8_t *)malloc(cap + 1);
      if (!buf) { writeLine("@post " + path + " 500", false); break; }
      size_t n = decode(b64, buf, cap); buf[n] = 0;
      r->post(String((const char *)buf));
      free(buf);
      writeLine("@post " + path + " 200", false);
      break;
    }
  }
}
}  // namespace detail

// Replaces Serial.begin(). The TX buffer holds one status line or one mirror frame without blocking; RX only needs to
// cover the gap between poll() calls (heap is the scarce resource on this board — the Wi-Fi page needs it).
inline void begin(const char *firmware, unsigned long baud = 115200) {
  detail::firmware() = firmware;
  Serial.setRxBufferSize(1024);
  Serial.setTxBufferSize(2048);
  Serial.begin(baud);
}
inline void onText(TextFn fn)       { detail::textFn() = fn; }
inline void onBinary(BinaryFn fn)   { detail::binaryFn() = fn; }
inline void onConnect(ConnectFn fn) { detail::connectFn() = fn; }   // host (re)appeared — e.g. re-send the full state
inline void onGet(const char *path, GetFn fn, const char *type = "application/json") {
  if (detail::Route *r = detail::addRoute(path)) { r->get = fn; r->type = type; }
}
inline void onPost(const char *path, PostFn fn) { if (detail::Route *r = detail::addRoute(path)) r->post = fn; }

inline bool active() { uint32_t t = detail::lastSeen(); return t && millis() - t < ACTIVE_MS; }

// Read whatever has arrived and dispatch complete lines. Call from one task only.
inline void poll() {
  static String line;
  static bool overflow = false;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() && !overflow) detail::handleLine(line);
      line = ""; overflow = false;
    } else if (line.length() < MAX_LINE) line += c;
    else overflow = true;                              // too long: drop the whole line
  }
}

// Send to the host if one is listening. text() always goes out; binary() is skipped when the TX buffer is busy.
inline void text(const String &msg) { if (active()) detail::writeLine("@ws " + msg, false); }
inline bool binary(const uint8_t *data, size_t len) {
  return active() && detail::writeLine("@bin " + detail::encode(data, len), true);
}

}  // namespace usblink
