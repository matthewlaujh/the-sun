// fluid.h — per-pixel port of the "neon fluid" GLSL fragment shader for the sun's LED map.
//
// The shader is a pure function of (position, time, params), so we evaluate it once per LED instead of per
// screen pixel. Mouse / scroll inputs become web-page sliders (FluidParams). Output is linear RGBW —
// the PixLite applies gamma 2.2 + dithering, so nothing is gamma'd here.
//
// Cost per pixel ≈ ITER × (2 sin + 2 cos) + 4 exp + tanh. With the fast trig below the full 2,417-px sun at
// 8 iterations is ~0.3 M trig/frame → well under 10 ms at 240 MHz; the 173-px disc is negligible.
#pragma once
#include <math.h>
#include <stdint.h>

struct FluidParams {
  float speed  = 0.6f;   // time multiplier
  float zoom   = 0.0f;   // added to z (shader: mouse.y * 2)          -3 … 3
  float flow   = 0.0f;   // phase offset (shader: mouse.x * 3)         0 … 6.3
  float fx     = 0.0f;   // "mouse" focus point, -1 … 1 in disc units
  float fy     = 0.0f;
  float glow   = 0.8f;   // neon boost near the focus point            0 … 2
  float bright = 1.6f;   // output gain                                0.2 … 3
  float sat    = 1.5f;   // saturation (shader used 1.5)               0 … 2
  float wmix   = 0.5f;   // how much of the common RGB floor moves to the W die   0 … 1
  float radius = 226.0f; // mm that maps to |p| = 1 (disc 226, full sun 886.5)
  uint8_t iter = 8;      // 1 … 8 (fluid: loop iterations · black hole: fbm octaves)
  uint8_t shader = 0;    // 0 = neon fluid · 1 = black-hole sunset
};

// ---- fast trig: range-reduce + parabola with correction (|err| < 0.001) ----
static inline float fsin(float x) {
  x *= 0.15915494f;                      // /2π
  x -= floorf(x + 0.5f);                 // -0.5 … 0.5 (one period, centred)
  x *= 6.2831853f;                       // -π … π
  const float ax = x < 0 ? -x : x;
  float y = 1.2732395f * x - 0.4052847f * x * ax;   // (4/π)x − (4/π²)x|x|
  const float ay = y < 0 ? -y : y;
  return 0.225f * (y * ay - y) + y;      // correction term
}
static inline float fcos(float x) { return fsin(x + 1.5707963f); }
static inline float ftanh(float x) {           // the shader's own approximation
  float x2 = x * x;
  float y = x * (27.0f + x2) / (27.0f + 9.0f * x2);
  return y < -1 ? -1 : (y > 1 ? 1 : y);
}
static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Evaluate one pixel at (x_mm, y_mm), time t seconds. Writes linear RGBW 0..255 to out[4].
static inline void fluidPixel(float x_mm, float y_mm, float t, const FluidParams &P, uint8_t *out) {
  const float px = x_mm / P.radius, py = y_mm / P.radius;         // p: centred, |p| ≤ 1 on the disc
  const float dotp = px * px + py * py;
  float zb = 0.7f - dotp; if (zb < 0) zb = -zb;
  const float z = 4.0f - 4.0f * zb + P.zoom;                       // z = zBase + mouse.y*2
  float fx = px * z, fy = py * z;                                  // f = p * z
  float Or = 0, Og = 0, Ob = 0;                                    // O accumulates (r,g,b) = (sinF.x, sinF.y, sinF.x)

  // neon boost near the focus point (shader: mouseDistance / neonBoost)
  const float dx = px - P.fx, dy = py - P.fy;
  float md = sqrtf(dx * dx + dy * dy) * 2.0f;
  float s = md / 0.5f; s = clamp01(s); s = s * s * (3 - 2 * s);   // smoothstep(0, .5, md)
  const float boost = 1.0f + (1.0f - s) * P.glow;

  const float tt = t + P.flow;                                     // timeOffset = u_time + mouse.x*3
  for (int j = 1; j <= P.iter; j++) {
    const float jf = (float)j;
    const float sx = fsin(fx) + 1.0f, sy = fsin(fy) + 1.0f;
    float d = fx - fy; if (d < 0) d = -d;
    const float k = d * 1.5f * boost;
    Or += sx * k; Og += sy * k; Ob += sx * k;
    // f += cos(f.yx * i.y + i + timeOffset) / i.y + 0.7   with i = (0, j)
    const float nx = fcos(fy * jf + tt) / jf + 0.7f;
    const float ny = fcos(fx * jf + jf + tt) / jf + 0.7f;
    fx += nx; fy += ny;
  }
  // tonemap: tanh(7 * exp(z - 4 - p.y * (-1, 1, 2)) / O)
  const float e = z - 4.0f;
  float r = ftanh(7.0f * expf(e + py) / (Or + 1e-3f));
  float g = ftanh(7.0f * expf(e - py) / (Og + 1e-3f));
  float b = ftanh(7.0f * expf(e - 2.0f * py) / (Ob + 1e-3f));
  r = clamp01(r); g = clamp01(g); b = clamp01(b);

  // saturation (mix toward luminance with factor > 1 pushes away from grey)
  const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
  r = clamp01(lum + (r - lum) * P.sat);
  g = clamp01(lum + (g - lum) * P.sat);
  b = clamp01(lum + (b - lum) * P.sat);

  // gain, then RGB→RGBW: move part of the common floor onto the white die
  r *= P.bright; g *= P.bright; b *= P.bright;
  float w = (r < g ? (r < b ? r : b) : (g < b ? g : b)) * P.wmix;
  r -= w; g -= w; b -= w;
  out[0] = (uint8_t)(clamp01(r) * 255.0f + 0.5f);
  out[1] = (uint8_t)(clamp01(g) * 255.0f + 0.5f);
  out[2] = (uint8_t)(clamp01(b) * 255.0f + 0.5f);
  out[3] = (uint8_t)(clamp01(w) * 255.0f + 0.5f);
}

// =====================================================================================================
// Shader 1 — "black-hole sunset": simplex-fbm sky pulled into a spiral around a dark disc, stars outside.
// Straight port of the GLSL (snoise by Ian McEwan / Ashima Arts). Parameter meanings for this shader:
//   zoom → overall scale (positive = hole bigger)   flow → spiral rotation phase   glow → pull strength (×)
//   fx/fy → "mouse" that deepens the pull nearby     iter → fbm octaves (1…8)
// =====================================================================================================
static inline float g_mod289(float x) { return x - floorf(x * (1.0f / 289.0f)) * 289.0f; }
static inline float g_permute(float x) { return g_mod289(((x * 34.0f) + 1.0f) * x); }
static inline float g_fract(float x) { return x - floorf(x); }
static inline float smoothstepf(float e0, float e1, float x) { float t = clamp01((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }

static float snoise(float vx, float vy) {
  const float Cx = 0.211324865405187f, Cy = 0.366025403784439f, Cz = -0.577350269189626f, Cw = 0.024390243902439f;
  float s = (vx + vy) * Cy;
  float ix = floorf(vx + s), iy = floorf(vy + s);
  float t = (ix + iy) * Cx;
  float x0x = vx - ix + t, x0y = vy - iy + t;
  float i1x = x0x > x0y ? 1.0f : 0.0f, i1y = 1.0f - i1x;
  float x1x = x0x + Cx - i1x, x1y = x0y + Cx - i1y;
  float x2x = x0x + Cz,       x2y = x0y + Cz;
  ix = g_mod289(ix); iy = g_mod289(iy);
  float p0 = g_permute(g_permute(iy)       + ix);
  float p1 = g_permute(g_permute(iy + i1y) + ix + i1x);
  float p2 = g_permute(g_permute(iy + 1)   + ix + 1);
  float m0 = 0.5f - (x0x * x0x + x0y * x0y); if (m0 < 0) m0 = 0;
  float m1 = 0.5f - (x1x * x1x + x1y * x1y); if (m1 < 0) m1 = 0;
  float m2 = 0.5f - (x2x * x2x + x2y * x2y); if (m2 < 0) m2 = 0;
  m0 *= m0; m0 *= m0; m1 *= m1; m1 *= m1; m2 *= m2; m2 *= m2;
  float xa = 2.0f * g_fract(p0 * Cw) - 1.0f, xb = 2.0f * g_fract(p1 * Cw) - 1.0f, xc = 2.0f * g_fract(p2 * Cw) - 1.0f;
  float ha = fabsf(xa) - 0.5f, hb = fabsf(xb) - 0.5f, hc = fabsf(xc) - 0.5f;
  float a0a = xa - floorf(xa + 0.5f), a0b = xb - floorf(xb + 0.5f), a0c = xc - floorf(xc + 0.5f);
  m0 *= 1.79284291400159f - 0.85373472095314f * (a0a * a0a + ha * ha);
  m1 *= 1.79284291400159f - 0.85373472095314f * (a0b * a0b + hb * hb);
  m2 *= 1.79284291400159f - 0.85373472095314f * (a0c * a0c + hc * hc);
  float gx = a0a * x0x + ha * x0y, gy = a0b * x1x + hb * x1y, gz = a0c * x2x + hc * x2y;
  return 130.0f * (m0 * gx + m1 * gy + m2 * gz);
}
static inline float fbm(float x, float y, int oct) {
  float v = 0, a = 0.5f, f = 1.0f;
  for (int i = 0; i < oct; i++) { v += a * snoise(x * f, y * f); a *= 0.5f; f *= 2.0f; }
  return v;
}

static inline void blackholePixel(float x_mm, float y_mm, float t, const FluidParams &P, uint8_t *out) {
  const float sc = 0.5f * expf(-P.zoom * 0.35f);              // shader uv spans ±0.5 across the disc
  const float ux = x_mm / P.radius * sc, uy = y_mm / P.radius * sc;
  const float mx = P.fx * sc, my = P.fy * sc;
  const float dC = sqrtf(ux * ux + uy * uy);
  const float dM = sqrtf((ux - mx) * (ux - mx) + (uy - my) * (uy - my));
  const float bhr = 0.1f + 0.03f * fsin(t * 0.5f);
  const float ehr = 0.25f + 0.05f * fsin(t * 0.3f);
  const float mi = smoothstepf(0.5f, 0.0f, dM) * 0.5f;
  const float strength = 2.0f * P.glow + mi * 3.0f;
  // spiral
  float ang = atan2f(uy, ux) + t * 0.2f + P.flow + strength / (dC + 0.001f);
  const float dx = fcos(ang) * dC, dy = fsin(ang) * dC;
  const int oct = P.iter < 1 ? 1 : P.iter;
  const float n = fbm(dx * 3.0f + t * 0.1f, dy * 3.0f + t * 0.1f, oct);
  const float vg = smoothstepf(-0.5f, 0.5f, dy);
  float r = 0.9f + (0.2f - 0.9f) * vg, g = 0.6f + (0.4f - 0.6f) * vg, b = 0.3f + (0.8f - 0.3f) * vg;
  const float nk = smoothstepf(0.5f, 1.0f, vg);
  r += (0.05f - r) * nk; g += (0.05f - g) * nk; b += (0.2f - b) * nk;
  r += n * 0.1f; g += n * 0.1f; b += n * 0.1f;
  // stars
  if (dC > bhr * 1.5f) {
    const float sn = fbm(dx * 20.0f, dy * 20.0f, 3);
    const float st = smoothstepf(0.75f, 0.8f, sn) * smoothstepf(bhr * 2.0f, ehr * 3.0f, dC);
    r += st * 0.9f; g += st * 0.95f; b += st;
  }
  // hole + horizon glow + vignette
  const float hole = smoothstepf(0.01f, -0.01f, dC - bhr);
  r *= 1 - hole; g *= 1 - hole; b *= 1 - hole;
  const float gl = smoothstepf(ehr, bhr, dC) * 0.8f;
  r += gl * 0.1f; g += gl * 0.2f; b += gl * 0.4f;
  const float vig = smoothstepf(1.0f, 0.3f, dC * 1.5f);
  r *= vig; g *= vig; b *= vig;
  r = clamp01(r); g = clamp01(g); b = clamp01(b);
  const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
  r = clamp01((lum + (r - lum) * P.sat) * P.bright); g = clamp01((lum + (g - lum) * P.sat) * P.bright); b = clamp01((lum + (b - lum) * P.sat) * P.bright);
  float w = (r < g ? (r < b ? r : b) : (g < b ? g : b)) * P.wmix;
  r -= w; g -= w; b -= w;
  out[0] = (uint8_t)(clamp01(r) * 255.0f + 0.5f); out[1] = (uint8_t)(clamp01(g) * 255.0f + 0.5f);
  out[2] = (uint8_t)(clamp01(b) * 255.0f + 0.5f); out[3] = (uint8_t)(clamp01(w) * 255.0f + 0.5f);
}

static inline void shaderPixel(float x_mm, float y_mm, float t, const FluidParams &P, uint8_t *out) {
  if (P.shader == 1) blackholePixel(x_mm, y_mm, t, P, out); else fluidPixel(x_mm, y_mm, t, P, out);
}
