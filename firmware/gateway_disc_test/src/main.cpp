// main.cpp — Olimex ESP32-GATEWAY → PixLite E4-S bring-up test for the sun's centre disc (board A, 173 px)
//
//   phone/laptop ──WiFi AP "SUN-DISC"──▶ ESP32-GATEWAY ──Ethernet (direct)──▶ PixLite E4-S ──run 1──▶ disc
//
// • Web page at http://192.168.4.1 (or http://sun.local): tap/drag to paint individual pixels, fill, clear, chase test,
//   and a "Fluid" mode that runs a per-pixel port of a neon-fluid GLSL shader (fluid.h) with live sliders.
// • Output: Art-Net (default) or DDP, streamed continuously at 40 fps from its own FreeRTOS task, so the web server
//   and the pixel stream never wait on each other (the web side only writes into a locked frame buffer).
// • Pixel order / universes follow simulator/sun-map.csv: px i → universe 1 + i/128, channel 1 + (i%128)*4, RGBW.
// • Settings (protocol, target IP, universe, gateway Ethernet IP) are editable on the page and kept in flash.
// • The page also works over the USB cable (SunUsbLink + firmware/tools/usb_bridge.py → http://localhost:8080), at the same
//   time as Wi-Fi: both feed the same handlers. Over USB the live mirror is capped at ~5 fps (115200 baud).
//
// Toolchain: arduino-esp32 core 3.x (2.0.x also handled below). Libraries: ESPAsyncWebServer + AsyncTCP (ESP32Async).
// PlatformIO project: `pio run -t upload && pio device monitor` (board esp32-gateway, pioarduino platform).

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <SunUsbLink.h>      // firmware/shared/SunUsbLink — the page over USB
#include "index_html.h"
#include "sun_map.h"
#include "fluid.h"

// ---------------- user settings ----------------
static const char *AP_SSID = "SUN-DISC";
static const char *AP_PASS = "sunshine26";      // ≥ 8 chars
static const uint8_t AP_CHANNEL = 6;

#define DISC_N          173                       // board A, indices 0..172 of the sun map (run 1)
#define PX_PER_UNI      128                       // 512 ch / 4 (RGBW) — matches sun_map.h SUN_PX_PER_UNIVERSE
#define BRIGHTNESS_CAP  0.90f                     // power budget from sun_map.h — keep
#define FRAME_MS        25                        // 40 fps
#define STATUS_LED      33                        // ESP32-GATEWAY user LED

// Defaults (changeable from the web page, stored in NVS)
#define DEF_ETH_IP      "10.10.0.10"              // 10.10.0.x: private range no home router uses (192.168.0.x clashed with Wi-Fi)
#define DEF_TARGET_IP   "10.10.0.50"              // PixLite E4-S static IP (unicast); 10.10.0.255 = subnet broadcast if unsure
#define CFG_VERSION     2                         // bump when the defaults above change → stale NVS settings are discarded
#define DEF_UNIVERSE    1
#define DEF_PROTO       0                         // 0 = Art-Net, 1 = DDP

// ---------------- ESP32-GATEWAY Ethernet (LAN8720, rev C and later) ----------------
#define ETH_PHY_ADDR_   0
#define ETH_POWER_PIN_  5
#define ETH_MDC_PIN_    23
#define ETH_MDIO_PIN_   18
#define ETH_CLK_MODE_   ETH_CLOCK_GPIO17_OUT

// ---------------- state ----------------
static uint8_t  px[DISC_N * 4];                  // what the page painted (RGBW)
static portMUX_TYPE pxLock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint8_t master = 77;             // 0..255, ~30 % on boot for safety
static volatile bool    chase = false;
static volatile bool    fluid = false;           // Fluid shader mode (overrides paint + chase)
static FluidParams      fp;                      // written from the web task, read by the output task (floats are atomic on ESP32)
static uint8_t          mirror[DISC_N * 4];      // last computed fluid frame, echoed to the page
static volatile bool    mirrorDirty = false;
static volatile uint8_t chaseSpeed = 8;          // px per second
static uint8_t          chaseCol[4] = {0, 0, 0, 255};
static volatile int     chaseIdx = 0;
static volatile bool    frameDirty = true;       // page state changed → echo to all clients

struct Cfg { uint8_t proto; IPAddress target; uint16_t uni; IPAddress ethIp; } cfg;
static volatile bool cfgChanged = false;

static volatile bool     ethLink = false;
static volatile uint32_t pktOk = 0, pktErr = 0, framesSent = 0;
static volatile bool     pollRequested = false;

struct Node { IPAddress ip; char name[19]; };
static Node nodes[6]; static int nodeCount = 0;
static portMUX_TYPE nodeLock = portMUX_INITIALIZER_UNLOCKED;

Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP udp;

// ---------------- config persistence ----------------
static void loadCfg() {
  prefs.begin("sundisc", true);
  if (prefs.getUChar("ver", 0) != CFG_VERSION) { prefs.end(); prefs.begin("sundisc", false); prefs.clear(); prefs.putUChar("ver", CFG_VERSION); prefs.end(); prefs.begin("sundisc", true); }
  cfg.proto = prefs.getUChar("proto", DEF_PROTO);
  cfg.uni   = prefs.getUShort("uni", DEF_UNIVERSE);
  if (!cfg.target.fromString(prefs.getString("tip", DEF_TARGET_IP))) cfg.target.fromString(DEF_TARGET_IP);
  if (!cfg.ethIp.fromString(prefs.getString("eip", DEF_ETH_IP)))     cfg.ethIp.fromString(DEF_ETH_IP);
  prefs.end();
}
static void saveCfg() {
  prefs.begin("sundisc", false);
  prefs.putUChar("ver", CFG_VERSION);
  prefs.putUChar("proto", cfg.proto);
  prefs.putUShort("uni", cfg.uni);
  prefs.putString("tip", cfg.target.toString());
  prefs.putString("eip", cfg.ethIp.toString());
  prefs.end();
}
static void applyEthIp() {
  IPAddress mask(255, 255, 255, 0), gw(0, 0, 0, 0);
  ETH.config(cfg.ethIp, gw, mask);               // static — no DHCP on a point-to-point cable
}

// ---------------- Ethernet events ----------------
static void onNetEvent(arduino_event_id_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_ETH_START:        ETH.setHostname("sun-gateway"); break;
    case ARDUINO_EVENT_ETH_CONNECTED:    ethLink = true;  Serial.println("[eth] link up"); break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: ethLink = false; Serial.println("[eth] link down"); break;
    case ARDUINO_EVENT_ETH_STOP:         ethLink = false; break;
    default: break;
  }
}

// ---------------- packet builders ----------------
static uint8_t artSeq = 1, ddpSeq = 1;

static bool sendArtDmx(uint16_t universe, const uint8_t *data, uint16_t len) {
  uint8_t p[18 + 512];
  memcpy(p, "Art-Net\0", 8);
  p[8] = 0x00; p[9] = 0x50;                      // OpDmx (LE)
  p[10] = 0; p[11] = 14;                         // protocol 14
  p[12] = artSeq; p[13] = 0;
  p[14] = universe & 0xFF; p[15] = (universe >> 8) & 0x7F;
  if (len & 1) len++;                            // Art-Net wants an even length
  p[16] = len >> 8; p[17] = len & 0xFF;
  memcpy(p + 18, data, len);
  if (!udp.beginPacket(cfg.target, 6454)) return false;
  udp.write(p, 18 + len);
  return udp.endPacket();
}

static bool sendDdp(uint32_t offset, const uint8_t *data, uint16_t len) {
  uint8_t p[10 + 1440];
  p[0] = 0x41;                                   // v1 + PUSH (single packet frame)
  p[1] = ddpSeq & 0x0F; if (!p[1]) p[1] = 1;
  p[2] = 0x00;                                   // data type: undefined / default
  p[3] = 0x01;                                   // destination: display
  p[4] = offset >> 24; p[5] = offset >> 16; p[6] = offset >> 8; p[7] = offset;
  p[8] = len >> 8; p[9] = len & 0xFF;
  memcpy(p + 10, data, len);
  if (!udp.beginPacket(cfg.target, 4048)) return false;
  udp.write(p, 10 + len);
  return udp.endPacket();
}

static void sendArtPoll() {
  uint8_t p[14] = {'A','r','t','-','N','e','t',0, 0x00,0x20, 0,14, 0x00, 0x00};
  IPAddress bcast(cfg.ethIp[0], cfg.ethIp[1], cfg.ethIp[2], 255);   // subnet broadcast → leaves via Ethernet
  udp.beginPacket(bcast, 6454); udp.write(p, sizeof p); udp.endPacket();
}

static void readUdp() {                          // drain: ArtPollReplies (+ our own broadcasts looping back)
  uint8_t b[240];
  for (int k = 0; k < 8; k++) {
    int n = udp.parsePacket();
    if (n <= 0) return;
    int r = udp.read(b, sizeof b);
    if (r >= 44 && !memcmp(b, "Art-Net\0", 8) && b[8] == 0x00 && b[9] == 0x21) {
      IPAddress ip(b[10], b[11], b[12], b[13]);
      if (ip == cfg.ethIp) continue;
      portENTER_CRITICAL(&nodeLock);
      int i = 0; while (i < nodeCount && !(nodes[i].ip == ip)) i++;
      if (i < 6) {
        nodes[i].ip = ip; memcpy(nodes[i].name, b + 26, 18); nodes[i].name[18] = 0;
        if (i == nodeCount) nodeCount++;
      }
      portEXIT_CRITICAL(&nodeLock);
    }
  }
}

// ---------------- output task (own core, fixed rate) ----------------
static void outputTask(void *) {
  static uint8_t out[DISC_N * 4];
  TickType_t last = xTaskGetTickCount();
  uint32_t chaseAcc = 0;
  for (;;) {
    vTaskDelayUntil(&last, pdMS_TO_TICKS(FRAME_MS));

    if (cfgChanged) { cfgChanged = false; applyEthIp(); }
    if (pollRequested) { pollRequested = false; portENTER_CRITICAL(&nodeLock); nodeCount = 0; portEXIT_CRITICAL(&nodeLock); sendArtPoll(); }
    readUdp();

    if (fluid) {
      const float t = millis() * 0.001f * fp.speed;
      for (int i = 0; i < DISC_N; i++) shaderPixel(SUN_X10[i] * 0.1f, SUN_Y10[i] * 0.1f, t, fp, out + i * 4);
      if ((framesSent & 3) == 0) { memcpy(mirror, out, sizeof mirror); mirrorDirty = true; }   // ~10 fps preview
    } else if (chase) {
      chaseAcc += (uint32_t)chaseSpeed * FRAME_MS;
      while (chaseAcc >= 1000) { chaseAcc -= 1000; chaseIdx = (chaseIdx + 1) % DISC_N; }
      memset(out, 0, sizeof out);
      memcpy(out + chaseIdx * 4, chaseCol, 4);
    } else {
      portENTER_CRITICAL(&pxLock); memcpy(out, px, sizeof out); portEXIT_CRITICAL(&pxLock);
    }
    const uint32_t k = (uint32_t)(master * BRIGHTNESS_CAP * 256.0f / 255.0f);   // 8.8 fixed point
    for (int i = 0; i < DISC_N * 4; i++) out[i] = (out[i] * k) >> 8;

    if (!ethLink) continue;
    bool ok = true;
    if (cfg.proto == 0) {
      for (int u = 0; u * PX_PER_UNI < DISC_N; u++) {
        int first = u * PX_PER_UNI, cnt = min(PX_PER_UNI, DISC_N - first);
        ok &= sendArtDmx(cfg.uni + u, out + first * 4, cnt * 4);
      }
      if (++artSeq == 0) artSeq = 1;
    } else {
      ok &= sendDdp(0, out, DISC_N * 4);
      ddpSeq++;
    }
    if (ok) pktOk++; else pktErr++;
    framesSent++;
    digitalWrite(STATUS_LED, (framesSent >> 3) & 1);   // ~2.5 Hz blink while streaming
  }
}

// ---------------- web socket ----------------
static void handleBinary(const uint8_t *d, size_t len) {
  switch (d[0]) {
    case 'P':                                     // n × [idx_hi idx_lo r g b w]
      portENTER_CRITICAL(&pxLock);
      for (size_t o = 1; o + 6 <= len; o += 6) {
        uint16_t i = (d[o] << 8) | d[o + 1];
        if (i < DISC_N) memcpy(px + i * 4, d + o + 2, 4);
      }
      portEXIT_CRITICAL(&pxLock);
      frameDirty = true; break;
    case 'F':                                     // fill r g b w
      if (len >= 5) {
        portENTER_CRITICAL(&pxLock);
        for (int i = 0; i < DISC_N; i++) memcpy(px + i * 4, d + 1, 4);
        portEXIT_CRITICAL(&pxLock);
        frameDirty = true;
      } break;
    case 'M': if (len >= 2) master = d[1]; break; // master brightness
    case 'C':                                     // chase on/off, speed, r g b w
      if (len >= 7) {
        chase = d[1]; chaseSpeed = d[2] ? d[2] : 1;
        memcpy(chaseCol, d + 3, 4);
        if (!(chaseCol[0] | chaseCol[1] | chaseCol[2] | chaseCol[3])) chaseCol[3] = 255;
        if (chase) chaseIdx = 0;
      } break;
  }
}

static String jsonStr(const String &s, const char *key) {   // tiny extractor — enough for our own page
  int k = s.indexOf(String("\"") + key + "\""); if (k < 0) return "";
  int c = s.indexOf(':', k); if (c < 0) return "";
  int a = c + 1; while (a < (int)s.length() && s[a] == ' ') a++;
  if (a < (int)s.length() && s[a] == '"') {             // quoted value: read to the closing quote (may contain commas)
    int b = s.indexOf('"', a + 1); return b < 0 ? "" : s.substring(a + 1, b);
  }
  int b = a; while (b < (int)s.length() && s[b] != ',' && s[b] != '}') b++;
  return s.substring(a, b);
}

static void handleText(const String &s) {
  String t = jsonStr(s, "t");
  if (t == "poll") { pollRequested = true; return; }
  if (t == "fl") {                                  // fluid params — any subset of keys
    String v;
    #define FLD(key, field, lo, hi) v = jsonStr(s, key); if (v.length()) { float x = v.toFloat(); fp.field = x < lo ? lo : (x > hi ? hi : x); }
    v = jsonStr(s, "on"); if (v.length()) fluid = v.toInt() != 0;
    FLD("speed", speed, 0, 5); FLD("zoom", zoom, -4, 4); FLD("flow", flow, 0, 6.3f);
    FLD("fx", fx, -1.5f, 1.5f); FLD("fy", fy, -1.5f, 1.5f); FLD("glow", glow, 0, 3);
    FLD("bright", bright, 0, 4); FLD("sat", sat, 0, 3); FLD("wmix", wmix, 0, 1); FLD("radius", radius, 50, 2000);
    v = jsonStr(s, "iter"); if (v.length()) { int n = v.toInt(); fp.iter = n < 1 ? 1 : (n > 8 ? 8 : n); }
    v = jsonStr(s, "sh");   if (v.length()) { int n = v.toInt(); fp.shader = n < 0 ? 0 : (n > 1 ? 1 : n); }
    #undef FLD
    return;
  }
  if (t == "cfg") {
    IPAddress tip, eip;
    if (tip.fromString(jsonStr(s, "tip"))) cfg.target = tip;
    if (eip.fromString(jsonStr(s, "eip")) && !(eip == cfg.ethIp)) { cfg.ethIp = eip; cfgChanged = true; }
    cfg.proto = jsonStr(s, "proto").toInt() ? 1 : 0;
    long u = jsonStr(s, "uni").toInt(); if (u >= 0 && u < 32768) cfg.uni = u;
    saveCfg();
    Serial.printf("[cfg] %s → %s uni %u, eth %s\n", cfg.proto ? "DDP" : "Art-Net",
                  cfg.target.toString().c_str(), cfg.uni, cfg.ethIp.toString().c_str());
  }
}

static void onWs(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) { frameDirty = true; return; }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || len == 0) return;   // our messages are single-frame
  if (info->opcode == WS_BINARY) handleBinary(data, len);
  else handleText(String((const char *)data, len));
}

static void pushFrame() {
  static uint8_t msg[1 + DISC_N * 4];
  msg[0] = 'S';
  portENTER_CRITICAL(&pxLock); memcpy(msg + 1, px, DISC_N * 4); portEXIT_CRITICAL(&pxLock);
  ws.binaryAll(msg, sizeof msg);
  usblink::binary(msg, sizeof msg);
}

static void pushStatus() {
  static uint32_t lastFrames = 0, lastMs = 0;
  uint32_t now = millis();
  float fps = lastMs ? (framesSent - lastFrames) * 1000.0f / (now - lastMs) : 0;
  lastFrames = framesSent; lastMs = now;

  String j = "{\"t\":\"st\",\"link\":"; j += ethLink ? "true" : "false";
  j += ",\"speed\":" + String(ethLink ? (int)ETH.linkSpeed() : 0);
  j += ",\"eip\":\"" + cfg.ethIp.toString() + "\",\"tip\":\"" + cfg.target.toString() + "\"";
  j += ",\"proto\":" + String((int)cfg.proto) + ",\"uni\":" + String((int)cfg.uni);
  j += ",\"fps\":" + String(ethLink ? (int)(fps + 0.5f) : 0) + ",\"err\":" + String((unsigned long)pktErr);
  j += ",\"master\":" + String((int)master) + ",\"chase\":" + String(chase ? 1 : 0) + ",\"ci\":" + String((int)chaseIdx);
  j += ",\"fl\":{\"on\":" + String(fluid ? 1 : 0) + ",\"speed\":" + String(fp.speed, 2) + ",\"zoom\":" + String(fp.zoom, 2)
     + ",\"flow\":" + String(fp.flow, 2) + ",\"fx\":" + String(fp.fx, 2) + ",\"fy\":" + String(fp.fy, 2) + ",\"glow\":" + String(fp.glow, 2)
     + ",\"bright\":" + String(fp.bright, 2) + ",\"sat\":" + String(fp.sat, 2) + ",\"wmix\":" + String(fp.wmix, 2)
     + ",\"radius\":" + String(fp.radius, 0) + ",\"iter\":" + String((int)fp.iter) + ",\"sh\":" + String((int)fp.shader) + ",\"t\":" + String(millis() * 0.001f, 2) + "}";
  j += ",\"nodes\":[";
  portENTER_CRITICAL(&nodeLock);
  Node copy[6]; int n = nodeCount; memcpy(copy, nodes, sizeof copy);
  portEXIT_CRITICAL(&nodeLock);
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    String nm = copy[i].name; nm.replace("\"", "'");
    j += "{\"ip\":\"" + copy[i].ip.toString() + "\",\"name\":\"" + nm + "\"}";
  }
  j += "]}";
  ws.textAll(j);
  usblink::text(j);
}

// ---------------- setup / loop ----------------
void setup() {
  usblink::begin("gateway_disc_test");              // Serial at 115200 + the page over USB (firmware/tools/usb_bridge.py)
  usblink::onText(handleText);
  usblink::onBinary(handleBinary);
  usblink::onConnect([] { frameDirty = true; });   // like a new WebSocket client: send the full paint buffer
  pinMode(STATUS_LED, OUTPUT);
  loadCfg();

  WiFi.onEvent(onNetEvent);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, 4);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.setSleep(false);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_POWER_PIN_, ETH_CLK_MODE_);
#else
  ETH.begin(ETH_PHY_ADDR_, ETH_POWER_PIN_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_PHY_LAN8720, ETH_CLK_MODE_);
#endif
  applyEthIp();
  udp.begin(6454);

  MDNS.begin("sun");
  MDNS.addService("http", "tcp", 80);

  ws.onEvent(onWs);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", (const uint8_t *)INDEX_HTML, strlen_P(INDEX_HTML));
  });
  server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("/"); });
  server.begin();

  // Output on core 1 at higher priority than loop(); WiFi/AsyncTCP live on core 0.
  xTaskCreatePinnedToCore(outputTask, "pixout", 6144, nullptr, 3, nullptr, 1);

  Serial.printf("\n[sun] AP \"%s\" / \"%s\" → http://192.168.4.1  ·  eth %s → %s (%s, uni %u)\n",
                AP_SSID, AP_PASS, cfg.ethIp.toString().c_str(), cfg.target.toString().c_str(),
                cfg.proto ? "DDP" : "Art-Net", cfg.uni);
}

void loop() {
  static uint32_t tFrame = 0, tStatus = 0, tUsbMirror = 0;
  usblink::poll();
  uint32_t now = millis();
  if (frameDirty && now - tFrame >= 60) { frameDirty = false; tFrame = now; pushFrame(); }
  if (fluid && mirrorDirty && (ws.count() || usblink::active())) {   // live preview of the computed frame ('L' = live, not the paint buffer)
    static uint8_t msg[1 + DISC_N * 4]; msg[0] = 'L'; mirrorDirty = false;
    memcpy(msg + 1, mirror, DISC_N * 4);
    if (ws.count()) ws.binaryAll(msg, sizeof msg);
    if (now - tUsbMirror >= 200 && usblink::binary(msg, sizeof msg)) tUsbMirror = now;   // ~5 fps fits 115200 baud
  }
  if (now - tStatus >= 1000) { tStatus = now; ws.cleanupClients(); pushStatus(); }
  delay(5);
}
