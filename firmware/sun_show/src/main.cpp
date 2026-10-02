// sun_show — standalone show firmware for the sun. Olimex ESP32-GATEWAY → direct Ethernet → PixLite E4-S Mk3 (Expanded) → 2,417 px.
// No web page. It boots and plays: the scenes in SHOW[] take turns, each for a random SCENE_MIN..MAX minutes, with a FADE_SECONDS
// crossfade, all coloured through one saturated red/orange "sun" palette. Each scene's speed and main parameter wander smoothly
// between the limits in SHOW[] (new target every WANDER_MIN..MAX s, eased over WANDER_EASE_S), so it never looks the same twice.
// Per pixel, before the palette: scene energy × ring gain SHADE[] × core focus (FOCUS_*), with bright features breaking through both
// (BREAK_*), then a soft shoulder (SHOULDER) so nothing clips flat — the palette then turns that into colour, and W is capped at W_MAX.
// TEST_MODE 1 compresses all timing for bench testing.
// No Wi-Fi, no OTA: the radio is off, uploads are over USB only. Everything you might want to touch is in the SETTINGS block.
//
// Flash over USB:  pio run -t upload && pio device monitor

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_random.h>
#include "sun_map.h"
#pragma GCC optimize("O2", "fast-math")
#include "scenes.h"

// ============================================================ SETTINGS ============================================================
#define MASTER_PCT        100      // overall brightness 0–100
#define POWER_CAP         1.00f    // extra multiplier on everything sent. 1.00 is safe for THIS show: red/orange palette + W_MAX + the dark rings keep
                                   // each wedge PSU well under its 500 W even if every pixel hit the palette's top (see README); the old rig test used 0.90
#define W_MAX             140      // hard ceiling on the W channel (0–255) so the white die never blows out — R/G/B are free to go to 255
#define SHOULDER          1.3f     // soft shoulder on scene energy AFTER ring gain / focus / breakthrough and BEFORE the palette:
                                   // e' = 1 − exp(−SHOULDER·e). Energy ×2.00 in the core lands at 0.93 instead of clipping at 1, so a
                                   // hot cluster still shades from red through orange into white instead of one flat block.
                                   // (gain 2 → 0.93 · gain 1 → 0.73 · gain 0.5 → 0.48 · gain 0.06 → 0.08). Bigger = brighter, flatter.
#define SCENE_MIN_MINUTES 5        // each scene plays for a random length between these two…
#define SCENE_MAX_MINUTES 10
#define FADE_SECONDS      8        // …then crossfades to the next

// Test mode: 1 = compressed timing, the same as the Sun Show Preview page (30 s per scene, wander every 5–10 s eased over 3 s), so a
// whole cycle — both scenes, both crossfades, several wander changes — fits in a couple of minutes. Set back to 0 for the event.
#define TEST_MODE         0

// Core focus: scene energy is scaled by a radial envelope BEFORE the palette — full inside FOCUS_CORE_MM, easing down to FOCUS_RIM at the
// rim — so the hot, busy, white-tinged activity lives in the diffused core and the pixelated outer rings sit calmer and deeper red.
// (Ring shading below then sets brightness on top of that.) 1.0 at FOCUS_RIM switches it off.
#define FOCUS_CORE_MM     240
#define FOCUS_RIM         0.30f

// Breakthrough: bright features — rays, filaments, hot spots — punch through the dark rings and the core focus instead of being cut off
// at the edge of the core. A pixel whose own scene energy (before focus and palette, 0–1) is above BREAK_LO starts to be lifted, and at
// BREAK_HI or above it gets the full BREAK_GAIN as if its ring were set to that; everything below BREAK_LO keeps the ring shading as
// normal. It only ever lifts, never dims, so the 2.00 core is untouched.
#define BREAKTHROUGH      1
#define BREAK_LO          0.60f
#define BREAK_HI          0.85f
#define BREAK_GAIN        1.00f    // ×100 → gain a fully-lifted pixel gets on a dark ring (1.00 = as if the ring were at 100)

// Wander: speed and the main parameter glide between the limits below. A new random target is picked every WANDER_MIN..MAX seconds
// and approached with a time constant of WANDER_EASE_S (so a change takes ~3× that to fully arrive — never a visible step).
#if TEST_MODE
#define SCENE_MIN_SEC     30
#define SCENE_MAX_SEC     30
#define WANDER_MIN_S      5
#define WANDER_MAX_S      10
#define WANDER_EASE_S     3.0f
#else
#define SCENE_MIN_SEC     (SCENE_MIN_MINUTES * 60)
#define SCENE_MAX_SEC     (SCENE_MAX_MINUTES * 60)
#define WANDER_MIN_S      20
#define WANDER_MAX_S      60
#define WANDER_EASE_S     15.0f
#endif

// The scenes, by name (see the table at the bottom of scenes.h), with their speed and parameter ranges
struct Show { const char *name; float spMin, spMax, p1Min, p1Max; int scene; };
static Show SHOW[] = {
  { "Solar surface", 0.50f, 1.60f, 0.30f, 0.85f },   // p1 = activity
  { "Neon fluid",    0.40f, 1.40f, 0.35f, 0.70f },   // p1 = zoom (0.5 = neutral)
};
static const int N_SHOW = sizeof(SHOW) / sizeof(SHOW[0]);

// One palette for both: deep red → red → red-orange → orange; R is at 255 from mid-energy up, W joins only at the very top (then W_MAX)
static const sc::Palette SUN_SAT = {"Sun saturated", 7, {
  {0.00f, 0.00f, 0.00f, 0.00f, 0.00f},
  {0.20f, 0.30f, 0.00f, 0.00f, 0.00f},
  {0.42f, 0.60f, 0.01f, 0.00f, 0.00f},
  {0.62f, 0.85f, 0.07f, 0.00f, 0.00f},
  {0.78f, 0.97f, 0.17f, 0.00f, 0.06f},
  {0.90f, 1.00f, 0.32f, 0.01f, 0.20f},
  {1.00f, 1.00f, 0.52f, 0.03f, 0.45f}}};     // brightness keeps climbing to the very top (R only saturates near 0.9) so clusters shade instead of blocking
#define CONTRAST          0.95f    // <1 lifts the mid-tones a little → longer, softer tails on flares; >1 isolates the hot spots
#define WARMTH           -0.25f    // negative = redder (trims G/B/W), positive = whiter

// Dome shading — brightness per pixel ring, ×100 (29 rings at 30 mm pitch, ring 0 = centre, ring 28 = rim). 100 = full, up to 200.
// Your tuned curve from the rig-test page (the one in the video): the whole diffused core (rings 0–16, 0–480 mm, boards A + inner B) at
// 2.00 so it saturates white-hot, a smooth taper through the outer B rings, then the pixelated C rings barely lit with a faint alternation.
static const uint8_t SHADE[29] = {
  200, 200, 200, 200, 200, 200, 200, 200, 200,    // rings 0–8   core (A)
  200, 200, 200, 200, 200, 200, 200, 200,         // rings 9–16  inner B · 270–480 mm
  164, 141, 112,  85,  54,  28,                   // rings 17–22 taper · 510–660 mm
    6,  15,   6,  14,   6,  15 };                 // rings 23–28 C · 690–840 mm, faint alternation
#define SHADE_USE_SAVED   0        // 1 = ignore SHADE[] and use the ring curve saved on this board by gateway_rig_test (NVS sunrig/shr)

#define ETH_IP            "10.10.0.10"
#define PIXLITE_IP        "10.10.0.50"
#define ARTNET_UNI0       1        // Art-Net universe of the plan's universe 1

// ==================================================================================================================================

#define N               SUN_N
#define BRIGHTNESS_CAP  POWER_CAP
#define FRAME_MS        25
#define MIN_YIELD_MS    5
#define STATUS_LED      33
#define RINGS           29
#define ETH_PHY_ADDR_   0
#define ETH_POWER_PIN_  5
#define ETH_MDC_PIN_    23
#define ETH_MDIO_PIN_   18
#define ETH_CLK_MODE_   ETH_CLOCK_GPIO17_OUT

static volatile bool ethLink = false;
static uint32_t pktErr = 0, framesSent = 0;
static uint8_t  shadeRing[RINGS], shadeGain[N];
static uint16_t focus[N];                                                 // radial energy envelope ×256
static uint8_t  brkA[N], brkB[N], brkH[N];
static uint16_t *shoulderLUT = nullptr;                                   // soft shoulder on energy: index = (channel × gain) >> 5, 4096 entries                                // per-pixel breakthrough amount 0–255 (current scene, next scene, helper)
static sc::ib_t *intent = nullptr;
static IPAddress pixlite;
static TaskHandle_t outTask = nullptr, helperTask = nullptr;
WiFiUDP udp;

static inline float frand() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }        // 0..1
static inline float frange(float a, float b) { return a + (b - a) * frand(); }

// ---------------- universe table ----------------
struct Uni { uint16_t first, cnt; uint8_t num; };
static Uni unis[SUN_UNIVERSES]; static int nUni = 0;
static void buildTables() {
  nUni = 0;
  for (int i = 0; i < N; i++) { uint8_t u = sun_universe(i); if (nUni == 0 || unis[nUni - 1].num != u) { unis[nUni] = { (uint16_t)i, 0, u }; nUni++; } unis[nUni - 1].cnt++; }
}
static void buildShade() {
  for (int i = 0; i < N; i++) { int ring = (sc::gR10[i] + 150) / 300; if (ring >= RINGS) ring = RINGS - 1; shadeGain[i] = (uint8_t)((shadeRing[ring] * 64 + 50) / 100); }
}
static void buildShoulder() {
  shoulderLUT = (uint16_t *)malloc(4096 * sizeof(uint16_t));
  for (int i = 0; i < 4096; i++) { float x = (i << 5) / 65535.0f; shoulderLUT[i] = (uint16_t)((1.0f - expf(-SHOULDER * x)) * 65535.0f + 0.5f); }
}
static void buildFocus() {
  const float rim = 8400.0f, core = FOCUS_CORE_MM * 10.0f;
  for (int i = 0; i < N; i++) { float t = (sc::gR10[i] - core) / (rim - core); t = t < 0 ? 0 : t > 1 ? 1 : t; t = t * t * (3 - 2 * t);
    focus[i] = (uint16_t)((1.0f - (1.0f - FOCUS_RIM) * t) * 256.0f + 0.5f); }
}
static void loadShade() {
  memcpy(shadeRing, SHADE, RINGS);
  if (SHADE_USE_SAVED) { Preferences p; if (p.begin("sunrig", true)) { if (p.getBytesLength("shr") == RINGS) { p.getBytes("shr", shadeRing, RINGS); Serial.println("[shade] using the curve saved on this board"); } else Serial.println("[shade] nothing saved on this board — using SHADE[]"); p.end(); } }
  else Serial.println("[shade] using SHADE[] from main.cpp");
  Serial.print("[shade] ring gains ×100:"); for (int r = 0; r < RINGS; r++) Serial.printf(" %u", shadeRing[r]); Serial.println();
}

static void onNetEvent(arduino_event_id_t ev) {
  switch (ev) {
    case ARDUINO_EVENT_ETH_START:        ETH.setHostname("sun"); break;
    case ARDUINO_EVENT_ETH_CONNECTED:    ethLink = true;  Serial.println("[eth] link up"); break;
    case ARDUINO_EVENT_ETH_DISCONNECTED: ethLink = false; Serial.println("[eth] link down"); break;
    case ARDUINO_EVENT_ETH_STOP:         ethLink = false; break;
    default: break;
  }
}

// ---------------- Art-Net ----------------
static uint8_t artSeq = 1;
static bool sendArtDmx(uint16_t universe, const uint8_t *data, uint16_t len) {
  uint8_t p[18 + 512];
  memcpy(p, "Art-Net\0", 8); p[8] = 0x00; p[9] = 0x50; p[10] = 0; p[11] = 14; p[12] = artSeq; p[13] = 0;
  p[14] = universe & 0xFF; p[15] = (universe >> 8) & 0x7F; if (len & 1) len++; p[16] = len >> 8; p[17] = len & 0xFF;
  memcpy(p + 18, data, len);
  for (int attempt = 0; attempt < 2; attempt++) {
    if (!udp.beginPacket(pixlite, 6454)) return false;
    udp.write(p, 18 + len);
    if (udp.endPacket()) return true;
    vTaskDelay(1);
  }
  return false;
}

// ---------------- two-core scene rendering (helper on core 0, below Wi-Fi/OTA in priority) ----------------
struct Job { uint8_t *out; int scene; float t, p1; int i0, i1; };
static Job helperJob;
static volatile bool helperBusy = false; static int sceneSplit = N / 2; static uint8_t hbuf[N * 4];
static const float TINT[3] = {1.0f, 0.45f, 0.08f};
static void renderRange(uint8_t *out, int scene, float t, float p1, int i0, int i1) {
  sc::Ctx c; c.t = t; c.speed = 1.0f; c.p1 = p1; c.tint[0] = TINT[0]; c.tint[1] = TINT[1]; c.tint[2] = TINT[2];   // speed is integrated into t
  sc::SCENES[scene].fn(intent, c, i0, i1);
  const float inv = 1.0f / IB_ONE, invR = 1.0f / (BREAK_HI - BREAK_LO);
  const uint32_t lift = (uint32_t)(BREAK_GAIN * 64 + 0.5f);
  for (int i = i0; i < i1; i++) {
    sc::ib_t *k = intent + 4 * i;
    uint32_t kb = 0;
#if BREAKTHROUGH
    float e = (0.3f * k[0] + 0.5f * k[1] + 0.2f * k[2] + 0.9f * k[3]) * inv;        // the scene's own energy, same weights as sunify
    float u = (e - BREAK_LO) * invR; u = u < 0 ? 0 : u > 1 ? 1 : u; kb = (uint32_t)(u * u * (3 - 2 * u) * 255.0f + 0.5f);
#endif
    uint32_t g = shadeGain[i]; if (lift > g) g += ((lift - g) * kb) >> 8;             // ring gain ×64, lifted where the feature breaks through
    const uint32_t f = focus[i] + (((256 - focus[i]) * kb) >> 8);                       // core focus ×256, bypassed the same way
    const uint32_t m = (g * f) >> 3;                                                    // combined multiplier ×2048
    // channel × m >> 11 would be the scaled energy (0..~2×65535); >> 5 more indexes the 4096-entry shoulder table
    uint32_t v;
    v = (k[0] * m) >> 16; k[0] = shoulderLUT[v > 4095 ? 4095 : v];
    v = (k[1] * m) >> 16; k[1] = shoulderLUT[v > 4095 ? 4095 : v];
    v = (k[2] * m) >> 16; k[2] = shoulderLUT[v > 4095 ? 4095 : v];
    v = (k[3] * m) >> 16; k[3] = shoulderLUT[v > 4095 ? 4095 : v];
  }
  sc::sunifyWith(intent, SUN_SAT, CONTRAST, WARMTH, i0, i1);
  for (int k = i0 * 4; k < i1 * 4; k++) out[k] = (uint8_t)(intent[k] >> 8);
}
static void helperTaskFn(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!helperBusy) continue;
    const Job j = helperJob; const int mid = (j.i0 + j.i1) / 2;
    renderRange(j.out, j.scene, j.t, j.p1, j.i0, mid);
    vTaskDelay(1);
    renderRange(j.out, j.scene, j.t, j.p1, mid, j.i1);
    helperBusy = false; xTaskNotifyGive(outTask);
  }
}
static void renderScene(uint8_t *out, int scene, float t, float p1) {
  if (!helperTask || helperBusy) { renderRange(out, scene, t, p1, 0, N); return; }
  ulTaskNotifyTake(pdTRUE, 0);
  helperJob = { hbuf, scene, t, p1, 0, sceneSplit }; helperBusy = true;
  const int64_t a = esp_timer_get_time(); xTaskNotifyGive(helperTask);
  renderRange(out, scene, t, p1, sceneSplit, N);
  const int64_t b = esp_timer_get_time();
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(300)) == 0) { sceneSplit = N / 4; return; }
  const int64_t c = esp_timer_get_time();
  memcpy(out, hbuf, sceneSplit * 4);
  const int64_t mine = b - a, waited = c - b;
  if (waited > 1500) sceneSplit -= 40; else if (waited < 300 && mine > 2000) sceneSplit += 40;
  if (sceneSplit < N / 8) sceneSplit = N / 8; if (sceneSplit > N * 3 / 5) sceneSplit = N * 3 / 5;
}

// ---------------- wander: each scene's speed & parameter drift smoothly ----------------
struct Wander { float ph, sp, p1, spT, p1T; uint32_t nextPick; };
static Wander W[N_SHOW];
static void wanderInit(int k, uint32_t now) {
  const Show &s = SHOW[k];
  W[k].ph = frange(0, 1000); W[k].sp = W[k].spT = frange(s.spMin, s.spMax); W[k].p1 = W[k].p1T = frange(s.p1Min, s.p1Max);
  W[k].nextPick = now + (uint32_t)frange(WANDER_MIN_S, WANDER_MAX_S) * 1000;
}
static void wanderStep(int k, uint32_t now, float dt) {
  const Show &s = SHOW[k]; Wander &w = W[k];
  if ((int32_t)(now - w.nextPick) >= 0) { w.spT = frange(s.spMin, s.spMax); w.p1T = frange(s.p1Min, s.p1Max); w.nextPick = now + (uint32_t)frange(WANDER_MIN_S, WANDER_MAX_S) * 1000; }
  const float a = dt / WANDER_EASE_S;                       // first-order glide toward the targets
  w.sp += (w.spT - w.sp) * a; w.p1 += (w.p1T - w.p1) * a;
  w.ph += dt * w.sp;                                        // speed is integrated so a speed change never jumps the picture
}
static uint32_t pickSceneMs() { return (uint32_t)(frange(SCENE_MIN_SEC, SCENE_MAX_SEC) * 1000.0f); }

// ---------------- the show (output task, core 1) ----------------
static void outputTask(void *) {
  static uint8_t out[N * 4], out2[N * 4];
  uint32_t lastMs = millis(), frameStart = 0, showStart = millis(), lastLog = 0, sceneMs = pickSceneMs();
  const uint32_t fadeMs = (uint32_t)FADE_SECONDS * 1000UL;
  const uint32_t k = (uint32_t)(MASTER_PCT * 2.55f * BRIGHTNESS_CAP * 256.0f / 255.0f);
  int cur = 0;
  for (int i = 0; i < N_SHOW; i++) wanderInit(i, lastMs);
  esp_task_wdt_add(NULL);
  Serial.printf("[show] scene %d (%s) for %.1f min\n", cur, sc::SCENES[SHOW[cur].scene].name, sceneMs / 60000.0f);
  for (;;) {
    esp_task_wdt_reset();
    { uint32_t spent = millis() - frameStart; uint32_t wait = spent < FRAME_MS ? FRAME_MS - spent : 0; if (wait < MIN_YIELD_MS) wait = MIN_YIELD_MS; vTaskDelay(pdMS_TO_TICKS(wait)); frameStart = millis(); }
    const uint32_t now = millis(); float dt = (now - lastMs) * 0.001f; lastMs = now; if (dt > 0.25f) dt = 0.25f;
    for (int i = 0; i < N_SHOW; i++) wanderStep(i, now, dt);

    const uint32_t el = now - showStart; const int nxt = (cur + 1) % N_SHOW;
    if (el < sceneMs) {
      renderScene(out, SHOW[cur].scene, W[cur].ph, W[cur].p1);
    } else if (el < sceneMs + fadeMs) {                                   // crossfade: render both, blend
      float f = (float)(el - sceneMs) / fadeMs; f = f * f * (3 - 2 * f);
      renderScene(out,  SHOW[cur].scene, W[cur].ph, W[cur].p1);
      renderScene(out2, SHOW[nxt].scene, W[nxt].ph, W[nxt].p1);
      const uint32_t fb = (uint32_t)(f * 256), fa = 256 - fb;
      for (int i = 0; i < N * 4; i++) out[i] = (out[i] * fa + out2[i] * fb) >> 8;
    } else {                                                               // hand over
      wanderInit(cur, now);                                                // the scene that just left gets a fresh start for next time
      cur = nxt; showStart = now; sceneMs = pickSceneMs();
      Serial.printf("[show] scene %d (%s) for %.1f min · up %lu min\n", cur, sc::SCENES[SHOW[cur].scene].name, sceneMs / 60000.0f, now / 60000UL);
      renderScene(out, SHOW[cur].scene, W[cur].ph, W[cur].p1);
    }

    for (int i = 0; i < N; i++) {                                          // master × cap (ring gain, focus and breakthrough already applied before the palette)
      uint8_t *o = out + i * 4;
      uint32_t v0 = (o[0] * k) >> 8, v1 = (o[1] * k) >> 8, v2 = (o[2] * k) >> 8, v3 = (o[3] * k) >> 8;
      o[0] = v0 > 255 ? 255 : v0; o[1] = v1 > 255 ? 255 : v1; o[2] = v2 > 255 ? 255 : v2; o[3] = v3 > W_MAX ? W_MAX : v3;   // W ceiling
    }
    if (ethLink) { bool ok = true; for (int u = 0; u < nUni; u++) ok &= sendArtDmx(ARTNET_UNI0 - 1 + unis[u].num, out + unis[u].first * 4, unis[u].cnt * 4); if (++artSeq == 0) artSeq = 1; if (!ok) pktErr++; }
    framesSent++; digitalWrite(STATUS_LED, (framesSent >> 3) & 1);
    if (now - lastLog >= 60000) { lastLog = now; static uint32_t lastF = 0;
      Serial.printf("[show] up %lu min · %s · speed %.2f · p1 %.2f · %.0f fps · %u send errors · heap %u\n", now / 60000UL, sc::SCENES[SHOW[cur].scene].name, W[cur].sp, W[cur].p1, (framesSent - lastF) / 60.0f, (unsigned)pktErr, (unsigned)ESP.getFreeHeap()); lastF = framesSent; }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  Serial.printf("\n[sun] show · build " __DATE__ " " __TIME__ " · reset reason %d\n", (int)esp_reset_reason());
  WiFi.onEvent(onNetEvent);
  WiFi.mode(WIFI_OFF);                                                     // no radio — USB uploads only
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_POWER_PIN_, ETH_CLK_MODE_);
#else
  ETH.begin(ETH_PHY_ADDR_, ETH_POWER_PIN_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_PHY_LAN8720, ETH_CLK_MODE_);
#endif
  IPAddress ip; ip.fromString(ETH_IP); ETH.config(ip, IPAddress(0, 0, 0, 0), IPAddress(255, 255, 255, 0));
  pixlite.fromString(PIXLITE_IP);
  udp.begin(6454);

  sc::buildGeom(); buildTables(); loadShade(); buildShade(); buildFocus(); buildShoulder();
#if TEST_MODE
  Serial.println("[sun] *** TEST MODE — compressed timing (30 s scenes, fast wander). Set TEST_MODE 0 for the event. ***");
#endif
  for (int k = 0; k < N_SHOW; k++) { SHOW[k].scene = 0; for (int i = 0; i < sc::N_SCENES; i++) if (!strcmp(sc::SCENES[i].name, SHOW[k].name)) SHOW[k].scene = i;
    Serial.printf("[show] slot %d: %s → scene %d%s · speed %.2f–%.2f · p1 %.2f–%.2f\n", k, SHOW[k].name, SHOW[k].scene, strcmp(sc::SCENES[SHOW[k].scene].name, SHOW[k].name) ? "  (NOT FOUND — using Solar surface)" : "", SHOW[k].spMin, SHOW[k].spMax, SHOW[k].p1Min, SHOW[k].p1Max); }
  intent = (sc::ib_t *)malloc(N * 4 * sizeof(sc::ib_t));
  sc::buildLUTFrom(SUN_SAT); sc::buildCLUT(CONTRAST);                      // palette + contrast tables ready before either core renders
  { esp_task_wdt_config_t wd = {}; wd.timeout_ms = 12000; wd.idle_core_mask = 0; wd.trigger_panic = true; if (esp_task_wdt_reconfigure(&wd) != ESP_OK) esp_task_wdt_init(&wd); }

  xTaskCreatePinnedToCore(outputTask, "show", 16384, nullptr, 3, &outTask, 1);          // core 1: the show
  xTaskCreatePinnedToCore(helperTaskFn, "show0", 12288, nullptr, 1, &helperTask, 0);     // core 0: render helper, lowest priority
  Serial.printf("[sun] %u px · %d universes · master %d %% · cap %.2f · W ≤ %d · focus %d mm→%.2f · breakthrough %s %.2f–%.2f→%.2f · %d–%d s per scene · %d s fade · eth %s → %s · free heap %u\n",
                (unsigned)N, nUni, MASTER_PCT, (double)POWER_CAP, W_MAX, FOCUS_CORE_MM, (double)FOCUS_RIM, BREAKTHROUGH ? "on" : "off", (double)BREAK_LO, (double)BREAK_HI, (double)BREAK_GAIN, SCENE_MIN_SEC, SCENE_MAX_SEC, FADE_SECONDS, ETH_IP, PIXLITE_IP, (unsigned)ESP.getFreeHeap());
}
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
