// main.cpp — gateway_rig_test: Olimex ESP32-GATEWAY → PixLite E4-S Mk3 (Expanded) → the whole sun (25 boards, 2,417 px, 8 runs, 23 universes)
//
//   phone/laptop ──WiFi AP "SUN-DISC"──▶ ESP32-GATEWAY ──Ethernet (direct)──▶ PixLite E4-S ──runs 1–8──▶ A, B1–B12, C1–C12
//
// A bring-up / channel-test tool in the spirit of Advatek Assistant's test patterns, but aware of the sun's map:
// • Select what to light: everything, any of the 8 data runs (= E4-S terminals), or individual boards.
// • Modes: off · solid · chase (per run, in wire order) · wipe · colour cycle · board step · universe step · ends (first/last px
//   of every board) · rainbow · breathe (power test) · radial pulse · spot (one pixel, tap it on the page).
// • Two colours (A, B), master brightness (boots at 30 %, ×0.90 power cap always applied), speed, chase tail, manual stepping.
// • Live estimate of the load on each PSU (disc RSP-200, East / West RSP-500) from the frame actually being sent.
// • Output: Art-Net (default) or DDP, 40 fps from its own FreeRTOS task on core 1; web server + OTA on core 0.
// • Pixel order / universes follow sun_map.h (generated from simulator/sun-map.csv, Rev Q — runs 6–8 in descending wedge order).
//   px i → universe cfg.uni − 1 + SUN_RUN_UNI0[run] + RI/128, channel 1 + 4·(RI mod 128), GRBW handled by the PixLite (we send RGBW).
// • OTA: ArduinoOTA on the same AP (192.168.4.1:3232). `pio run -e ota -t upload` from a laptop joined to SUN-DISC.
//
// Toolchain: arduino-esp32 core 3.x via pioarduino. Libraries: ESPAsyncWebServer + AsyncTCP (ESP32Async).

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <WiFi.h>
#include <ETH.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include "index_html.h"
#include "sun_map.h"
#include "scenes.h"      // simulator patterns + fluid shaders + palettes

// ---------------- user settings ----------------
static const char *AP_SSID = "SUN-DISC";
static const char *AP_PASS = "sunshine26";      // ≥ 8 chars
static const uint8_t AP_CHANNEL = 6;
static const char *OTA_PASS = "sunshine26-ota"; // must match upload_flags --auth in platformio.ini
#ifndef UI_DARK
#define UI_DARK 0                                 // build with -DUI_DARK=1 for the dark page theme (handy to see that an OTA upload landed)
#endif

#define N               SUN_N                     // 2417
#define NB              25                        // boards: A, B1–B12, C1–C12 (SunBoard order)
#define PX_PER_UNI      SUN_PX_PER_UNIVERSE       // 128 RGBW px / universe
#define BRIGHTNESS_CAP  SUN_BRIGHTNESS_CAP        // 0.90 power budget — keep
#define FRAME_MS        25                        // 40 fps target
#define MIN_YIELD_MS    5                         // output task always sleeps at least this long per frame so loop()/OTA get core 1
#define STATUS_LED      33                        // ESP32-GATEWAY user LED
#define W_PER_PX_FULL   0.40f                     // 967 W / 2417 px at full RGBW white (wiring notes §7)

// Defaults (changeable from the web page, stored in NVS)
#define DEF_ETH_IP      "10.10.0.10"
#define DEF_TARGET_IP   "10.10.0.50"              // PixLite E4-S static IP (unicast); 10.10.0.255 = broadcast if unsure
#define DEF_UNIVERSE    1                         // Art-Net universe of the plan's universe 1 (1-based on the PixLite too)
#define DEF_PROTO       0                         // 0 = Art-Net, 1 = DDP
#define CFG_VERSION     1                         // bump when the defaults above change → stale NVS settings are discarded

// ---------------- ESP32-GATEWAY Ethernet (LAN8720, rev C and later) ----------------
#define ETH_PHY_ADDR_   0
#define ETH_POWER_PIN_  5
#define ETH_MDC_PIN_    23
#define ETH_MDIO_PIN_   18
#define ETH_CLK_MODE_   ETH_CLOCK_GPIO17_OUT

static const char *BOARD_NAME[NB] = {"A","B1","B2","B3","B4","B5","B6","B7","B8","B9","B10","B11","B12",
                                     "C1","C2","C3","C4","C5","C6","C7","C8","C9","C10","C11","C12"};

// ---------------- test state (written by the web task, read by the output task) ----------------
enum Mode : uint8_t { M_OFF, M_SOLID, M_CHASE, M_WIPE, M_CYCLE, M_BOARD, M_UNI, M_ENDS, M_RAINBOW, M_BREATHE, M_RADIAL, M_SPOT, M_SCENE, M_COUNT };
static const char *MODE_NAME[M_COUNT] = {"off","solid","chase","wipe","cycle","board","universe","ends","rainbow","breathe","radial","spot","scene"};

static volatile uint8_t  mode      = M_OFF;
static volatile uint16_t runMask   = 0x1FE;      // bits 1..8 → run selected (0x1FE = all runs)
static volatile uint32_t boardMask = 0;          // bits 0..24 → board selected (in addition to runs)
static uint8_t           colA[4]   = {255, 255, 255, 255};   // RGBW
static uint8_t           colB[4]   = {255, 0, 0, 0};
static volatile uint8_t  master    = 77;         // 0..255, ~30 % on boot
static volatile uint8_t  speed     = 20;         // 1..100 (see rates below)
static volatile uint8_t  tail      = 6;          // chase tail length in px
static volatile bool     hold      = false;      // step modes: advance only on prev/next from the page
static volatile int16_t  step      = 0;          // board / universe / cycle index
static volatile uint16_t spot      = 0;          // spot pixel
static volatile bool     stepReset = false;      // page asked for a step → restart the step timer
// scene engine (simulator patterns / shaders) — see scenes.h
static volatile int8_t   sceneId   = 0;
static volatile int8_t   palId     = 0;          // -1 = pattern's own colours, 0.. = sc::PALETTES index
static volatile uint8_t  sceneSpd  = 10;         // ×0.1 → 0.1 … 3.0
static volatile uint8_t  sceneCon  = 100;        // ×0.01 → contrast 0.25 … 2
static volatile int8_t   sceneWarm = 0;          // ×0.01 → warmth −1 … 1
static volatile uint8_t  sceneP1   = 50;         // 0..100 → p1 0..1
static volatile uint32_t renderUs  = 0;          // last scene render time
// dome shading — radial gain curve to tame the rim (dome stand-off → ~8 mm there: pixels show through, band looks hot)
// and lift the dark ring just inboard of it. Knots at fixed radii (fraction of rim), gains ×100 (100 = unity, max 200). Saved in NVS.
#define SHADE_K 6
static const float   SHADE_R[SHADE_K] = {0.0f, 0.50f, 0.70f, 0.82f, 0.92f, 1.0f};
static const uint8_t SHADE_DEF[SHADE_K] = {100, 100, 110, 115, 70, 45};
static uint8_t  shadeK[SHADE_K];
static volatile bool shadeOn = true;
static volatile bool shadeDirty = false; static volatile uint32_t shadeDirtyMs = 0;
static uint8_t  shadeGain[N];                    // per-pixel gain ×64 (64 = unity, 255 ≈ 4×)
static void buildShade() {
  for (int i = 0; i < N; i++) {
    float r = SUN_R10[i] * 0.1f / SUN_RIM_R_MM; int j = 0; while (j < SHADE_K - 2 && r > SHADE_R[j + 1]) j++;
    float f = (r - SHADE_R[j]) / (SHADE_R[j + 1] - SHADE_R[j]); f = f < 0 ? 0 : (f > 1 ? 1 : f);
    float g = (shadeK[j] + (shadeK[j + 1] - shadeK[j]) * f) * 0.01f;
    int v = (int)(g * 64 + 0.5f); shadeGain[i] = v > 255 ? 255 : v;
  }
}
static float            *intent = nullptr;       // per-pixel RGBW floats (the simulator's "intent" buffer), heap-allocated in setup()

static volatile float    pwrW[3]   = {0, 0, 0};  // estimated load: disc, East, West (W) of the frame being sent
static volatile bool     otaActive = false;
static uint8_t           mirror[N * 4];          // last frame, echoed to the page
static volatile bool     mirrorDirty = false;

struct Cfg { uint8_t proto; IPAddress target; uint16_t uni; IPAddress ethIp; } cfg;
static volatile bool cfgChanged = false;

static volatile bool     ethLink = false;
static volatile uint32_t pktOk = 0, pktErr = 0, framesSent = 0;
static volatile bool     pollRequested = false;

struct Node { IPAddress ip; char name[19]; };
static Node nodes[6]; static int nodeCount = 0;
static portMUX_TYPE nodeLock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t outTask = nullptr;

Preferences prefs;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiUDP udp;

// ---------------- derived map tables (built once at boot from sun_map.h) ----------------
struct Uni { uint16_t first, cnt; uint8_t num; };     // pixels of a universe are contiguous in index order
static Uni     unis[SUN_UNIVERSES]; static int nUni = 0;
static uint16_t boardFirst[NB], boardLast[NB], boardCnt[NB];
static uint32_t runBoards[9];                         // boards on each run (bitmask)

static void buildTables() {
  for (int b = 0; b < NB; b++) { boardFirst[b] = 0xFFFF; boardLast[b] = 0; boardCnt[b] = 0; }
  for (int r = 0; r < 9; r++) runBoards[r] = 0;
  nUni = 0;
  for (int i = 0; i < N; i++) {
    uint8_t u = sun_universe(i), b = SUN_BOARD[i], r = SUN_RUN[i];
    if (nUni == 0 || unis[nUni - 1].num != u) { unis[nUni] = { (uint16_t)i, 0, u }; nUni++; }
    unis[nUni - 1].cnt++;
    if (boardFirst[b] == 0xFFFF) boardFirst[b] = i;
    boardLast[b] = i; boardCnt[b]++;
    runBoards[r] |= 1UL << b;
  }
}

static inline bool selPx(uint16_t i) { return ((runMask >> SUN_RUN[i]) & 1) || ((boardMask >> SUN_BOARD[i]) & 1); }
static inline bool selRun(uint8_t r) { return ((runMask >> r) & 1) || (runBoards[r] & boardMask); }

// ---------------- config persistence ----------------
static void loadCfg() {
  prefs.begin("sunrig", true);
  shadeOn = prefs.getBool("shon", true);
  if (prefs.getBytesLength("shk") == SHADE_K) prefs.getBytes("shk", shadeK, SHADE_K); else memcpy(shadeK, SHADE_DEF, SHADE_K);
  if (prefs.getUChar("ver", 0) != CFG_VERSION) { prefs.end(); prefs.begin("sunrig", false); prefs.clear(); prefs.putUChar("ver", CFG_VERSION); prefs.end(); prefs.begin("sunrig", true); }
  cfg.proto = prefs.getUChar("proto", DEF_PROTO);
  cfg.uni   = prefs.getUShort("uni", DEF_UNIVERSE);
  if (!cfg.target.fromString(prefs.getString("tip", DEF_TARGET_IP))) cfg.target.fromString(DEF_TARGET_IP);
  if (!cfg.ethIp.fromString(prefs.getString("eip", DEF_ETH_IP)))     cfg.ethIp.fromString(DEF_ETH_IP);
  prefs.end();
}
static void saveCfg() {
  prefs.begin("sunrig", false);
  prefs.putUChar("ver", CFG_VERSION);
  prefs.putUChar("proto", cfg.proto);
  prefs.putUShort("uni", cfg.uni);
  prefs.putString("tip", cfg.target.toString());
  prefs.putString("eip", cfg.ethIp.toString());
  prefs.end();
}
static void saveShade() { prefs.begin("sunrig", false); prefs.putBool("shon", shadeOn); prefs.putBytes("shk", shadeK, SHADE_K); prefs.end(); }
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

static bool sendDdp(uint32_t offset, const uint8_t *data, uint16_t len, bool push) {
  uint8_t p[10 + 1440];
  p[0] = 0x40 | (push ? 0x01 : 0x00);           // v1, PUSH on the last packet of the frame
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

// ---------------- pattern helpers ----------------
static inline void put(uint8_t *out, uint16_t i, const uint8_t *c) { memcpy(out + i * 4, c, 4); }
static inline void putScaled(uint8_t *out, uint16_t i, const uint8_t *c, uint8_t k) {   // k = 0..255
  uint8_t *o = out + i * 4;
  o[0] = (c[0] * k + 127) / 255; o[1] = (c[1] * k + 127) / 255; o[2] = (c[2] * k + 127) / 255; o[3] = (c[3] * k + 127) / 255;
}
static void hueToRgb(float h, uint8_t *o) {      // h in [0,1) → RGB, W = 0
  h -= floorf(h); float s = h * 6.0f; int k = (int)s; float f = s - k;
  uint8_t q = (uint8_t)(255 * (1 - f)), t = (uint8_t)(255 * f);
  switch (k) {
    case 0: o[0] = 255; o[1] = t;   o[2] = 0;   break;
    case 1: o[0] = q;   o[1] = 255; o[2] = 0;   break;
    case 2: o[0] = 0;   o[1] = 255; o[2] = t;   break;
    case 3: o[0] = 0;   o[1] = q;   o[2] = 255; break;
    case 4: o[0] = t;   o[1] = 0;   o[2] = 255; break;
    default: o[0] = 255; o[1] = 0;  o[2] = q;   break;
  }
  o[3] = 0;
}
static const uint8_t CYCLE_COLS[6][4] = {{255,0,0,0},{0,255,0,0},{0,0,255,0},{0,0,0,255},{255,255,255,0},{255,255,255,255}};   // R G B W RGBw RGBW
#define CYCLE_STEPS 7                            // + colour A

static int stepCount() {
  switch (mode) { case M_CYCLE: return CYCLE_STEPS; case M_BOARD: return NB; case M_UNI: return nUni; default: return 1; }
}

// Rates from the 1..100 speed slider
static inline float pxPerSec()   { return speed * 3.0f; }          // chase / wipe: 3 … 300 px/s
static inline uint32_t stepMs()  { return 6000 / speed; }          // step modes: 6 s … 60 ms
static inline float cyclesPerS() { return speed / 50.0f; }         // rainbow / breathe / radial: 0.02 … 2 Hz

// ---------------- frame render ----------------
static float sceneT = 0;                       // scene clock, seconds (advances by real elapsed time so motion speed is fps-independent)
static void render(uint8_t *out, float pos, float phase) {
  memset(out, 0, N * 4);
  const uint8_t m = mode;
  switch (m) {
    case M_OFF: break;

    case M_SOLID:
      for (int i = 0; i < N; i++) if (selPx(i)) put(out, i, colA);
      break;

    case M_CHASE: {                              // one head per selected run, walking in wire order (= index order within the run)
      const int T = tail < 1 ? 1 : tail;
      for (int r = 1; r <= SUN_RUNS; r++) {
        if (!selRun(r)) continue;
        const int len = SUN_RUN_LEN[r], start = SUN_RUN_START[r];
        const int head = ((int)pos) % (len + T);   // run off the end so the tail clears the last pixel
        for (int k = 0; k < T; k++) {
          int idx = head - k; if (idx < 0 || idx >= len) continue;
          uint16_t i = start + idx; if (!selPx(i)) continue;
          putScaled(out, i, colA, (uint8_t)(255 * (T - k) / T));
        }
      }
    } break;

    case M_WIPE: {                               // fill each selected run head→tail, then drain from the head
      for (int r = 1; r <= SUN_RUNS; r++) {
        if (!selRun(r)) continue;
        const int len = SUN_RUN_LEN[r], start = SUN_RUN_START[r];
        const int p = ((int)pos) % (2 * len);
        const int a = p < len ? 0 : p - len, b = p < len ? p : len;
        for (int idx = a; idx < b; idx++) { uint16_t i = start + idx; if (selPx(i)) put(out, i, colA); }
      }
    } break;

    case M_CYCLE: {
      const uint8_t *c = step < 6 ? CYCLE_COLS[step] : colA;
      for (int i = 0; i < N; i++) if (selPx(i)) put(out, i, c);
    } break;

    case M_BOARD: {                              // one board at a time (selection ignored)
      int b = step; if (b < 0 || b >= NB) break;
      for (int i = boardFirst[b]; i <= boardLast[b]; i++) if (SUN_BOARD[i] == b) put(out, i, colA);
    } break;

    case M_UNI: {                                // one universe at a time (selection ignored)
      int u = step; if (u < 0 || u >= nUni) break;
      for (int i = unis[u].first; i < unis[u].first + unis[u].cnt; i++) put(out, i, colA);
    } break;

    case M_ENDS:                                 // first px of each board = A, last px = B → counts and seams at a glance
      for (int b = 0; b < NB; b++) {
        if (!selPx(boardFirst[b])) continue;
        put(out, boardFirst[b], colA); put(out, boardLast[b], colB);
      }
      break;

    case M_RAINBOW: {                            // hue by angle, rotating
      uint8_t c[4];
      for (int i = 0; i < N; i++) {
        if (!selPx(i)) continue;
        hueToRgb(SUN_TH10[i] / 3600.0f + phase, c); put(out, i, c);
      }
    } break;

    case M_BREATHE: {                            // 0 → 100 % → 0 of colour A on the selection (PSU / thermal test)
      const float lv = 0.5f - 0.5f * cosf(phase * 6.2831853f);
      const uint8_t k = (uint8_t)(lv * 255 + 0.5f);
      for (int i = 0; i < N; i++) if (selPx(i)) putScaled(out, i, colA, k);
    } break;

    case M_RADIAL: {                             // ring expanding from the centre to the rim
      const float ring = (phase - floorf(phase)) * (SUN_RIM_R_MM + 60.0f), half = 1.5f * SUN_PITCH_MM;
      for (int i = 0; i < N; i++) {
        if (!selPx(i)) continue;
        float d = fabsf(SUN_R10[i] * 0.1f - ring); if (d >= half) continue;
        putScaled(out, i, colA, (uint8_t)(255 * (1.0f - d / half)));
      }
    } break;

    case M_SPOT:                                 // exactly one pixel (selection ignored)
      if (spot < N) put(out, spot, colA);
      break;

    case M_SCENE: {                              // simulator pattern / shader; colour A = tint for the patterns that use one
      const float tint[3] = {colA[0] / 255.0f, colA[1] / 255.0f, colA[2] / 255.0f};
      if (!intent) break;
      const int64_t t0 = esp_timer_get_time();
      sc::render(out, intent, sceneId, sceneT, sceneSpd * 0.1f, sceneP1 * 0.01f, tint, palId, sceneCon * 0.01f, sceneWarm * 0.01f);
      renderUs = (uint32_t)(esp_timer_get_time() - t0);
    } break;

    default: break;
  }
}

// ---------------- output task (own core, fixed rate) ----------------
static void outputTask(void *) {
  static uint8_t out[N * 4];
  float pos = 0, phase = 0; uint32_t stepAcc = 0; uint8_t lastMode = 0xFF;
  uint32_t lastMs = millis();
  for (;;) {
    // Pace to FRAME_MS, but ALWAYS sleep at least MIN_YIELD_MS. This task (prio 3) shares core 1 with Arduino loop() (prio 1);
    // with vTaskDelayUntil a frame longer than FRAME_MS never slept, loop() starved, and the page/OTA/status went dead.
    { static uint32_t frameStart = 0; uint32_t spent = millis() - frameStart;
      uint32_t wait = spent < FRAME_MS ? FRAME_MS - spent : 0; if (wait < MIN_YIELD_MS) wait = MIN_YIELD_MS;
      vTaskDelay(pdMS_TO_TICKS(wait)); frameStart = millis(); }
    const uint32_t nowMs = millis(); float dt = (nowMs - lastMs) * 0.001f; lastMs = nowMs; if (dt > 0.25f) dt = 0.25f;
    const uint32_t dtMs = (uint32_t)(dt * 1000 + 0.5f);

    if (cfgChanged) { cfgChanged = false; applyEthIp(); }
    if (pollRequested) { pollRequested = false; portENTER_CRITICAL(&nodeLock); nodeCount = 0; portEXIT_CRITICAL(&nodeLock); sendArtPoll(); }
    readUdp();

    // animation clocks
    if (mode != lastMode) { lastMode = mode; pos = 0; phase = 0; stepAcc = 0; if (step >= stepCount()) step = 0; }
    if (stepReset) { stepReset = false; stepAcc = 0; }
    sceneT += dt;
    pos   += pxPerSec()   * dt;
    phase += cyclesPerS() * dt; if (phase > 1e6f) phase -= 1e6f;
    if (stepCount() > 1 && !hold) {
      stepAcc += dtMs;
      const uint32_t per = stepMs();
      while (stepAcc >= per) { stepAcc -= per; step = (step + 1) % stepCount(); }
    }

    render(out, pos, phase);

    // dome shading (not for the diagnostic modes — those must show raw channels)
    if (shadeOn && mode != M_BOARD && mode != M_UNI && mode != M_ENDS && mode != M_SPOT)
      for (int i = 0; i < N; i++) { const uint32_t g = shadeGain[i]; if (g == 64) continue; uint8_t *o = out + i * 4;
        for (int c = 0; c < 4; c++) { uint32_t v = (o[c] * g) >> 6; o[c] = v > 255 ? 255 : v; } }

    // master × cap, plus the PSU load estimate of what actually leaves
    const uint32_t k = (uint32_t)(master * BRIGHTNESS_CAP * 256.0f / 255.0f);   // 8.8 fixed point
    uint32_t sum[3] = {0, 0, 0};
    for (int i = 0; i < N; i++) {
      uint8_t *o = out + i * 4;
      o[0] = (o[0] * k) >> 8; o[1] = (o[1] * k) >> 8; o[2] = (o[2] * k) >> 8; o[3] = (o[3] * k) >> 8;
      const uint8_t z = sun_zone(i);             // 0 disc · 1–3 East (wedges 1–6) · 4–6 West (7–12)
      sum[z == 0 ? 0 : (z <= 3 ? 1 : 2)] += o[0] + o[1] + o[2] + o[3];
    }
    for (int z = 0; z < 3; z++) pwrW[z] = sum[z] * (W_PER_PX_FULL / (4.0f * 255.0f));

    if ((framesSent % 10) == 0) { memcpy(mirror, out, sizeof mirror); mirrorDirty = true; }  // ~4 fps preview to the page

    if (!ethLink) { framesSent++; continue; }
    bool ok = true;
    if (cfg.proto == 0) {
      for (int u = 0; u < nUni; u++)
        ok &= sendArtDmx(cfg.uni - 1 + unis[u].num, out + unis[u].first * 4, unis[u].cnt * 4);
      if (++artSeq == 0) artSeq = 1;
    } else {
      for (uint32_t off = 0; off < N * 4; off += 1440) {
        uint16_t len = min<uint32_t>(1440, N * 4 - off);
        ok &= sendDdp(off, out + off, len, off + len >= N * 4);
      }
      ddpSeq++;
    }
    if (ok) pktOk++; else pktErr++;
    framesSent++;
    digitalWrite(STATUS_LED, (framesSent >> 3) & 1);   // ~2.5 Hz blink while streaming
  }
}

// ---------------- web socket ----------------
static String jsonStr(const String &s, const char *key) {   // tiny extractor — enough for our own page
  int k = s.indexOf(String("\"") + key + "\""); if (k < 0) return "";
  int c = s.indexOf(':', k); if (c < 0) return "";
  int a = c + 1; while (a < (int)s.length() && (s[a] == ' ' || s[a] == '"')) a++;
  int b = a; while (b < (int)s.length() && s[b] != '"' && s[b] != ',' && s[b] != '}') b++;
  return s.substring(a, b);
}
static void parseCol(const String &v, uint8_t *c) {     // "r,g,b,w"
  int p = 0;
  for (int j = 0; j < 4; j++) {
    int q = v.indexOf(',', p); if (q < 0) q = v.length();
    long x = v.substring(p, q).toInt(); c[j] = x < 0 ? 0 : (x > 255 ? 255 : x);
    p = q + 1; if (p > (int)v.length()) break;
  }
}

static void handleText(const String &s) {
  String t = jsonStr(s, "t"), v;
  if (t == "poll") { pollRequested = true; return; }

  if (t == "set") {                                   // any subset of the test state
    v = jsonStr(s, "mode");  if (v.length()) { long n = v.toInt(); if (n >= 0 && n < M_COUNT) { mode = n; } }
    v = jsonStr(s, "run");   if (v.length()) runMask = v.toInt() & 0x1FE;
    v = jsonStr(s, "board"); if (v.length()) boardMask = (uint32_t)strtoul(v.c_str(), nullptr, 10) & 0x1FFFFFF;
    v = jsonStr(s, "a");     if (v.length()) parseCol(v, colA);
    v = jsonStr(s, "b");     if (v.length()) parseCol(v, colB);
    v = jsonStr(s, "master");if (v.length()) { long n = v.toInt(); master = n < 0 ? 0 : (n > 255 ? 255 : n); }
    v = jsonStr(s, "speed"); if (v.length()) { long n = v.toInt(); speed = n < 1 ? 1 : (n > 100 ? 100 : n); }
    v = jsonStr(s, "tail");  if (v.length()) { long n = v.toInt(); tail = n < 1 ? 1 : (n > 64 ? 64 : n); }
    v = jsonStr(s, "hold");  if (v.length()) hold = v.toInt() != 0;
    v = jsonStr(s, "spot");  if (v.length()) { long n = v.toInt(); if (n >= 0 && n < N) spot = n; }
    v = jsonStr(s, "sc");    if (v.length()) { long n = v.toInt(); if (n >= 0 && n < sc::N_SCENES) { sceneId = n; sceneP1 = (uint8_t)(sc::SCENES[n].p1d * 100 + 0.5f); palId = sc::SCENES[n].noPal ? -1 : (palId < 0 ? 0 : palId); } }
    v = jsonStr(s, "pal");   if (v.length()) { long n = v.toInt(); palId = n < 0 ? -1 : (n >= sc::N_PAL ? sc::N_PAL - 1 : n); }
    v = jsonStr(s, "ss");    if (v.length()) { long n = v.toInt(); sceneSpd = n < 1 ? 1 : (n > 30 ? 30 : n); }
    v = jsonStr(s, "con");   if (v.length()) { long n = v.toInt(); sceneCon = n < 25 ? 25 : (n > 200 ? 200 : n); }
    v = jsonStr(s, "wa");    if (v.length()) { long n = v.toInt(); sceneWarm = n < -100 ? -100 : (n > 100 ? 100 : n); }
    v = jsonStr(s, "p1");    if (v.length()) { long n = v.toInt(); sceneP1 = n < 0 ? 0 : (n > 100 ? 100 : n); }
    return;
  }
  if (t == "shade") {                                 // {"on":0/1,"k":"g0,g1,g2,g3,g4,g5"} gains ×100
    v = jsonStr(s, "on"); if (v.length()) shadeOn = v.toInt() != 0;
    v = jsonStr(s, "k");
    if (v.length()) { int p = 0; for (int j = 0; j < SHADE_K; j++) { int q = v.indexOf(',', p); if (q < 0) q = v.length(); long x = v.substring(p, q).toInt(); shadeK[j] = x < 0 ? 0 : (x > 200 ? 200 : x); p = q + 1; if (p > (int)v.length()) break; } }
    v = jsonStr(s, "reset"); if (v.length() && v.toInt()) memcpy(shadeK, SHADE_DEF, SHADE_K);
    buildShade(); shadeDirtyMs = millis(); shadeDirty = true;   // saved from loop() 1.5 s after the last change (not on every slider tick)
    return;
  }
  if (t == "step") {                                  // {"d":±1} or {"to":n}
    int cnt = stepCount();
    v = jsonStr(s, "to"); if (v.length()) { long n = v.toInt(); if (n >= 0 && n < cnt) step = n; }
    v = jsonStr(s, "d");  if (v.length()) { int d = v.toInt(); step = (step + d % cnt + cnt) % cnt; }
    stepReset = true;
    return;
  }
  if (t == "cfg") {
    IPAddress tip, eip;
    if (tip.fromString(jsonStr(s, "tip"))) cfg.target = tip;
    if (eip.fromString(jsonStr(s, "eip")) && !(eip == cfg.ethIp)) { cfg.ethIp = eip; cfgChanged = true; }
    cfg.proto = jsonStr(s, "proto").toInt() ? 1 : 0;
    long u = jsonStr(s, "uni").toInt(); if (u >= 1 && u < 32768 - SUN_UNIVERSES) cfg.uni = u;
    saveCfg();
    Serial.printf("[cfg] %s → %s uni %u, eth %s\n", cfg.proto ? "DDP" : "Art-Net",
                  cfg.target.toString().c_str(), cfg.uni, cfg.ethIp.toString().c_str());
  }
}

static void onWs(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || len == 0) return;   // our messages are single-frame
  if (info->opcode == WS_TEXT) handleText(String((const char *)data, len));
}

static String colStr(const uint8_t *c) { return String(c[0]) + "," + c[1] + "," + c[2] + "," + c[3]; }

static void pushStatus() {
  static uint32_t lastFrames = 0, lastMs = 0;
  uint32_t now = millis();
  float fps = lastMs ? (framesSent - lastFrames) * 1000.0f / (now - lastMs) : 0;
  lastFrames = framesSent; lastMs = now;

  String lbl;                                        // what the current step is, for the page
  switch (mode) {
    case M_BOARD: lbl = step < NB ? BOARD_NAME[step] : ""; if (step < NB) lbl += " · " + String(boardCnt[step]) + " px · run " + String(SUN_RUN[boardFirst[step]]); break;
    case M_UNI:   if (step < nUni) lbl = "universe " + String(unis[step].num) + " (Art-Net " + String(cfg.uni - 1 + unis[step].num) + ") · " + String(unis[step].cnt) + " px · run " + String(SUN_RUN[unis[step].first]) + " · px " + String(unis[step].first) + "–" + String(unis[step].first + unis[step].cnt - 1); break;
    case M_CYCLE: { static const char *cn[CYCLE_STEPS] = {"red","green","blue","white (W)","RGB white","RGBW white","colour A"}; lbl = cn[step < CYCLE_STEPS ? step : 0]; } break;
    case M_SPOT:  lbl = "px " + String(spot) + " · " + BOARD_NAME[SUN_BOARD[spot]] + " · run " + String(SUN_RUN[spot]) + " · uni " + String(sun_universe(spot)) + " ch " + String(sun_channel(spot)); break;
    case M_SCENE: lbl = String(sc::SCENES[sceneId].group) + " · " + sc::SCENES[sceneId].name; break;
    default: break;
  }

  String j = "{\"t\":\"st\",\"link\":"; j += ethLink ? "true" : "false";
  j += ",\"speed\":" + String(ethLink ? (int)ETH.linkSpeed() : 0);
  j += ",\"eip\":\"" + cfg.ethIp.toString() + "\",\"tip\":\"" + cfg.target.toString() + "\"";
  j += ",\"proto\":" + String((int)cfg.proto) + ",\"uni\":" + String((int)cfg.uni);
  j += ",\"fps\":" + String(ethLink ? (int)(fps + 0.5f) : 0) + ",\"err\":" + String((unsigned long)pktErr);
  j += ",\"mode\":" + String((int)mode) + ",\"run\":" + String((int)runMask) + ",\"board\":" + String((unsigned long)boardMask);
  j += ",\"a\":\"" + colStr(colA) + "\",\"b\":\"" + colStr(colB) + "\"";
  j += ",\"master\":" + String((int)master) + ",\"spd\":" + String((int)speed) + ",\"tail\":" + String((int)tail);
  j += ",\"hold\":" + String(hold ? 1 : 0) + ",\"step\":" + String((int)step) + ",\"steps\":" + String(stepCount()) + ",\"spot\":" + String((int)spot);
  j += ",\"lbl\":\"" + lbl + "\"";
  j += ",\"sc\":" + String((int)sceneId) + ",\"pal\":" + String((int)palId) + ",\"ss\":" + String((int)sceneSpd) + ",\"con\":" + String((int)sceneCon)
     + ",\"wa\":" + String((int)sceneWarm) + ",\"p1\":" + String((int)sceneP1) + ",\"ren\":" + String(mode == M_SCENE ? (int)(renderUs / 1000) : 0);
  j += ",\"pw\":[" + String(pwrW[0], 1) + "," + String(pwrW[1], 1) + "," + String(pwrW[2], 1) + "]";
  j += ",\"shon\":" + String(shadeOn ? 1 : 0) + ",\"shk\":[" + String(shadeK[0]) + "," + shadeK[1] + "," + shadeK[2] + "," + shadeK[3] + "," + shadeK[4] + "," + shadeK[5] + "]";
  j += ",\"rst\":" + String((int)esp_reset_reason()) + ",\"up\":" + String(millis() / 1000);
  j += ",\"heap\":" + String((unsigned long)ESP.getFreeHeap()) + ",\"ota\":" + String(otaActive ? 1 : 0) + ",\"build\":\"" __DATE__ " " __TIME__ "\",\"dark\":" + String((int)UI_DARK);
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
}

// ---------------- OTA (over the same AP; laptop joins SUN-DISC and uploads to 192.168.4.1:3232) ----------------
static uint8_t masterBeforeOta = 77;
static void otaSetup() {
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname("sun-gateway");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.setMdnsEnabled(false);                 // we run our own mDNS ("sun.local")
  ArduinoOTA.onStart([] {
    otaActive = true; masterBeforeOta = master;
    master = 0; vTaskDelay(pdMS_TO_TICKS(FRAME_MS * 3));   // one black frame goes out (PixLite holds last frame)
    if (outTask) vTaskSuspend(outTask);                    // no 40 fps sends while flash is being written
    Serial.println("[ota] start");
  });
  ArduinoOTA.onEnd([] { Serial.println("[ota] done → reboot"); });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u — resuming\n", (unsigned)e);
    master = masterBeforeOta; otaActive = false;
    if (outTask) vTaskResume(outTask);
  });
  ArduinoOTA.begin();
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  loadCfg();
  buildTables();
  sc::buildGeom();
  buildShade();
  intent = (float *)malloc(N * 4 * sizeof(float));

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
  otaSetup();

  ws.onEvent(onWs);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {          // stream the page straight from flash — no 20 kB heap copy
    r->send(r->beginResponse_P(200, "text/html", (const uint8_t *)INDEX_HTML, strlen_P(INDEX_HTML)));
  });
  // Pixel map for the page, straight from sun_map.h: N × { int16 x·10, int16 y·10, uint8 board, uint8 run } little-endian
  server.on("/map.bin", HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginChunkedResponse("application/octet-stream", [](uint8_t *buf, size_t maxLen, size_t index) -> size_t {
      size_t i = index / 6; if (i >= N) return 0;
      size_t n = min<size_t>(maxLen / 6, N - i), o = 0;
      for (size_t k = 0; k < n; k++, i++) {
        buf[o++] = SUN_X10[i] & 0xFF; buf[o++] = (SUN_X10[i] >> 8) & 0xFF;
        buf[o++] = SUN_Y10[i] & 0xFF; buf[o++] = (SUN_Y10[i] >> 8) & 0xFF;
        buf[o++] = SUN_BOARD[i]; buf[o++] = SUN_RUN[i];
      }
      return o;
    });
    res->addHeader("Cache-Control", "max-age=86400");
    r->send(res);
  });
  server.on("/scenes.json", HTTP_GET, [](AsyncWebServerRequest *r) {   // scene + palette catalogue, from the C tables
    String j = "{\"scenes\":[";
    for (int i = 0; i < sc::N_SCENES; i++) { if (i) j += ","; j += "{\"n\":\"" + String(sc::SCENES[i].name) + "\",\"g\":\"" + sc::SCENES[i].group + "\",\"p\":\"" + sc::SCENES[i].p1name + "\",\"np\":" + (sc::SCENES[i].noPal ? 1 : 0) + "}"; }
    j += "],\"pal\":[";
    for (int i = 0; i < sc::N_PAL; i++) { if (i) j += ","; j += "\"" + String(sc::PALETTES[i].name) + "\""; }
    j += "]}";
    AsyncWebServerResponse *res = r->beginResponse(200, "application/json", j); res->addHeader("Cache-Control", "max-age=86400"); r->send(res);
  });
  server.onNotFound([](AsyncWebServerRequest *r) { r->redirect("/"); });
  server.begin();

  // Output on core 1 at higher priority than loop(); WiFi/AsyncTCP/OTA live on core 0.
  xTaskCreatePinnedToCore(outputTask, "pixout", 12288, nullptr, 3, &outTask, 1);

  Serial.printf("[sun] free heap after setup: %u bytes · reset reason %d (1 power-on, 3 software, 4 panic, 5/6 WDT, 7 task-WDT, 8 deep-sleep, 9 brown-out)\n", (unsigned)ESP.getFreeHeap(), (int)esp_reset_reason());
  Serial.printf("\n[sun] rig test · build " __DATE__ " " __TIME__ " · %u px · %d runs · %d universes\n", (unsigned)N, SUN_RUNS, nUni);
  Serial.printf("[sun] AP \"%s\" / \"%s\" → http://192.168.4.1  ·  OTA :3232  ·  eth %s → %s (%s, uni %u)\n",
                AP_SSID, AP_PASS, cfg.ethIp.toString().c_str(), cfg.target.toString().c_str(),
                cfg.proto ? "DDP" : "Art-Net", cfg.uni);
}

void loop() {
  static uint32_t tStatus = 0;
  ArduinoOTA.handle();                              // blocks for the duration of an upload; output task is suspended meanwhile
  uint32_t now = millis();
  if (mirrorDirty && ws.count() && ws.availableForWriteAll() && ESP.getFreeHeap() > 40000) {   // live preview ('L'); skip if a client lags or heap is tight
    static uint8_t msg[1 + N * 4]; msg[0] = 'L'; mirrorDirty = false;
    memcpy(msg + 1, mirror, N * 4); ws.binaryAll(msg, sizeof msg);
  }
  if (shadeDirty && now - shadeDirtyMs > 1500) { shadeDirty = false; saveShade(); }
  if (now - tStatus >= 500) { tStatus = now; ws.cleanupClients(); if (ws.availableForWriteAll()) pushStatus(); }
  delay(5);
}
