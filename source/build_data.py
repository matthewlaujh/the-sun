"""Compact sun-map.json → JS data block + C header for the ESP32 port."""
import json, math, os
K = os.path.dirname(__file__) + '/sun-visualizer-kit'
d = json.load(open(K + '/sun-map.json'))
px = d['pixels']; N = len(px)
BOARDS = ['A'] + [f'B{k}' for k in range(1, 13)] + [f'C{k}' for k in range(1, 13)]
bidx = {b: i for i, b in enumerate(BOARDS)}
X = [round(p['x'], 2) for p in px]
Y = [round(p['y'], 2) for p in px]
B = [bidx[p['board']] for p in px]
RUN = [p['run'] for p in px]
RI = [p['ri'] for p in px]
# sanity: order = i
assert all(p['i'] == i for i, p in enumerate(px))
# verify sACN address rule
runs = {r['run']: r for r in d['meta']['runs']}
for p in px:
    assert p['uni'] == runs[p['run']]['universes'][p['ri'] // 128], p
    assert p['ch'] == 1 + 4 * (p['ri'] % 128), p

def js(arr): return '[' + ','.join(str(v) for v in arr) + ']'
data = ("const SUN_DATA={N:%d,boards:%s,x:%s,y:%s,b:%s,run:%s,ri:%s,runs:%s};" % (
    N, json.dumps(BOARDS), js(X), js(Y), js(B), js(RUN), js(RI),
    json.dumps([[r['run'], r['boards'], r['pixels'], r['universes']] for r in d['meta']['runs']])))
open(os.path.dirname(__file__) + '/sun_data.js', 'w').write(data)
print('JS data', len(data), 'bytes')

# ---- C header ----
lines = []
lines.append('// sun_map.h — generated from sun-map.json (%d px). Do not edit; regenerate.' % N)
lines.append('// Coordinates: sun centre (0,0), mm×10 (int16), y-up. Output order = index i (DDP offset 4*i).')
lines.append('#pragma once\n#include <stdint.h>\n')
lines.append('#define SUN_N %d' % N)
lines.append('#define SUN_RUNS 8')
lines.append('#define SUN_UNIVERSES 24')
lines.append('#define SUN_PX_PER_UNIVERSE 128')
lines.append('#define SUN_BRIGHTNESS_CAP 0.90f   // power budget — keep')
lines.append('#define SUN_GAMMA 2.2f')
lines.append('#define SUN_RIM_R_MM 886.5f')
lines.append('#define SUN_PITCH_MM 30.0f\n')
lines.append('enum SunBoard : uint8_t { ' + ', '.join('SB_' + b for b in BOARDS) + ' };\n')
def carr(t, name, arr, fmt=str):
    body = ','.join(fmt(v) for v in arr)
    lines.append('static const %s %s[SUN_N] = {%s};\n' % (t, name, body))
carr('int16_t', 'SUN_X10', [round(x * 10) for x in X])
carr('int16_t', 'SUN_Y10', [round(y * 10) for y in Y])
carr('uint16_t', 'SUN_R10', [round(p['r'] * 10) for p in px])
carr('uint16_t', 'SUN_TH10', [round(p['th'] * 10) % 3600 for p in px])  # deg×10
carr('uint8_t', 'SUN_BOARD', B)
carr('uint8_t', 'SUN_RUN', RUN)
carr('uint16_t', 'SUN_RI', RI)
lines.append('// Per run: first global index, pixel count, first universe (universes are consecutive per run).')
lines.append('static const uint16_t SUN_RUN_START[9] = {' + ','.join(str(v) for v in [0] + [next(i for i, p in enumerate(px) if p['run'] == r) for r in range(1, 9)]) + '};')
lines.append('static const uint16_t SUN_RUN_LEN[9]   = {0,' + ','.join(str(runs[r]['pixels']) for r in range(1, 9)) + '};')
lines.append('static const uint8_t  SUN_RUN_UNI0[9]  = {0,' + ','.join(str(runs[r]['universes'][0]) for r in range(1, 9)) + '};\n')
lines.append('// sACN address of pixel i: universe = SUN_RUN_UNI0[SUN_RUN[i]] + SUN_RI[i]/128, start channel = 1 + 4*(SUN_RI[i]%128)')
lines.append('static inline uint8_t  sun_universe(uint16_t i){ return SUN_RUN_UNI0[SUN_RUN[i]] + (uint8_t)(SUN_RI[i] >> 7); }')
lines.append('static inline uint16_t sun_channel (uint16_t i){ return 1 + 4 * (SUN_RI[i] & 127); }')
lines.append('// Power supply zone 0 = disc (A), zones 1–6 = sectors (1,2),(3,4)…(11,12).')
lines.append('static inline uint8_t  sun_zone(uint16_t i){ uint8_t b = SUN_BOARD[i]; if (b == 0) return 0; uint8_t k = (b - 1) % 12 + 1; return (k + 1) / 2; }')
open(os.path.dirname(__file__) + '/sun_map.h', 'w').write('\n'.join(lines) + '\n')
print('header', os.path.getsize(os.path.dirname(__file__) + '/sun_map.h'), 'bytes')
