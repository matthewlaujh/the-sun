// sun_show — standalone show firmware for the sun. Olimex ESP32-GATEWAY → direct Ethernet → PixLite E4-S Mk3 (Expanded) → 2,417 px.
// No web page. It boots and plays: the scenes in SHOW[] take turns, each for a random SCENE_MIN..MAX minutes, with a FADE_SECONDS
// crossfade, all coloured through one saturated red/orange "sun" palette. Each scene's speed and main parameter wander smoothly
// between the limits in SHOW[] (new target every WANDER_MIN..MAX s, eased over WANDER_EASE_S), so it never looks the same twice.
// Dome shading = the fixed per-ring brightness curve SHADE[] (centre full → taper → hot/dark alternating rings). W is capped at W_MAX.
// OTA stays: the board hosts Wi-Fi "SUN-DISC" purely for `pio run -e ota -t upload` (ArduinoOTA, 192.168.4.1:3232).
// Everything you might want to touch is in the SETTINGS block.
//
// Flash over USB:  pio run -e usb -t upload && pio device monitor       OTA: pio run -e ota -t upload (laptop joined to SUN-DISC)

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <ETH.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
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
#define SCENE_MIN_MINUTES 5        // each scene plays for a random length between these two…
#define SCENE_MAX_MINUTES 10
#define FADE_SECONDS      8        // …then crossfades to the next

// Wander: speed and the main parameter glide between the limits below. A new random target is picked every WANDER_MIN..MAX seconds
// and approached with a time constant of WANDER_EASE_S (so a change takes ~3× that to fully arrive — never a visible step).
#define WANDER_MIN_S      20
#define WANDER_MAX_S      60
#define WANDER_EASE_S     15.0f

// The scenes, by name (see the table at the bottom of scenes.h), with their speed and parameter ranges
struct Show { const char *name; float spMin, spMax, p1Min, p1Max; int scene; };
static Show SHOW[] = {
  { "Solar surface", 0.50f, 1.60f, 0.30f, 0.85f },   // p1 = activity
  { "Neon fluid",    0.40f, 1.40f, 0.35f, 0.70f },   // p1 = zoom (0.5 = neutral)
};
static const int N_SHOW = sizeof(SHOW) / sizeof(SHOW[0]);

// One palette for both: deep red → red → red-orange → orange; R is at 255 from mid-energy up, W joins only at the very top (then W_MAX)
static const sc::Palette SUN_SAT = {"Sun saturated", 6, {
  {0.00f, 0.00f, 0.00f, 0.00f, 0.00f},
  {0.25f, 0.55f, 0.00f, 0.00f, 0.00f},
  {0.50f, 1.00f, 0.08f, 0.00f, 0.00f},
  {0.70f, 1.00f, 0.22f, 0.00f, 0.00f},
  {0.88f, 1.00f, 0.40f, 0.00f, 0.22f},
  {1.00f, 1.00f, 0.55f, 0.03f, 0.50f}}};     // W climbs only on the hottest energy and is capped by W_MAX on the way out
#define CONTRAST          1.10f    // >1 darkens the mid-tones, keeps the hot spots hot
#define WARMTH           -0.25f    // negative = redder (trims G/B/W), positive = whiter

// Dome shading — brightness per pixel ring, ×100 (29 rings at 30 mm pitch, ring 0 = centre, ring 28 = rim). 100 = full, up to 200.
// Centre full, the first five rings taper to very low, then hot / dark / warm / dark all the way out — literal hot spots and dark spots.
static const uint8_t SHADE[29] = {
  100,                                            // ring 0  centre
   70,  45,  25,  12,   5,                        // rings 1–5  taper
   80,   6,  55,   4,  80,   6,  55,   4,         // rings 6–13 hot · dark · warm · dark
   80,   6,  55,   4,  80,   6,  55,   4,         // rings 14–21
   80,   6,  55,   4,  80,   6,  55 };            // rings 22–28 (rim)
#define SHADE_USE_SAVED   0        // 1 = ignore SHADE[] and use the ring curve saved on this board by gateway_rig_test (NVS sunrig/shr)

#define ETH_IP            "10.10.0.10"
#define PIXLITE_IP        "10.10.0.50"
#define ARTNET_UNI0       1        // Art-Net universe of the plan's universe 1

static const char *AP_SSID  = "SUN-DISC";          // OTA only — nothing is served on it
static const char *AP_PASS  = "sunshine26";
static const uint8_t AP_CHANNEL = 6;
static const char *OTA_PASS = "sunshine26-ota";    // must match upload_flags --auth in platformio.ini
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

static volatile bool ethLink = false, otaActive = false;
static uint32_t pktErr = 0, framesSent = 0;
static uint8_t  shadeRing[RINGS], shadeGain[N];
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
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:    Serial.println("[wifi] laptop joined SUN-DISC"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: Serial.println("[wifi] laptop left SUN-DISC"); break;
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
static uint32_t pickSceneMs() { return (uint32_t)(frange(SCENE_MIN_MINUTES, SCENE_MAX_MINUTES) * 60000.0f); }

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

    for (int i = 0; i < N; i++) {                                          // ring gain × master × cap
      uint8_t *o = out + i * 4; const uint32_t kk = (k * shadeGain[i]) >> 6;
      uint32_t v0 = (o[0] * kk) >> 8, v1 = (o[1] * kk) >> 8, v2 = (o[2] * kk) >> 8, v3 = (o[3] * kk) >> 8;
      o[0] = v0 > 255 ? 255 : v0; o[1] = v1 > 255 ? 255 : v1; o[2] = v2 > 255 ? 255 : v2; o[3] = v3 > W_MAX ? W_MAX : v3;   // W ceiling
    }
    if (ethLink) { bool ok = true; for (int u = 0; u < nUni; u++) ok &= sendArtDmx(ARTNET_UNI0 - 1 + unis[u].num, out + unis[u].first * 4, unis[u].cnt * 4); if (++artSeq == 0) artSeq = 1; if (!ok) pktErr++; }
    framesSent++; digitalWrite(STATUS_LED, (framesSent >> 3) & 1);
    if (now - lastLog >= 60000) { lastLog = now; static uint32_t lastF = 0;
      Serial.printf("[show] up %lu min · %s · speed %.2f · p1 %.2f · %.0f fps · %u send errors · heap %u\n", now / 60000UL, sc::SCENES[SHOW[cur].scene].name, W[cur].sp, W[cur].p1, (framesSent - lastF) / 60.0f, (unsigned)pktErr, (unsigned)ESP.getFreeHeap()); lastF = framesSent; }
  }
}

// ---------------- OTA + housekeeping (core 0) ----------------
static void otaSetup() {
  ArduinoOTA.setPort(3232); ArduinoOTA.setHostname("sun"); ArduinoOTA.setPassword(OTA_PASS); ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([] { otaActive = true; Serial.println("[ota] start — output paused");
    if (outTask) { esp_task_wdt_delete(outTask); vTaskSuspend(outTask); } esp_task_wdt_delete(NULL); });   // no watchdog during the upload
  ArduinoOTA.onEnd([] { Serial.println("[ota] done → reboot"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[ota] error %u — resuming the show\n", (unsigned)e); otaActive = false;
    esp_task_wdt_add(NULL); if (outTask) { vTaskResume(outTask); esp_task_wdt_add(outTask); } });
  ArduinoOTA.begin();
}
static void houseTask(void *) {
  uint32_t apDeadSince = 0;
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    ArduinoOTA.handle();                                                   // blocks for the duration of an upload (watchdog unsubscribed in onStart)
    const uint32_t now = millis();
    const bool apUp = (WiFi.getMode() & WIFI_MODE_AP) && WiFi.softAPIP() == IPAddress(192, 168, 4, 1);   // the AP is the only way in once mounted
    if (!apUp) { if (!apDeadSince) apDeadSince = now; else if (now - apDeadSince > 15000) { Serial.println("[wifi] access point gone for 15 s → reboot"); delay(50); ESP.restart(); } }
    else apDeadSince = 0;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED, OUTPUT);
  Serial.printf("\n[sun] show · build " __DATE__ " " __TIME__ " · reset reason %d\n", (int)esp_reset_reason());
  WiFi.onEvent(onNetEvent);
  WiFi.mode(WIFI_AP);                                                      // OTA only
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0, 2);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.setSleep(false);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ETH.begin(ETH_PHY_LAN8720, ETH_PHY_ADDR_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_POWER_PIN_, ETH_CLK_MODE_);
#else
  ETH.begin(ETH_PHY_ADDR_, ETH_POWER_PIN_, ETH_MDC_PIN_, ETH_MDIO_PIN_, ETH_PHY_LAN8720, ETH_CLK_MODE_);
#endif
  IPAddress ip; ip.fromString(ETH_IP); ETH.config(ip, IPAddress(0, 0, 0, 0), IPAddress(255, 255, 255, 0));
  pixlite.fromString(PIXLITE_IP);
  udp.begin(6454);
  otaSetup();

  sc::buildGeom(); buildTables(); loadShade(); buildShade();
  for (int k = 0; k < N_SHOW; k++) { SHOW[k].scene = 0; for (int i = 0; i < sc::N_SCENES; i++) if (!strcmp(sc::SCENES[i].name, SHOW[k].name)) SHOW[k].scene = i;
    Serial.printf("[show] slot %d: %s → scene %d%s · speed %.2f–%.2f · p1 %.2f–%.2f\n", k, SHOW[k].name, SHOW[k].scene, strcmp(sc::SCENES[SHOW[k].scene].name, SHOW[k].name) ? "  (NOT FOUND — using Solar surface)" : "", SHOW[k].spMin, SHOW[k].spMax, SHOW[k].p1Min, SHOW[k].p1Max); }
  intent = (sc::ib_t *)malloc(N * 4 * sizeof(sc::ib_t));
  sc::buildLUTFrom(SUN_SAT); sc::buildCLUT(CONTRAST);                      // palette + contrast tables ready before either core renders
  { esp_task_wdt_config_t wd = {}; wd.timeout_ms = 12000; wd.idle_core_mask = 0; wd.trigger_panic = true; if (esp_task_wdt_reconfigure(&wd) != ESP_OK) esp_task_wdt_init(&wd); }

  xTaskCreatePinnedToCore(outputTask, "show", 16384, nullptr, 3, &outTask, 1);          // core 1: the show
  xTaskCreatePinnedToCore(houseTask, "house", 6144, nullptr, 2, nullptr, 0);             // core 0: OTA, AP watchdog
  xTaskCreatePinnedToCore(helperTaskFn, "show0", 12288, nullptr, 1, &helperTask, 0);     // core 0: render helper, lowest priority
  Serial.printf("[sun] %u px · %d universes · master %d %% · cap %.2f · W ≤ %d · %d–%d min per scene · %d s fade · eth %s → %s · AP \"%s\"/\"%s\" OTA :3232 · free heap %u\n",
                (unsigned)N, nUni, MASTER_PCT, (double)POWER_CAP, W_MAX, SCENE_MIN_MINUTES, SCENE_MAX_MINUTES, FADE_SECONDS, ETH_IP, PIXLITE_IP, AP_SSID, AP_PASS, (unsigned)ESP.getFreeHeap());
}
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
