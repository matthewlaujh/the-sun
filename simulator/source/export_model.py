"""Pack the tessellated STEP parts into sun_model.js (sim frame: x=X, y=Z, z=-Y; int16 mm*10; uint16 indices, chunked)."""
import numpy as np, json, base64
P = np.load('parts_P.npy', allow_pickle=True); I = np.load('parts_I.npy', allow_pickle=True); M = json.load(open('parts_meta.json'))
names = [m['name'] for m in M]
LAYERS = {
    'skel':  (lambda n: n.startswith('LED Skeleton'), [0.30, 0.31, 0.34]),
    'ring':  (lambda n: n.startswith('JL-8') or n.startswith('Connecting') or n.startswith('EDL') or n.startswith('TPAX') or n.startswith('EPDA'), [0.80, 0.81, 0.83]),
    'mount': (lambda n: n.startswith('Wall Mount') or 'TYZB' in n or 'EABA' in n or 'TPAH' in n, [0.80, 0.81, 0.83]),
    'tray':  (lambda n: n == 'Component1', [0.35, 0.35, 0.35]),
    'stand': (lambda n: n == 'Stand', [0.22, 0.23, 0.25]),
    'dome':  (lambda n: n.startswith('Dome'), [0.92, 0.92, 0.90]),
    'flat':  (lambda n: n == '2nd Diffuser', [0.92, 0.92, 0.90]),
}
def b64(a): return base64.b64encode(a.tobytes()).decode()
out = {}; stats = {}
for layer, (pred, defcol) in LAYERS.items():
    chunks = []; tris = 0
    for k, n in enumerate(names):
        if not pred(n): continue
        p = P[k].astype(np.float64); t = I[k]
        sim = np.stack([p[:, 0], p[:, 2], -p[:, 1]], 1)               # X, Z, -Y
        col = M[k]['color'] or defcol
        # chunk triangles so each chunk has < 65535 vertices (unwelded -> flat shading, as CAD should look)
        step = 20000
        for s in range(0, len(t), step):
            tt = t[s:s + step]
            uniq, inv = np.unique(tt.ravel(), return_inverse=True)
            v = sim[uniq]; origin = v.min(0)
            q = np.round((v - origin) * 10).astype(np.int16)
            chunks.append(dict(c=col, o=[round(float(x), 1) for x in origin], n=len(uniq), p=b64(q), i=b64(inv.astype(np.uint16))))
            tris += len(tt)
    out[layer] = chunks; stats[layer] = tris
# dome depth profile: outer surface z (sim) vs radius, 25 mm bins
dome = P[names.index('Dome7.1_flat_1800-200')]; r = np.hypot(dome[:, 0], dome[:, 2])
prof = []
for r0 in range(0, 901, 25):
    m = (r >= r0) & (r < r0 + 25); prof.append(round(float(-dome[m, 1].min()), 1) if m.any() else prof[-1])
out['domeZ'] = prof  # sim z of the dome's front surface at r = 0,25,...,900
js = 'const SUN_MODEL=' + json.dumps(out, separators=(',', ':')) + ';'
open('/home/claude/kit/sun_model.js', 'w').write(js)
print('tris per layer', stats, 'total', sum(stats.values()))
print('sun_model.js', round(len(js) / 1e6, 2), 'MB'); print('domeZ', prof)
