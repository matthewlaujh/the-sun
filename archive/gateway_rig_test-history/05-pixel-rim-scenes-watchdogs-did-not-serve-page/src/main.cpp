// main.cpp — gateway_rig_test: Olimex ESP32-GATEWAY → PixLite E4-S Mk3 (Expanded) → the sun (25 boards, 2,417 px, 8 runs, 23 universes)
//
//   phone/laptop ──WiFi AP "SUN-DISC"──▶ ESP32-GATEWAY ──Ethernet (direct)──▶ PixLite E4-S ──runs 1–8──▶ A, B1–B12, C1–C12
//
// Deliberately small: three modes (off · solid · scene), the simulator scenes + palettes (scenes.h), the dome-shading ring tuner,
// OTA, config export/import. No pixel map / live mirror / channel testers on the page any more (see gateway_disc_test and git history).
//
// • Tasks: core 1 = output task only (render → shading/master → Art-Net at up to 40 fps); core 0 = Wi-Fi/lwIP + housekeeping task
//   (status, OTA, NVS saves) + a render helper that takes an adaptive share of each scene frame. Arduino loop() is empty.
// • Output is linear; the PixLite does gamma 2.2 + dither. Master boots at 30 %, ×0.90 power cap always applied.
// • px i → universe cfg.uni − 1 + SUN_RUN_UNI0[run] + RI/128, channel 1 + 4·(RI mod 128), RGBW (PixLite set to GRBW).
// • OTA: ArduinoOTA on the AP (192.168.4.1:3232). `pio run -e ota -t upload` from a laptop joined to SUN-DISC.

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>
#include "index_html_gz.h"   // generated from index_html.h by tools/gen_html_gz.py (platformio.ini extra_scripts)
#include "index_html.h"      // the raw page too, served at /raw as a no-gzip fallback
#include "sun_map.h"
#pragma GCC optimize("O2", "fast-math")   // render path: Arduino builds -Os; the Xtensa FPU has no divide, fast-math lets GCC use reciprocals
#include "scenes.h"          // simulator patterns + fluid shaders + palettes

// ---------------- user settings ----------------
static const char *AP_SSID = "SUN-DISC";
static const char *AP_PASS = "sunshine26";      // ≥ 8 chars
static const uint8_t AP_CHANNEL = 6;
static const char *OTA_PASS = "sunshine26-ota"; // must match upload_flags --auth in platformio.ini
#ifndef UI_DARK
#define UI_DARK 0                                 // -DUI_DARK=1 → dark page theme
#endif

#define N               SUN_N                     // 2417
#define BRIGHTNESS_CAP  SUN_BRIGHTNESS_CAP        // 0.90 power budget — keep
#define FRAME_MS        25                        // 40 fps target
#define MIN_YIELD_MS    5                         // output task always sleeps at least this long per frame
#define STATUS_LED      33                        // ESP32-GATEWAY user LED
#define W_PER_PX_FULL   0.40f                     // 967 W / 2417 px at full RGBW white (wiring notes §7)

#define DEF_ETH_IP      "10.10.0.10"
#define DEF_TARGET_IP   "10.10.0.50"              // PixLite E4-S static IP
#define DEF_UNIVERSE    1                         // Art-Net universe of the plan's universe 1
#define CFG_VERSION     1                         // bump when the defaults above change → stale NVS settings are discarded

// ESP32-GATEWAY Ethernet (LAN8720, rev C and later)
#define ETH_PHY_ADDR_   0
#define ETH_POWER_PIN_  5
#define ETH_MDC_PIN_    23
#define ETH_MDIO_PIN_   18
#define ETH_CLK_MODE_   ETH_CLOCK_GPIO17_OUT

// ---------------- state (written by the web/housekeeping side, read by the output task) ----------------
enum Mode : uint8_t { M_OFF, M_SOLID, M_SCENE, M_COUNT };
static volatile uint8_t  mode      = M_OFF;
static uint8_t           colA[4]   = {255, 255, 255, 255};   // RGBW — solid colour, and the tint for Rotating rays / Radial pulse
static volatile uint8_t  master    = 77;         // 0..255, ~30 % on boot
static volatile int8_t   sceneId   = 0;
static volatile int8_t   palId     = 0;          // -1 = pattern's own colours, 0.. = sc::PALETTES index
static volatile uint8_t  sceneSpd  = 10;         // ×0.1 → 0.1 … 3.0
static volatile uint8_t  sceneCon  = 100;        // ×0.01 → contrast 0.25 … 2
static volatile int8_t   sceneWarm = 0;          // ×0.01 → warmth −1 … 1
static volatile uint8_t  sceneP1   = 50;         // 0..100 → p1 0..1
static volatile uint32_t renderUs  = 0;
static sc::ib_t         *intent    = nullptr;    // per-pixel RGBW intent, 16-bit fixed point (19 kB)

// dome shading — gain per pixel ring (29 rings at 30 mm pitch, ring = R/30). ×100, 0 … 200. Saved in NVS.
#define RINGS 29
static const uint8_t SHADE_DEF[RINGS] = {100,100,100,100,100,100,100,100, 100,100,100,100,100,100,100,100,100,  // 0–480 mm (A + inner B)
                                         100,100,105,110,110,110,                                              // 510–660 mm (outer B)
                                         115,110,95,80,60,40};                                                 // 690–840 mm (C)
static uint8_t  shadeRing[RINGS];
static volatile bool shadeOn = true, shadeDirty = false; static volatile uint32_t shadeDirtyMs = 0;
static uint8_t  shadeGain[N];                    // per-pixel gain ×64 (64 = unity, 128 = 2×)
static volatile int8_t   hlRing = -1;            // ring being tuned on the page: lit magenta (−1 none, −2 = inner group 0–16); auto-clears
static volatile uint32_t hlMs = 0;
static const uint8_t HL_COL[4] = {255, 0, 255, 0};
static void buildShade() {
  for (int i = 0; i < N; i++) { int ring = (sc::gR10[i] + 150) / 300; if (ring >= RINGS) ring = RINGS - 1; shadeGain[i] = (uint8_t)((shadeRing[ring] * 64 + 50) / 100); }
}

static volatile float    pwrW[3]   = {0, 0, 0};  // estimated load: disc, East, West (W) of the frame being sent
static volatile bool     otaActive = false;
// crash-loop guard: abnormal resets (panic / WDT / brown-out) are counted in RTC memory; 3 in a row → SAFE MODE: no render helper,
// scenes disabled, output off, page still served and says so. Counter clears after 90 s of healthy uptime. Power cycle also clears it.
RTC_NOINIT_ATTR uint32_t crashMagic; RTC_NOINIT_ATTR uint32_t crashCount;
static bool safeMode = false; static int bootReason = 0;
RTC_NOINIT_ATTR uint32_t wdRestarts;             // restarts WE triggered (web server dead / heap / AP) — also feed the safe-mode count
#define WDT_TIMEOUT_MS 12000                     // task watchdog: output task and housekeeping task must each check in every 12 s or the board reboots
static volatile uint32_t selfOk = 0, selfFail = 0, selfFailRow = 0, serverRestarts = 0; static volatile bool selfEverOk = false;
static const char *lastWd = "";                  // last watchdog action, for the page
static bool abnormalReset(int r) { return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT; }
static void wdRestart(const char *why) { Serial.printf("[wd] %s → restarting\n", why); wdRestarts++; Serial.flush(); delay(100); ESP.restart(); }

struct Cfg { IPAddress target; uint16_t uni; IPAddress ethIp; } cfg;
static volatile bool cfgChanged = false;
static volatile bool     ethLink = false;
static volatile uint32_t pktErr = 0, framesSent = 0;
static volatile uint32_t lastWebMs = 0;         // last HTTP request / WS connect: render single-core for a few seconds so the page loads fast
static TaskHandle_t outTask = nullptr;

Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP udp;

// ---------------- universe table (pixels of a universe are contiguous in index order) ----------------
struct Uni { uint16_t first, cnt; uint8_t num; };
static Uni unis[SUN_UNIVERSES]; static int nUni = 0;
static void buildTables() {
  nUni = 0;
  for (int i = 0; i < N; i++) {
    uint8_t u = sun_universe(i);
    if (nUni == 0 || unis[nUni - 1].num != u) { unis[nUni] = { (uint16_t)i, 0, u }; nUni++; }
    unis[nUni - 1].cnt++;
  }
}

// ---------------- config persistence ----------------
static void loadCfg() {
  prefs.begin("sunrig", true);
  if (prefs.getUChar("ver", 0) != CFG_VERSION) { prefs.end(); prefs.begin("sunrig", false); prefs.clear(); prefs.putUChar("ver", CFG_VERSION); prefs.end(); prefs.begin("sunrig", true); }
  shadeOn = prefs.getBool("shon", true);
  if (prefs.getBytesLength("shr") == RINGS) prefs.getBytes("shr", shadeRing, RINGS); else memcpy(shadeRing, SHADE_DEF, RINGS);
  cfg.uni = prefs.getUShort("uni", DEF_UNIVERSE);
  if (!cfg.target.fromString(prefs.getString("tip", DEF_TARGET_IP))) cfg.target.fromString(DEF_TARGET_IP);
  if (!cfg.ethIp.fromString(prefs.getString("eip", DEF_ETH_IP)))     cfg.ethIp.fromString(DEF_ETH_IP);
  prefs.end();
}
static void saveCfg()   { prefs.begin("sunrig", false); prefs.putUChar("ver", CFG_VERSION); prefs.putUShort("uni", cfg.uni); prefs.putString("tip", cfg.target.toString()); prefs.putString("eip", cfg.ethIp.toString()); prefs.end(); }
static void saveShade() { prefs.begin("sunrig", false); prefs.putBool("shon", shadeOn); prefs.putBytes("shr", shadeRing, RINGS); prefs.end(); }
static void applyEthIp() { IPAddress mask(255, 255, 255, 0), gw(0, 0, 0, 0); ETH.config(cfg.ethIp, gw, mask); }

static void onNetEvent(arduino_event_id_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_ETH_START:        ETH.setHostname("sun-gateway"); break;
    case ARDUINO_EVENT_ETH_CONNECTED:    ethLink = true;  Serial.println("[eth] link up"); break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: ethLink = false; Serial.println("[eth] link down"); break;
    case ARDUINO_EVENT_ETH_STOP:         ethLink = false; break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("[wifi] station joined SUN-DISC"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[wifi] station left SUN-DISC"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:   Serial.println("[wifi] station got an IP"); break;
    default: break;
  }
}

// ---------------- Art-Net ----------------
static uint8_t artSeq = 1;
static bool sendArtDmx(uint16_t universe, const uint8_t *data, uint16_t len) {
  uint8_t p[18 + 512];
  memcpy(p, "Art-Net\0", 8);
  p[8] = 0x00; p[9] = 0x50; p[10] = 0; p[11] = 14; p[12] = artSeq; p[13] = 0;
  p[14] = universe & 0xFF; p[15] = (universe >> 8) & 0x7F;
  if (len & 1) len++;
  p[16] = len >> 8; p[17] = len & 0xFF;
  memcpy(p + 18, data, len);
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!udp.beginPacket(cfg.target, 6454)) return false;
    udp.write(p, 18 + len);
    if (udp.endPacket()) return true;              // errno 12 (ENOMEM) = lwIP out of pbufs mid-burst: give it a tick and try once more
    vTaskDelay(1);
  }
  return false;
}

// ---------------- scene rendering on both cores ----------------
struct SceneJob { uint8_t *out; int scene; float t, speed, p1, con, warm; int pal; float tint[3]; int i0, i1; };
static SceneJob helperJob;
static TaskHandle_t helperTask = nullptr;
static volatile bool helperBusy = false;         // one job in flight, ever
static volatile uint32_t helperTimeouts = 0;
static int     sceneSplit = N / 2;
static uint8_t hbuf[N * 4];                      // helper renders here; main copies [0,split) in after the helper reports done
static float   sceneT = 0;
static void helperTaskFn(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!helperBusy) continue;
    const SceneJob j = helperJob; const int mid = (j.i0 + j.i1) / 2;
    sc::render(j.out, intent, j.scene, j.t, j.speed, j.p1, j.tint, j.pal, j.con, j.warm, j.i0, mid);
    vTaskDelay(1);                               // core 0's idle task (task WDT) and Wi-Fi get a slot every frame
    sc::render(j.out, intent, j.scene, j.t, j.speed, j.p1, j.tint, j.pal, j.con, j.warm, mid, j.i1);
    helperBusy = false;
    xTaskNotifyGive(outTask);
  }
}
static void renderScene(uint8_t *out) {
  const float tint[3] = {colA[0] / 255.0f, colA[1] / 255.0f, colA[2] / 255.0f};
  const int scn = sceneId, pal = palId; const float t = sceneT, sp = sceneSpd * 0.1f, p1 = sceneP1 * 0.01f, con = sceneCon * 0.01f, wa = sceneWarm * 0.01f;
  if (!helperTask || helperBusy || millis() - lastWebMs < 4000) { sc::render(out, intent, scn, t, sp, p1, tint, pal, con, wa, 0, N); return; }
  ulTaskNotifyTake(pdTRUE, 0);
  helperJob = { hbuf, scn, t, sp, p1, con, wa, pal, {tint[0], tint[1], tint[2]}, 0, sceneSplit };
  helperBusy = true;
  const int64_t a = esp_timer_get_time();
  xTaskNotifyGive(helperTask);
  sc::render(out, intent, scn, t, sp, p1, tint, pal, con, wa, sceneSplit, N);
  const int64_t b = esp_timer_get_time();
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300)) == 0) { helperTimeouts = helperTimeouts + 1; sceneSplit = N / 4; return; }   // late: keep last frame's bytes there, go single-core next frame
  const int64_t c = esp_timer_get_time();
  memcpy(out, hbuf, sceneSplit * 4);
  const int64_t mine = b - a, waited = c - b;
  if (waited > 1500) sceneSplit -= 40; else if (waited < 300 && mine > 2000) sceneSplit += 40;
  if (sceneSplit < N / 8) sceneSplit = N / 8; if (sceneSplit > N * 3 / 5) sceneSplit = N * 3 / 5;
}

// ---------------- output task (core 1) ----------------
static void outputTask(void *) {
  static uint8_t out[N * 4];
  uint32_t lastMs = millis(), frameStart = 0;
  esp_task_wdt_add(NULL);                        // a frame that never finishes reboots the board instead of freezing the rig
  for (;;) {
    esp_task_wdt_reset();
    { uint32_t spent = millis() - frameStart; uint32_t wait = spent < FRAME_MS ? FRAME_MS - spent : 0; if (wait < MIN_YIELD_MS) wait = MIN_YIELD_MS;
      vTaskDelay(pdMS_TO_TICKS(wait)); frameStart = millis(); }
    const uint32_t nowMs = millis(); float dt = (nowMs - lastMs) * 0.001f; lastMs = nowMs; if (dt > 0.25f) dt = 0.25f;
    if (cfgChanged) { cfgChanged = false; applyEthIp(); }
    sceneT += dt;

    switch (mode) {
      case M_SOLID: for (int i = 0; i < N; i++) memcpy(out + i * 4, colA, 4); break;
      case M_SCENE: if (intent) { const int64_t t0 = esp_timer_get_time(); renderScene(out); renderUs = (uint32_t)(esp_timer_get_time() - t0); } break;
      default: memset(out, 0, sizeof out); break;
    }

    // ring gain × master × cap (8.8 fixed point), plus the PSU load estimate of what actually leaves
    const uint32_t k = (uint32_t)(master * BRIGHTNESS_CAP * 256.0f / 255.0f);
    const bool shade = shadeOn;
    uint32_t sum[3] = {0, 0, 0};
    for (int i = 0; i < N; i++) {
      uint8_t *o = out + i * 4;
      const uint32_t kk = shade ? (k * shadeGain[i]) >> 6 : k;
      uint32_t v0 = (o[0] * kk) >> 8, v1 = (o[1] * kk) >> 8, v2 = (o[2] * kk) >> 8, v3 = (o[3] * kk) >> 8;
      o[0] = v0 > 255 ? 255 : v0; o[1] = v1 > 255 ? 255 : v1; o[2] = v2 > 255 ? 255 : v2; o[3] = v3 > 255 ? 255 : v3;
      const uint8_t z = sc::gZone[i];
      sum[z == 0 ? 0 : (z <= 3 ? 1 : 2)] += o[0] + o[1] + o[2] + o[3];
    }
    for (int z = 0; z < 3; z++) pwrW[z] = sum[z] * (W_PER_PX_FULL / (4.0f * 255.0f));

    if (hlRing != -1 && millis() - hlMs < 2500) {                     // light the ring being tuned
      const int hr = hlRing;
      for (int i = 0; i < N; i++) { int ring = (sc::gR10[i] + 150) / 300; if (ring == hr || (hr == -2 && ring <= 16)) { uint8_t *o = out + i * 4; for (int c = 0; c < 4; c++) o[c] = (HL_COL[c] * k) >> 8; } }
    } else if (hlRing != -1) hlRing = -1;

    if (ethLink) {
      bool ok = true;
      for (int u = 0; u < nUni; u++) ok &= sendArtDmx(cfg.uni - 1 + unis[u].num, out + unis[u].first * 4, unis[u].cnt * 4);
      if (++artSeq == 0) artSeq = 1;
      if (!ok) pktErr = pktErr + 1;
    }
    framesSent = framesSent + 1;
    digitalWrite(STATUS_LED, (framesSent >> 3) & 1);
  }
}

// ---------------- web ----------------
static String jsonStr(const String &s, const char *key) {   // tiny extractor — enough for our own page
  int k = s.indexOf(String("\"") + key + "\""); if (k < 0) return "";
  int c = s.indexOf(':', k); if (c < 0) return "";
  int a = c + 1; while (a < (int)s.length() && (s[a] == ' ' || s[a] == '"')) a++;
  int b = a; while (b < (int)s.length() && s[b] != '"' && s[b] != ',' && s[b] != '}') b++;
  return s.substring(a, b);
}
static void parseCol(const String &v, uint8_t *c) {     // "r,g,b,w"
  int p = 0;
  for (int j = 0; j < 4; j++) { int q = v.indexOf(',', p); if (q < 0) q = v.length(); long x = v.substring(p, q).toInt(); c[j] = x < 0 ? 0 : (x > 255 ? 255 : x); p = q + 1; if (p > (int)v.length()) break; }
}
static String colStr(const uint8_t *c) { return String(c[0]) + "," + c[1] + "," + c[2] + "," + c[3]; }
static inline long clampl(long x, long lo, long hi) { return x < lo ? lo : (x > hi ? hi : x); }

static void handleText(const String &s) {
  String t = jsonStr(s, "t"), v;
  if (t == "set") {                                   // any subset of the state
    v = jsonStr(s, "mode");  if (v.length()) { long n = v.toInt(); if (n >= 0 && n < M_COUNT && !(safeMode && n == M_SCENE)) mode = n; }
    v = jsonStr(s, "a");     if (v.length()) parseCol(v, colA);
    v = jsonStr(s, "master");if (v.length()) master = clampl(v.toInt(), 0, 255);
    v = jsonStr(s, "sc");    if (v.length()) { long n = v.toInt(); if (n >= 0 && n < sc::N_SCENES) { sceneId = n; sceneP1 = (uint8_t)(sc::SCENES[n].p1d * 100 + 0.5f); palId = sc::SCENES[n].noPal ? -1 : (palId < 0 ? 0 : palId); } }
    v = jsonStr(s, "pal");   if (v.length()) palId = clampl(v.toInt(), -1, sc::N_PAL - 1);
    v = jsonStr(s, "ss");    if (v.length()) sceneSpd = clampl(v.toInt(), 1, 30);
    v = jsonStr(s, "con");   if (v.length()) sceneCon = clampl(v.toInt(), 25, 200);
    v = jsonStr(s, "wa");    if (v.length()) sceneWarm = clampl(v.toInt(), -100, 100);
    v = jsonStr(s, "p1");    if (v.length()) sceneP1 = clampl(v.toInt(), 0, 100);
    return;
  }
  if (t == "shade") {                                 // {"on":0/1} · {"ring":n,"g":gain} · {"inner":gain} (rings 0–16) · {"reset":1} · {"hl":ring|-2|-1}
    v = jsonStr(s, "on"); if (v.length()) shadeOn = v.toInt() != 0;
    String g = jsonStr(s, "g"), ring = jsonStr(s, "ring"), inner = jsonStr(s, "inner"), reset = jsonStr(s, "reset"), hl = jsonStr(s, "hl");
    if (ring.length() && g.length()) { long r = ring.toInt(); if (r >= 0 && r < RINGS) shadeRing[r] = clampl(g.toInt(), 0, 200); }
    if (inner.length()) { long x = clampl(inner.toInt(), 0, 200); for (int r = 0; r <= 16; r++) shadeRing[r] = x; }
    if (reset.length() && reset.toInt()) memcpy(shadeRing, SHADE_DEF, RINGS);
    if (hl.length()) { long h = hl.toInt(); hlRing = h < -2 || h >= RINGS ? -1 : h; hlMs = millis(); }
    if (!g.length() && !inner.length() && !reset.length() && !jsonStr(s, "on").length()) return;   // highlight-only: nothing to rebuild or save
    buildShade(); shadeDirtyMs = millis(); shadeDirty = true;
    return;
  }
  if (t == "cfg") {
    IPAddress tip, eip;
    if (tip.fromString(jsonStr(s, "tip"))) cfg.target = tip;
    if (eip.fromString(jsonStr(s, "eip")) && !(eip == cfg.ethIp)) { cfg.ethIp = eip; cfgChanged = true; }
    long u = jsonStr(s, "uni").toInt(); if (u >= 1 && u < 32768 - SUN_UNIVERSES) cfg.uni = u;
    saveCfg();
    Serial.printf("[cfg] → %s uni %u, eth %s\n", cfg.target.toString().c_str(), cfg.uni, cfg.ethIp.toString().c_str());
  }
}

static void onWs(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) { lastWebMs = millis(); Serial.println("[ws] client connected"); return; }
  if (type == WS_EVT_DISCONNECT) { Serial.println("[ws] client disconnected"); return; }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || len == 0) return;
  if (info->opcode == WS_TEXT) handleText(String((const char *)data, len));
}

static void pushStatus() {
  static uint32_t lastFrames = 0, lastMs = 0;
  uint32_t now = millis();
  float fps = lastMs ? (framesSent - lastFrames) * 1000.0f / (now - lastMs) : 0;
  lastFrames = framesSent; lastMs = now;
  String j = "{\"t\":\"st\",\"link\":"; j += ethLink ? "true" : "false";
  j += ",\"speed\":" + String(ethLink ? (int)ETH.linkSpeed() : 0) + ",\"eip\":\"" + cfg.ethIp.toString() + "\",\"tip\":\"" + cfg.target.toString() + "\",\"uni\":" + String((int)cfg.uni);
  j += ",\"fps\":" + String((int)(fps + 0.5f)) + ",\"err\":" + String((unsigned long)pktErr) + ",\"ren\":" + String(mode == M_SCENE ? (int)(renderUs / 1000) : 0);
  j += ",\"mode\":" + String((int)mode) + ",\"a\":\"" + colStr(colA) + "\",\"master\":" + String((int)master);
  j += ",\"sc\":" + String((int)sceneId) + ",\"pal\":" + String((int)palId) + ",\"ss\":" + String((int)sceneSpd) + ",\"con\":" + String((int)sceneCon) + ",\"wa\":" + String((int)sceneWarm) + ",\"p1\":" + String((int)sceneP1);
  j += ",\"pw\":[" + String(pwrW[0], 0) + "," + String(pwrW[1], 0) + "," + String(pwrW[2], 0) + "]";
  j += ",\"shon\":" + String(shadeOn ? 1 : 0) + ",\"shk\":[";
  for (int r = 0; r < RINGS; r++) { if (r) j += ","; j += String(shadeRing[r]); }
  j += "],\"hto\":" + String((unsigned long)helperTimeouts) + ",\"rst\":" + String((int)esp_reset_reason()) + ",\"up\":" + String(millis() / 1000);
  j += ",\"sta\":" + String((int)WiFi.softAPgetStationNum()) + ",\"heap\":" + String((unsigned long)ESP.getFreeHeap()) + ",\"ota\":" + String(otaActive ? 1 : 0) + ",\"safe\":" + String(safeMode ? 1 : 0) + ",\"crashes\":" + String((unsigned long)(crashCount + wdRestarts)) + ",\"self\":[" + String((unsigned long)selfOk) + "," + String((unsigned long)selfFail) + "," + String((unsigned long)serverRestarts) + "],\"wd\":\"" + lastWd + "\""
     + ",\"build\":\"" __DATE__ " " __TIME__ "\",\"dark\":" + String((int)UI_DARK) + "}";
  ws.textAll(j);
}

// ---------------- config export / import ----------------
static String configJson() {
  String j = "{\"v\":2,\"build\":\"" __DATE__ " " __TIME__ "\",\"net\":{\"tip\":\"" + cfg.target.toString() + "\",\"uni\":" + String((int)cfg.uni) + ",\"eip\":\"" + cfg.ethIp.toString() + "\"}";
  j += ",\"shade\":{\"on\":" + String(shadeOn ? 1 : 0) + ",\"rings_mm\":\"index × 30 mm; gains ×100\",\"rings\":[";
  for (int r = 0; r < RINGS; r++) { if (r) j += ","; j += String(shadeRing[r]); }
  j += "]},\"state\":{\"mode\":" + String((int)mode) + ",\"a\":\"" + colStr(colA) + "\",\"master\":" + String((int)master)
     + ",\"sc\":" + String((int)sceneId) + ",\"pal\":" + String((int)palId) + ",\"ss\":" + String((int)sceneSpd) + ",\"con\":" + String((int)sceneCon) + ",\"wa\":" + String((int)sceneWarm) + ",\"p1\":" + String((int)sceneP1) + "}}";
  return j;
}
static void applyConfig(const String &s) {
  IPAddress tip, eip; String v;
  if (tip.fromString(jsonStr(s, "tip"))) cfg.target = tip;
  if (eip.fromString(jsonStr(s, "eip")) && !(eip == cfg.ethIp)) { cfg.ethIp = eip; cfgChanged = true; }
  v = jsonStr(s, "uni"); if (v.length()) { long u = v.toInt(); if (u >= 1 && u < 32768 - SUN_UNIVERSES) cfg.uni = u; }
  saveCfg();
  int k = s.indexOf("\"rings\"");
  if (k >= 0) { int a = s.indexOf('[', k), b = s.indexOf(']', a);
    if (a > 0 && b > a) { int r = 0, p = a + 1; while (p < b && r < RINGS) { int q = s.indexOf(',', p); if (q < 0 || q > b) q = b; shadeRing[r++] = clampl(s.substring(p, q).toInt(), 0, 200); p = q + 1; } } }
  k = s.indexOf("\"shade\""); if (k >= 0) { int o = s.indexOf("\"on\"", k); if (o > 0) { int c = s.indexOf(':', o); shadeOn = s.substring(c + 1).toInt() != 0; } }
  buildShade(); saveShade();
  k = s.indexOf("\"state\""); if (k >= 0) { int a = s.indexOf('{', k), b = s.indexOf('}', a); if (a > 0 && b > a) handleText("{\"t\":\"set\"," + s.substring(a + 1, b) + "}"); }
  Serial.println("[cfg] config imported");
}

// ---------------- OTA ----------------
static uint8_t masterBeforeOta = 77;
static void otaSetup() {
  ArduinoOTA.setPort(3232); ArduinoOTA.setHostname("sun-gateway"); ArduinoOTA.setPassword(OTA_PASS); ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([] { otaActive = true; masterBeforeOta = master; master = 0; vTaskDelay(pdMS_TO_TICKS(FRAME_MS * 3));
    if (outTask) { esp_task_wdt_delete(outTask); vTaskSuspend(outTask); } esp_task_wdt_delete(NULL);   // no watchdog during the upload
    Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([] { Serial.println("[ota] done → reboot"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] error %u — resuming\n", (unsigned)e); master = masterBeforeOta; otaActive = false;
    esp_task_wdt_add(NULL); if (outTask) { vTaskResume(outTask); esp_task_wdt_add(outTask); } });
  ArduinoOTA.begin();
}

// ---------------- housekeeping task (core 0) ----------------
// Self-test: fetch our own /ping over TCP (lwIP loopback). If it has ever worked and then fails 3× in a row, restart the web server;
// if that doesn't bring it back, reboot. Never acts if loopback never worked on this build (then the test is simply unsupported).
static bool selfTest() {
  WiFiClient c; c.setTimeout(2);
  if (!c.connect(IPAddress(192, 168, 4, 1), 80)) return false;
  c.print("GET /ping HTTP/1.0\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n");
  uint32_t t0 = millis(); String line;
  while (millis() - t0 < 2000) { if (c.available()) { line = c.readStringUntil('\n'); break; } vTaskDelay(pdMS_TO_TICKS(10)); }
  c.stop();
  return line.indexOf("200") > 0;
}
static void houseTask(void *) {
  uint32_t tStatus = 0, tSelf = 20000, tBeat = 0, heapLowSince = 0, apDeadSince = 0;
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    ArduinoOTA.handle();                            // blocks for the duration of an upload (watchdog unsubscribed in onStart)
    uint32_t now = millis();
    if (shadeDirty && now - shadeDirtyMs > 1500) { shadeDirty = false; saveShade(); }
    if (now - tStatus >= 1000) { tStatus = now; ws.cleanupClients(); if (ws.count() && ws.availableForWriteAll()) pushStatus(); }
    if ((crashCount || wdRestarts) && now > 90000) { crashCount = 0; wdRestarts = 0; Serial.println("[sun] 90 s healthy — reset counters cleared"); }

    // heap watchdog: the web server dies quietly when lwIP can't get buffers — act before that
    const uint32_t heap = ESP.getFreeHeap();
    if (heap < 22000) { if (!heapLowSince) heapLowSince = now; else if (now - heapLowSince > 10000) wdRestart("heap below 22 kB for 10 s"); }
    else heapLowSince = 0;

    // AP watchdog: the access point must exist and have its address
    const bool apUp = (WiFi.getMode() & WIFI_MODE_AP) && WiFi.softAPIP() == IPAddress(192, 168, 4, 1);
    if (!apUp) { if (!apDeadSince) apDeadSince = now; else if (now - apDeadSince > 15000) wdRestart("access point down for 15 s"); }
    else apDeadSince = 0;

    // web-server self-test every 30 s (skipped while a client is mid-OTA)
    if (now - tSelf >= 30000 && !otaActive) {
      tSelf = now;
      if (selfTest()) { selfOk++; selfFailRow = 0; selfEverOk = true; lastWd = ""; }
      else {
        selfFail++; selfFailRow++;
        Serial.printf("[wd] self-test failed (%u in a row, ever ok: %d)\n", (unsigned)selfFailRow, selfEverOk ? 1 : 0);
        if (selfEverOk) {
          if (selfFailRow == 3) { Serial.println("[wd] restarting web server"); server.end(); vTaskDelay(pdMS_TO_TICKS(200)); server.begin(); serverRestarts++; lastWd = "web server restarted"; }
          else if (selfFailRow >= 6) wdRestart("web server unresponsive after restart");
        }
      }
    }
    if (now - tBeat >= 60000) { tBeat = now; Serial.printf("[sun] up %lus · %d Wi-Fi client(s) · %u frames · %u send errors · heap %u · self-test ok %u / fail %u\n", now / 1000, (int)WiFi.softAPgetStationNum(), (unsigned)framesSent, (unsigned)pktErr, (unsigned)heap, (unsigned)selfOk, (unsigned)selfFail); }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ---------------- setup ----------------
void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  bootReason = (int)esp_reset_reason();
  if (crashMagic != 0x50A1234u) { crashMagic = 0x50A1234u; crashCount = 0; wdRestarts = 0; }   // first boot after power-up: RTC memory is garbage
  if (abnormalReset(bootReason)) crashCount++; else if (bootReason == ESP_RST_POWERON) { crashCount = 0; wdRestarts = 0; }
  safeMode = crashCount + wdRestarts >= 3;
  { esp_task_wdt_config_t wd = {}; wd.timeout_ms = WDT_TIMEOUT_MS; wd.idle_core_mask = 1; wd.trigger_panic = true;   // keep watching core 0's idle task too
    if (esp_task_wdt_reconfigure(&wd) != ESP_OK) esp_task_wdt_init(&wd); }
  Serial.printf("\n[sun] boot · reset reason %d · abnormal resets %u · watchdog restarts %u%s · free heap %u\n", bootReason, (unsigned)crashCount, (unsigned)wdRestarts, safeMode ? " → SAFE MODE (no scenes, no helper, output off)" : "", (unsigned)ESP.getFreeHeap());
  loadCfg();
  buildTables();

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
  MDNS.begin("sun"); MDNS.addService("http", "tcp", 80);
  otaSetup();

  ws.onEvent(onWs);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {          // gzip'd page streamed straight from flash
    lastWebMs = millis(); Serial.printf("[http] GET / from %s\n", r->client()->remoteIP().toString().c_str());
    const bool gz = r->hasHeader("Accept-Encoding") && r->header("Accept-Encoding").indexOf("gzip") >= 0;
    AsyncWebServerResponse *res = gz ? r->beginResponse(200, "text/html", INDEX_HTML_GZ, (size_t)INDEX_HTML_GZ_LEN)
                                     : r->beginResponse(200, "text/html", (const uint8_t *)INDEX_HTML, strlen(INDEX_HTML));
    if (gz) res->addHeader("Content-Encoding", "gzip");
    res->addHeader("Cache-Control", "no-cache");
    r->send(res);
  });
  server.on("/raw", HTTP_GET, [](AsyncWebServerRequest *r) {       // same page, uncompressed — fallback / diagnosis
    lastWebMs = millis(); Serial.printf("[http] GET /raw from %s\n", r->client()->remoteIP().toString().c_str());
    AsyncWebServerResponse *res = r->beginResponse(200, "text/html", (const uint8_t *)INDEX_HTML, strlen(INDEX_HTML));
    res->addHeader("Cache-Control", "no-cache"); r->send(res);
  });
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *r) {      // smallest possible response — proves the server answers at all
    Serial.printf("[http] GET /ping from %s\n", r->client()->remoteIP().toString().c_str());
    r->send(200, "text/plain", String("ok · up ") + (millis() / 1000) + " s · heap " + ESP.getFreeHeap() + " · clients " + WiFi.softAPgetStationNum());
  });
  server.on("/scenes.json", HTTP_GET, [](AsyncWebServerRequest *r) {
    lastWebMs = millis(); Serial.println("[http] GET /scenes.json");
    String j = "{\"scenes\":[";
    for (int i = 0; i < sc::N_SCENES; i++) { if (i) j += ","; j += "{\"n\":\"" + String(sc::SCENES[i].name) + "\",\"g\":\"" + sc::SCENES[i].group + "\",\"p\":\"" + sc::SCENES[i].p1name + "\",\"np\":" + (sc::SCENES[i].noPal ? 1 : 0) + "}"; }
    j += "],\"pal\":[";
    for (int i = 0; i < sc::N_PAL; i++) { if (i) j += ","; j += "\"" + String(sc::PALETTES[i].name) + "\""; }
    j += "]}";
    AsyncWebServerResponse *res = r->beginResponse(200, "application/json", j); res->addHeader("Cache-Control", "max-age=86400"); r->send(res);
  });
  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse(200, "application/json", configJson());
    res->addHeader("Content-Disposition", "attachment; filename=\"sun-gateway-config.json\""); r->send(res);
  });
  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *r) { r->send(200, "text/plain", "ok"); }, nullptr,
    [](AsyncWebServerRequest *, uint8_t *data, size_t len, size_t index, size_t total) {
      static String body; if (index == 0) body = ""; body.concat((const char *)data, len);
      if (index + len >= total) { applyConfig(body); body = ""; }
    });
  server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("/"); });
  server.begin();
  Serial.printf("[sun] network up · AP %s at %s · free heap %u\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str(), (unsigned)ESP.getFreeHeap());

  // our big buffers come AFTER the Wi-Fi/Ethernet drivers have taken what they need (they want large contiguous DMA-capable blocks)
  sc::buildGeom();
  buildShade();
  if (!safeMode) intent = (sc::ib_t *)malloc(N * 4 * sizeof(sc::ib_t));
  Serial.printf("[sun] buffers allocated · free heap %u · largest block %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  xTaskCreatePinnedToCore(outputTask, "pixout", 12288, nullptr, 3, &outTask, 1);          // core 1: render + send
#ifndef SINGLE_CORE_RENDER
  if (!safeMode) xTaskCreatePinnedToCore(helperTaskFn, "pixout0", 12288, nullptr, 1, &helperTask, 0);   // core 0: below Wi-Fi/lwIP and housekeeping
#endif
  xTaskCreatePinnedToCore(houseTask, "house", 8192, nullptr, 3, nullptr, 0);             // core 0: status, OTA, saves

  Serial.printf("[sun] rig · build " __DATE__ " " __TIME__ " · %u px · %d universes\n", (unsigned)N, nUni);
  Serial.printf("[sun] AP \"%s\" / \"%s\" → http://192.168.4.1 · OTA :3232 · eth %s → %s uni %u\n", AP_SSID, AP_PASS, cfg.ethIp.toString().c_str(), cfg.target.toString().c_str(), cfg.uni);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }   // everything lives in houseTask (core 0) and outputTask (core 1)
