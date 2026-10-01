// scenes.h — on-device port of the sun simulator's pattern engine (simulator/source/template.html) plus the two fluid shaders.
//
// Same model as the website: every pattern writes a per-pixel RGBW "intent" (floats 0..1); optionally the frame is re-coloured
// through a palette ramp ("sunify": energy → RGBW lookup, contrast, warmth). Output is linear — the PixLite does gamma 2.2.
// The pattern bodies are line-for-line ports of the JS (same constants), so what the ESP shows matches the simulator.
// Coordinates come from sun_map.h: X/Y mm (LED side, y-up), R mm, TH degrees (math angle, anticlockwise from +x).
#pragma once
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sun_map.h"
#include "fluid.h"        // FluidParams, shaderPixel() — neon fluid / black-hole sunset (RGBW bytes)

namespace sc {

static const float TAU = 6.2831853f, PI_ = 3.1415927f, RIM = SUN_RIM_R_MM;
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float fract(float v) { return v - floorf(v); }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float smooth(float t) { return t * t * (3 - 2 * t); }
static inline float sqf_(float x) { return x * x; }
static inline float cube(float x) { return x * x * x; }
static inline float p5(float x) { float x2 = x * x; return x2 * x2 * x; }
static inline float p6(float x) { float x3 = x * x * x; return x3 * x3; }
static inline float p14(float x) { float x2 = x * x, x4 = x2 * x2, x8 = x4 * x4; return x8 * x4 * x2; }

// hash1 / hash3 / vnoise — exact-int32 versions of the JS. NOTE the browser multiplies as doubles and loses bits above 2^53, so its
// noise *field* differs from this one (same statistics, different seed). Verified 2026-09-30: with Math.imul on the JS side every
// ported pattern matches this file to < 0.02 per channel; the non-noise patterns match bit-for-bit either way.
static inline float hash1(int32_t n) {
  uint32_t u = (uint32_t)n * 374761393u + 668265263u;
  u = (u ^ (u >> 13)) * 1274126177u;
  return (float)(u ^ (u >> 16)) / 4294967296.0f;
}
static inline float hash3(int32_t x, int32_t y, int32_t z) { return hash1((int32_t)((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u)); }
static float vnoise(float x, float y, float z) {                    // value noise 0..1
  float xf = floorf(x), yf = floorf(y), zf = floorf(z);
  int32_t xi = (int32_t)xf, yi = (int32_t)yf, zi = (int32_t)zf;
  float fx = smooth(x - xf), fy = smooth(y - yf), fz = smooth(z - zf);
  float acc = 0;
  for (int dz = 0; dz < 2; dz++) for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
    float w = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
    acc += w * hash3(xi + dx, yi + dy, zi + dz);
  }
  return acc;
}
static inline float DEPTH(float r) { return 8 + 200 * powf(maxf(0, cosf(PI_ * minf(r, 700) / 1400)), 1.5f); }   // dome stand-off, mm
static inline float FEAT(float r) { return 30 + 1.8f * DEPTH(r); }                                              // smallest legible feature at r, mm

// ---------------- per-pixel geometry — straight from the int16 tables in flash (no RAM copies; heap is precious) ----------------
// The sun_map.h tables live in flash. Flash is read through ONE cache shared by both cores and also holds the Wi-Fi stack's code,
// so two cores streaming 70 kB of tables every frame starved Wi-Fi/web of instruction fetches. Copy the hot tables to RAM once (~29 kB).
static int16_t  *gX10 = nullptr, *gY10 = nullptr;
static uint16_t *gR10 = nullptr, *gTH10 = nullptr;
static uint8_t  *gBoard = nullptr, *gRun = nullptr, *gZone = nullptr;
static uint16_t *FE10 = nullptr;                                     // FEAT(R) × 10
#define X(i)  (gX10[i] * 0.1f)
#define Y(i)  (gY10[i] * 0.1f)
#define R(i)  (gR10[i] * 0.1f)
#define TH(i) (gTH10[i] * 0.1f)
#define FE(i) (FE10[i] * 0.1f)
static void buildGeom() {
  gX10 = (int16_t *)malloc(SUN_N * 2);  memcpy(gX10, SUN_X10, SUN_N * 2);
  gY10 = (int16_t *)malloc(SUN_N * 2);  memcpy(gY10, SUN_Y10, SUN_N * 2);
  gR10 = (uint16_t *)malloc(SUN_N * 2); memcpy(gR10, SUN_R10, SUN_N * 2);
  gTH10 = (uint16_t *)malloc(SUN_N * 2); memcpy(gTH10, SUN_TH10, SUN_N * 2);
  gBoard = (uint8_t *)malloc(SUN_N);    memcpy(gBoard, SUN_BOARD, SUN_N);
  gRun = (uint8_t *)malloc(SUN_N);      memcpy(gRun, SUN_RUN, SUN_N);
  gZone = (uint8_t *)malloc(SUN_N);     for (int i = 0; i < SUN_N; i++) gZone[i] = sun_zone(i);
  FE10 = (uint16_t *)malloc(SUN_N * 2);
  for (int i = 0; i < SUN_N; i++) FE10[i] = (uint16_t)(FEAT(R(i)) * 10 + 0.5f);
}

// The intent buffer is 16-bit fixed point (0..1 → 0..65535) instead of float: half the RAM (19 kB vs 39 kB) for 2,417 px, no visible loss.
typedef uint16_t ib_t;
#define IB_ONE 65535.0f
static inline ib_t q16(float v) { return (ib_t)(clamp01(v) * IB_ONE + 0.5f); }

// ---------------- palettes (verbatim from the simulator) — stops of [pos, r, g, b, w] ----------------
struct Stop { float p, r, g, b, w; };
struct Palette { const char *name; uint8_t n; Stop s[6]; };
static const Palette PALETTES[] = {
  {"Sun",         6, {{0,0,0,0,0},{0.2f,0.55f,0.04f,0,0},{0.4f,1,0.22f,0,0},{0.6f,1,0.45f,0.02f,0.12f},{0.8f,1,0.62f,0.10f,0.45f},{1,1,0.78f,0.30f,1}}},
  {"Ember",       4, {{0,0,0,0,0},{0.35f,0.45f,0.02f,0,0},{0.7f,1,0.18f,0,0},{1,1,0.5f,0.05f,0.25f}}},
  {"Gold",        4, {{0,0,0,0,0},{0.3f,0.6f,0.25f,0,0},{0.7f,1,0.65f,0.1f,0.3f},{1,1,0.85f,0.4f,1}}},
  {"White heat",  5, {{0,0,0,0,0},{0.25f,0.7f,0.1f,0,0},{0.5f,1,0.5f,0.1f,0.3f},{0.75f,1,0.8f,0.5f,0.9f},{1,0.9f,0.9f,1,1}}},
  {"Blue flame",  5, {{0,0,0,0,0},{0.3f,0.05f,0.1f,0.5f,0},{0.6f,0.2f,0.5f,1,0.1f},{0.85f,1,0.6f,0.3f,0.4f},{1,1,0.9f,0.7f,1}}},
  {"Ice",         4, {{0,0,0,0,0},{0.4f,0.05f,0.25f,0.6f,0},{0.8f,0.3f,0.7f,1,0.3f},{1,0.8f,0.95f,1,1}}},
  {"Aurora",      5, {{0,0,0,0,0},{0.3f,0,0.35f,0.2f,0},{0.6f,0.1f,1,0.4f,0},{0.85f,0.6f,0.3f,1,0.1f},{1,1,0.8f,0.9f,0.8f}}},
  {"Mono white",  2, {{0,0,0,0,0},{1,0.3f,0.3f,0.3f,1}}},
  {"Sky day",     5, {{0,0,0,0,0},{0.3f,0.05f,0.12f,0.45f,0},{0.6f,0.2f,0.45f,1,0.4f},{0.85f,0.5f,0.7f,1,0.8f},{1,0.9f,0.95f,1,1}}},
  {"Melanopic",   4, {{0,0,0,0,0},{0.4f,0,0.25f,0.7f,0},{0.75f,0.05f,0.5f,1,0.4f},{1,0.4f,0.8f,1,1}}},
  {"Dawn",        5, {{0,0,0,0,0},{0.25f,0.3f,0.02f,0.1f,0},{0.5f,1,0.3f,0.1f,0.1f},{0.75f,1,0.65f,0.25f,0.5f},{1,0.7f,0.85f,1,1}}},
  {"Golden hour", 4, {{0,0,0,0,0},{0.4f,0.6f,0.2f,0.02f,0.05f},{0.75f,1,0.6f,0.15f,0.5f},{1,1,0.85f,0.5f,1}}},
  {"Dusk",        5, {{0,0,0,0,0},{0.3f,0.1f,0.02f,0.2f,0},{0.6f,0.7f,0.15f,0.3f,0.05f},{0.85f,1,0.45f,0.15f,0.3f},{1,1,0.7f,0.3f,0.6f}}},
  {"Blue hour",   4, {{0,0,0,0,0},{0.5f,0.02f,0.06f,0.4f,0},{0.85f,0.1f,0.2f,0.9f,0.1f},{1,0.4f,0.5f,1,0.5f}}},
  {"Night amber", 3, {{0,0,0,0,0},{0.6f,0.5f,0.08f,0,0},{1,1,0.35f,0.02f,0.1f}}},
  {"Overcast",    3, {{0,0,0,0,0},{0.5f,0.25f,0.28f,0.35f,0.2f},{1,0.6f,0.65f,0.75f,1}}},
};
static const int N_PAL = sizeof(PALETTES) / sizeof(PALETTES[0]);

static float *RLUT = nullptr; static int lutPal = -1;
static void buildLUT(int pal) {
  if (!RLUT) RLUT = (float *)malloc(256 * 4 * sizeof(float));
  const Palette &P = PALETTES[pal];
  for (int k = 0; k < 256; k++) {
    float e = k / 255.0f; int j = 0; while (j < P.n - 2 && e > P.s[j + 1].p) j++;
    const Stop &A = P.s[j], &B = P.s[j + 1 < P.n ? j + 1 : P.n - 1];
    float span = maxf(1e-6f, B.p - A.p), f = clamp01((e - A.p) / span);
    RLUT[4 * k] = A.r + (B.r - A.r) * f; RLUT[4 * k + 1] = A.g + (B.g - A.g) * f; RLUT[4 * k + 2] = A.b + (B.b - A.b) * f; RLUT[4 * k + 3] = A.w + (B.w - A.w) * f;
  }
  lutPal = pal;
}
// energy → palette, in place (the simulator's sunify). The contrast curve pow(e, 0.85·contrast) is a 256-entry table (e is 8-bit anyway).
static uint8_t *CLUT = nullptr; static float clutCon = -1;
static void buildCLUT(float contrast) {
  if (!CLUT) CLUT = (uint8_t *)malloc(256);
  const float ex = 0.85f * contrast;
  for (int k = 0; k < 256; k++) CLUT[k] = (uint8_t)(powf(k / 255.0f, ex) * 255 + 0.5f);
  clutCon = contrast;
}
static void sunify(ib_t *o, int pal, float contrast, float warmth, int i0, int i1) {
  if (pal != lutPal) buildLUT(pal);
  if (contrast != clutCon) buildCLUT(contrast);
  const float cool = maxf(0, warmth), warm = maxf(0, -warmth), inv = 1.0f / IB_ONE;
  const float g1 = 1 - 0.5f * warm, g2 = 1 - 0.8f * warm, g3 = 1 - warm, c1 = cool * 0.15f, c2 = cool * 0.2f, c3 = cool * 0.6f;
  for (int i = i0; i < i1; i++) {
    ib_t *k = o + 4 * i;
    float e = clamp01((0.3f * k[0] + 0.5f * k[1] + 0.2f * k[2] + 0.9f * k[3]) * inv);
    int q = 4 * CLUT[(int)(e * 255 + 0.5f)];
    k[0] = q16(RLUT[q]); k[1] = q16(RLUT[q + 1] * g1 + c1 * e); k[2] = q16(RLUT[q + 2] * g2 + c2 * e); k[3] = q16(RLUT[q + 3] * g3 + c3 * e);
  }
}

// ---------------- pattern context ----------------
struct Ctx { float t, speed, p1; float tint[3]; };
static inline void set(ib_t *o, int i, float r, float g, float b, float w) { ib_t *k = o + 4 * i; k[0] = q16(r); k[1] = q16(g); k[2] = q16(b); k[3] = q16(w); }
static inline void E(ib_t *o, int i, float v) { set(o, i, v, v * 0.6f, v * 0.2f, v * 0.4f); }   // sun-energy shorthand

// ============================ Show ============================
static void p_solar(ib_t *o, const Ctx &c, int i0, int i1) {                        // Solar surface · p1 = Activity (0.55)
  const float t = c.t * c.speed * 0.25f, act = c.p1;
  for (int i = i0; i < i1; i++) {
    float rr = R(i) / RIM, limb = sqrtf(maxf(0, 1 - rr * rr * 0.92f));
    float n1 = vnoise(X(i) / 110 + t, Y(i) / 110 - t * 0.6f, t * 0.7f);
    float n2 = vnoise(X(i) / 38 - t * 1.7f, Y(i) / 38 + t, 7 + t * 1.3f);
    float flare = p6(vnoise(X(i) / 260 + 30, Y(i) / 260 + 30, t * 0.45f)) * 4 * act;
    float v = clamp01(limb * (0.35f + 0.65f * (0.55f * n1 + 0.45f * n2 - 0.15f + act * 0.25f)) + flare);
    float heat = clamp01(v * 1.3f - 0.25f);
    set(o, i, v, v * (0.28f + 0.5f * heat), v * 0.08f * heat, heat * heat * 0.9f);
  }
}
static void p_breath(ib_t *o, const Ctx &c, int i0, int i1) {                       // Breathing rings · p1 = Ring width (0.5)
  const float k = 1 / (60 + 140 * c.p1), t = c.t * c.speed * 2;
  for (int i = i0; i < i1; i++) { float v = 0.5f + 0.5f * sinf(R(i) * k - t); set(o, i, v, v * 0.55f, 0, v * 0.15f); }
}
static void p_rays(ib_t *o, const Ctx &c, int i0, int i1) {                         // Rotating rays · p1 = Ray count (0.4) · uses tint
  const int n = 2 + (int)(c.p1 * 14 + 0.5f); const float w = c.t * c.speed * 0.6f;
  for (int i = i0; i < i1; i++) {
    float a = TH(i) * PI_ / 180;
    float v = cube(0.5f + 0.5f * cosf(n * (a - w))) * (0.35f + 0.65f * (1 - R(i) / RIM)) + 0.04f;
    set(o, i, v * c.tint[0], v * c.tint[1], v * c.tint[2], v * v * 0.3f);
  }
}
static void p_sweep(ib_t *o, const Ctx &c, int i0, int i1) {                        // Radial pulse · p1 = Pulse spacing (0.5) · uses tint
  const float sp = 200 + 500 * c.p1, t = c.t * c.speed * 250;
  for (int i = i0; i < i1; i++) {
    float ph = fract((R(i) - t) / sp), v = p5(1 - ph), m = R(i) / RIM;
    set(o, i, v * (c.tint[0] * (1 - m) + 1.0f * m), v * c.tint[1] * (1 - m), v * c.tint[2] * (1 - m), v * 0.2f * (1 - m));
  }
}
static void p_embers(ib_t *o, const Ctx &c, int i0, int i1) {                       // Embers · p1 = Density (0.35)
  const float t = c.t * c.speed, dens = c.p1;
  for (int i = i0; i < i1; i++) {
    float h = hash1(i * 7 + 1), h2 = hash1(i * 13 + 5);
    float s = p14(maxf(0, sinf(h * 200 + t * (0.4f + 1.2f * h2)))) * (h2 < dens ? 1 : 0);
    float base = 0.03f + 0.03f * vnoise(X(i) / 200, Y(i) / 200, t * 0.2f);
    set(o, i, base + s, base * 0.3f + s * 0.6f, 0, s * 0.5f);
  }
}
// ============================ Dome studies ============================
static void p_corona(ib_t *o, const Ctx &c, int i0, int i1) {                       // Corona breath · p1 = Rim weight (0.6)
  const float t = c.t * c.speed * 0.5f, b = 0.5f + 0.5f * sinf(t);
  for (int i = i0; i < i1; i++) {
    float r = R(i) / RIM, rim = sqf_(clamp01((r - 0.78f) / 0.22f)) * (0.4f + 0.6f * (1 - b)) * (0.4f + 1.2f * c.p1);
    float core = powf(1 - r, 1.6f) * (0.3f + 0.7f * b), wall = expf(-sqf_((r - 0.68f) / 0.12f)) * 0.35f * b;
    set(o, i, rim + core + wall, rim * 0.8f + core * 0.45f + wall * 0.3f, rim * 0.35f, rim * 0.9f);
  }
}
static void p_heart(ib_t *o, const Ctx &c, int i0, int i1) {                        // Heartbeat · p1 = Rate (0.4)
  const float bpm = 30 + 40 * c.p1, ph = fract(c.t * c.speed * bpm / 60);
  const float beat = sqf_(maxf(0, 1 - ph * 3)) + 0.5f * sqf_(maxf(0, 1 - fabsf(ph - 0.25f) * 6));
  for (int i = i0; i < i1; i++) {
    float r = R(i) / RIM, core = (0.18f + 0.3f * beat) * powf(1 - r, 0.7f), tick = r > 0.94f ? beat : 0;
    set(o, i, core * 0.9f + tick, core * 0.5f + tick, core * 0.15f + tick, core + tick);
  }
}
static void p_aurora(ib_t *o, const Ctx &c, int i0, int i1) {                       // Aurora curtains · p1 = Turbulence (0.5)
  const float t = c.t * c.speed * 0.35f;
  float cx[3]; for (int k = 0; k < 3; k++) cx[k] = sinf(t * (0.4f + 0.15f * k) + k * 2.1f) * 600;
  for (int i = i0; i < i1; i++) {
    float wob = (vnoise(X(i) / 500 + t * 0.3f, Y(i) / 300, t) - 0.5f) * 400 * c.p1, v = 0;
    for (int k = 0; k < 3; k++) { float w = 60 + 60 * k; v += expf(-sqf_((X(i) - (cx[k] + wob)) / w)) * (0.4f + 0.3f * vnoise(Y(i) / 120 + t * 2, k * 3, t)); }
    v = clamp01(v) * (0.55f + 0.45f * (Y(i) / RIM)); set(o, i, v * 0.1f, v * 0.9f, v * 0.5f, v * 0.12f);
  }
}
// ============================ Natural sun ============================
static void p_photosphere(ib_t *o, const Ctx &c, int i0, int i1) {                  // Photosphere · p1 = Granule size (40..400 mm, default 140 ≈ 0.28); con 0.9, limb 0.7, drift 0.5, churn 0.6
  const float t = c.t * c.speed, g = 40 + 360 * c.p1, con = 0.9f, lim = 0.7f, drift = 0.5f, churn = 0.6f;
  for (int i = i0; i < i1; i++) {
    float r = R(i) / RIM, limb = 1 - lim * (1 - sqrtf(maxf(0, 1 - r * r)));
    float n1 = vnoise(X(i) / g + t * 0.35f * drift, Y(i) / g, t * 0.6f * churn), n2 = vnoise(X(i) / (g * 0.45f) - t * 0.5f * drift, Y(i) / (g * 0.45f) + 7, t * 1.2f * churn + 3), n3 = vnoise(X(i) / (g * 2.6f), Y(i) / (g * 2.6f) - t * 0.15f, t * 0.25f + 9);
    E(o, i, clamp01(limb * (0.62f + con * (0.55f * n1 + 0.3f * n2 + 0.15f * n3 - 0.5f))));
  }
}
// ============================ Rim ↔ dome ============================
static void p_spiral(ib_t *o, const Ctx &c, int i0, int i1) {                       // Spiral arms · p1 = Arms (0.4)
  const float t = c.t * c.speed * 0.4f; const int arms = 2 + (int)(c.p1 * 3 + 0.5f);
  for (int i = i0; i < i1; i++) {
    float r = maxf(30, R(i)), a = TH(i) * PI_ / 180, ph = fract((a - 2.2f * logf(r / 60) + t) * arms / TAU);
    float w = 0.06f + 0.25f * FE(i) / 400, d = minf(ph, 1 - ph), v = expf(-d * d / (2 * w * w)) * 0.9f + 0.08f * (1 - R(i) / RIM);
    set(o, i, v, v * 0.6f, v * 0.2f, v * 0.35f);
  }
}
// ============================ Shaders (fluid.h) — p1 = zoom (-3..3), tint unused ============================
static void shader(ib_t *o, const Ctx &c, int i0, int i1, uint8_t which) {
  FluidParams P; P.shader = which; P.radius = RIM; P.speed = 1; P.zoom = c.p1 * 6 - 3; P.iter = which ? 5 : 6;
  if (which) { P.glow = 1; P.bright = 1.4f; P.sat = 1.2f; }
  uint8_t px[4];
  for (int i = i0; i < i1; i++) { shaderPixel(X(i), Y(i), c.t * c.speed, P, px); set(o, i, px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f, px[3] / 255.0f); }
}
static void p_fluid(ib_t *o, const Ctx &c, int i0, int i1) { shader(o, c, i0, i1, 0); }
static void p_blackhole(ib_t *o, const Ctx &c, int i0, int i1) { shader(o, c, i0, i1, 1); }

// ---------------- scene table ----------------
typedef void (*Fn)(ib_t *, const Ctx &, int, int);
struct Scene { const char *name, *group, *p1name; float p1d; bool noPal; Fn fn; };
static const Scene SCENES[] = {
  {"Solar surface",   "Show",        "Activity",      0.55f, false, p_solar},
  {"Breathing rings", "Show",        "Ring width",    0.5f,  false, p_breath},
  {"Rotating rays",   "Show",        "Ray count",     0.4f,  false, p_rays},
  {"Radial pulse",    "Show",        "Pulse spacing", 0.5f,  false, p_sweep},
  {"Embers",          "Show",        "Density",       0.35f, false, p_embers},
  {"Corona breath",   "Dome",        "Rim weight",    0.6f,  false, p_corona},
  {"Heartbeat",       "Dome",        "Rate",          0.4f,  false, p_heart},
  {"Aurora curtains", "Dome",        "Turbulence",    0.5f,  false, p_aurora},
  {"Photosphere",     "Natural sun", "Granule size",  0.28f, false, p_photosphere},
  {"Spiral arms",     "Rim ↔ dome",  "Arms",          0.4f,  false, p_spiral},
  {"Neon fluid",      "Shader",      "Zoom",          0.5f,  true,  p_fluid},
  {"Black-hole sunset","Shader",     "Zoom",          0.5f,  true,  p_blackhole},
};
static const int N_SCENES = sizeof(SCENES) / sizeof(SCENES[0]);

// Render pixels [i0,i1) of one frame: intent floats → RGBW bytes (linear). pal: -1 = pattern's own colours, else palette index.
// Called from two tasks (one per core) on disjoint ranges; per-frame scalars inside the patterns are recomputed by both — cheap and deterministic.
static void render(uint8_t *out, ib_t *intent, int scene, float t, float speed, float p1, const float *tint, int pal, float contrast, float warmth, int i0, int i1) {
  if (scene < 0 || scene >= N_SCENES) return;
  Ctx c; c.t = t; c.speed = speed; c.p1 = p1; c.tint[0] = tint[0]; c.tint[1] = tint[1]; c.tint[2] = tint[2];
  SCENES[scene].fn(intent, c, i0, i1);
  if (pal >= 0 && pal < N_PAL) sunify(intent, pal, contrast, warmth, i0, i1);
  else if (contrast != 1) {                                          // pattern's own colours + contrast: per-channel pow via a 256-entry table
    static uint8_t *CLUT2 = nullptr; static float clut2Con = -1;
    if (!CLUT2) CLUT2 = (uint8_t *)malloc(256);
    if (contrast != clut2Con) { for (int k = 0; k < 256; k++) CLUT2[k] = (uint8_t)(powf(k / 255.0f, contrast) * 255 + 0.5f); clut2Con = contrast; }
    for (int k = i0 * 4; k < i1 * 4; k++) out[k] = CLUT2[intent[k] >> 8];
    return;
  }
  for (int k = i0 * 4; k < i1 * 4; k++) out[k] = (uint8_t)(intent[k] >> 8);   // 65535 → 255 (NOT (v+128)>>8, which wraps 65535 to 0)
}

} // namespace sc
