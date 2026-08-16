#!/usr/bin/env python3
"""Generate the three KiCad boards for the LED sun disc.

Run with KiCad's own interpreter so `pcbnew` is importable:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\
Versions/3.9/bin/python3 tools/gen_pcb.py

Geometry sources (all under sun-pcb-kicad-pack/):
  designs/design-X-outline.dxf    board outline -> Edge.Cuts
  designs/design-X-placement.csv  LED position / rotation / chain order
  docs/design.json                radii, pitch, feeds, LED + cap parts

Stackup, per docs/pcb-design-guide.md and docs/electrical-requirements.md:
  F.Cu (LED side)  : LEDs, 100 nF caps, R1, the data serpentine, GND pour
  B.Cu (wire side) : J1/J2 WAGOs, bulk caps, +5V pour
  every LED and every cap stitches its +5V pad down to the plane with a via
"""

from __future__ import annotations

import csv
import json
import math
import os
import shutil
import sys
from pathlib import Path

import wx                       # ZONE_FILLER segfaults without a live wxApp
_APP = wx.App()

import pcbnew

REPO = Path(__file__).resolve().parent.parent
PACK = REPO / "sun-pcb-kicad-pack"
FPDIR = Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")

MM = pcbnew.FromMM


def V(x_mm, y_mm):
    return pcbnew.VECTOR2I(MM(x_mm), MM(y_mm))


# ------------------------------------------------------------------ parts
LED_FP = ("LED_SMD", "LED_OPSCO_SK6812_PLCC4_5.0x5.0mm_P3.1mm")
C_FP = ("Capacitor_SMD", "C_0603_1608Metric")
CDIST_FP = ("Capacitor_SMD", "C_1206_3216Metric")
CBULK_FP = ("Capacitor_THT", "CP_Radial_D10.0mm_P5.00mm")
R_FP = ("Resistor_SMD", "R_0603_1608Metric")
J_FP = ("Connector_Wago", "Wago_734-133_1x03_P3.50mm_Vertical")

LED_VAL = "SK6812"
C_VAL = "0603/100nF/50V"
CDIST_VAL = "1206/22uF/25V"
CBULK_VAL = "D10/1000uF/10V"
R_VAL = "0603/100R/50V"

# ------------------------------------------------------------- dimensions
TRACK_DATA = 0.25        # mm, per pcb-design-guide.md
TRACK_STUB = 0.5         # pad -> stitching via
VIA_D, VIA_DRILL = 0.6, 0.3
ZONE_INSET = 0.5         # copper pulled back from Edge.Cuts
PART_EDGE_KEEP = 3.0     # docs: nothing inside 3 mm of the edge
PITCH = 30.0

CAP_GAP = 0.8            # design.json vinCap.gapMM — cap edge to package edge
CAP_DY = 3.75            # LED-local Y offset that satisfies CAP_GAP
VIA_DY = 3.4             # LED-local Y of the VDD stitching via
WRAP_X = 4.6             # LED-local X where a wrapped data hop clears the pads
OUT_LANE = 6.0           # cap-free lane a wrapped hop leaves DOUT through
IN_LANE = 8.0            # cap-side lane a wrapped hop arrives at DIN through
CROSS_X = 11.0           # LED-local X where a wrapped hop swaps lanes
# candidate wrap lanes as (side of A, lane on A, side of B, lane on B), where
# side is +1 for the cap side of that LED and -1 for the free side
LANES = [(-1, 6.0, 1, 8.0), (-1, 6.0, 1, 11.0), (-1, 9.0, 1, 8.0),
         (-1, 9.0, 1, 12.0), (1, 8.0, -1, 6.0), (1, 11.0, -1, 6.0),
         (-1, 6.0, -1, 12.0), (1, 8.0, 1, 14.0), (-1, 12.0, 1, 8.0),
         (-1, 6.0, 1, 14.0), (1, 11.0, 1, 8.0), (-1, 13.0, -1, 6.0)]
CROSSES = [11.0, 14.0, 8.0, 18.0, 22.0]
END_LANE = 8.0           # lane R1 and the chain-exit stub sit in
R1_X = 8.5               # LED-local X where R1 sits, past LED1's DIN end

# keepout discs used when hunting for space on the wire side
KO_LED, KO_CAP, KO_VIA, KO_TRACE = 5.0, 2.0, 0.6, 0.35
BOT_CLEAR = 0.6          # required gap from a wire-side part to anything

# The placement CSV orients every LED to its ring tangent in one fixed sense,
# but the chain serpentines, so on reversed rings DIN/DOUT face backwards and
# ~50% of hops have to wrap around the package.  Adding 180 deg to those LEDs
# makes every hop a short straight run; the RGBW 5050 is square and its lens is
# symmetric, so it is optically identical.  Off = obey the CSV verbatim.
FLIP_TO_CHAIN = os.environ.get("SUN_FLIP_TO_CHAIN", "1") == "1"


# ============================================================ dxf + outline
def read_dxf(path: Path):
    lines = [l.strip() for l in path.read_text().splitlines()]
    ents, i = [], 0
    while i < len(lines):
        if lines[i] == "0" and lines[i + 1] in ("CIRCLE", "POLYLINE", "VERTEX",
                                                "SEQEND"):
            kind, d, j = lines[i + 1], {}, i + 2
            while j < len(lines) - 1 and lines[j] != "0":
                d.setdefault(lines[j], []).append(lines[j + 1])
                j += 2
            ents.append((kind, d))
            i = j
        else:
            i += 1
    return ents


def load_outline(letter: str, meta: dict):
    """Return a dict describing Edge.Cuts, in placement-CSV coordinates."""
    ents = read_dxf(PACK / f"designs/design-{letter}-outline.dxf")
    circles = [(float(d["10"][0]), float(d["20"][0]), float(d["40"][0]))
               for k, d in ents if k == "CIRCLE" and d.get("8", [""])[0] == "CUT"]
    verts = [(float(d["10"][0]), float(d["20"][0]))
             for k, d in ents if k == "VERTEX" and d.get("8", [""])[0] == "CUT"]
    if circles:
        cx, cy, r = circles[0]
        return {"kind": "circle", "c": (cx, cy), "r": r}

    # annular sector: recover the common arc centre from the two radii, which
    # is exact to <0.001 mm and avoids trusting the DXF bulge sign convention
    o = meta["outline"]
    rO, rI = o["rOuterMM"], o["rInnerMM"]
    (x1, y1), (x2, y2), (x3, y3), (x4, y4) = verts
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    dx, dy = x2 - x1, y2 - y1
    ch = math.hypot(dx, dy)
    h = math.sqrt(max(rO * rO - (ch / 2) ** 2, 0))
    nx, ny = -dy / ch, dx / ch
    best = None
    for s in (1, -1):
        c = (mx + s * h * nx, my + s * h * ny)
        err = (abs(math.dist(c, (x3, y3)) - rI)
               + abs(math.dist(c, (x4, y4)) - rI))
        if best is None or err < best[1]:
            best = (c, err)
    c, err = best
    assert err < 0.01, (letter, err)
    return {"kind": "wedge", "c": c, "rO": rO, "rI": rI, "v": verts}


def arc_sweep(c, p, q, r, n):
    a1 = math.atan2(p[1] - c[1], p[0] - c[0])
    a2 = math.atan2(q[1] - c[1], q[0] - c[0])
    while a2 - a1 > math.pi:
        a2 -= 2 * math.pi
    while a1 - a2 > math.pi:
        a2 += 2 * math.pi
    return [(c[0] + r * math.cos(a1 + (a2 - a1) * i / n),
             c[1] + r * math.sin(a1 + (a2 - a1) * i / n)) for i in range(n + 1)]


def outline_polygon(g, inset=0.0, n=240):
    """Flattened board outline, optionally shrunk by `inset` mm."""
    if g["kind"] == "circle":
        cx, cy = g["c"]
        r = g["r"] - inset
        return [(cx + r * math.cos(2 * math.pi * i / n),
                 cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
    c, v = g["c"], g["v"]
    rO, rI = g["rO"] - inset, g["rI"] + inset
    # v1..v2 is the outer arc, v3..v4 the inner one, so the sector spans the
    # angles of v1 and v2 (v4 sits at v1's angle, v3 at v2's)
    a1 = math.atan2(v[0][1] - c[1], v[0][0] - c[0])
    a2 = math.atan2(v[1][1] - c[1], v[1][0] - c[0])
    while a2 - a1 > math.pi:
        a2 -= 2 * math.pi
    while a1 - a2 > math.pi:
        a2 += 2 * math.pi
    s = 1.0 if a2 > a1 else -1.0
    da = inset / max(rI, 1e-6)           # pull the radial edges in as well
    b1, b2 = a1 + s * da, a2 - s * da
    pt = lambda a, r: (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
    outer = [pt(b1 + (b2 - b1) * i / n, rO) for i in range(n + 1)]
    inner = [pt(b2 + (b1 - b2) * i / n, rI) for i in range(n + 1)]
    return outer + inner


def poly_inside(pt, pts):
    x, y = pt
    c = False
    for i in range(len(pts)):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % len(pts)]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def poly_dist(pt, pts):
    best = 1e9
    for i in range(len(pts)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        vx, vy = b[0] - a[0], b[1] - a[1]
        wx, wy = pt[0] - a[0], pt[1] - a[1]
        L = vx * vx + vy * vy
        t = 0.0 if L == 0 else max(0.0, min(1.0, (wx * vx + wy * vy) / L))
        best = min(best, math.hypot(pt[0] - (a[0] + t * vx),
                                    pt[1] - (a[1] + t * vy)))
    return best


# ================================================================= helpers
def local_to_board(px, py, rot_deg, lx, ly):
    """Footprint-local (lx, ly) -> board mm, KiCad convention (y down, CCW+)."""
    t = math.radians(rot_deg)
    return (px + lx * math.cos(t) + ly * math.sin(t),
            py - lx * math.sin(t) + ly * math.cos(t))


def rect_point_dist(cx, cy, w, h, rot_deg, pt):
    """Distance from a rotated w x h rectangle centred at (cx,cy) to a point."""
    t = math.radians(rot_deg)
    dx, dy = pt[0] - cx, pt[1] - cy
    lx = dx * math.cos(t) - dy * math.sin(t)
    ly = dx * math.sin(t) + dy * math.cos(t)
    ox = max(abs(lx) - w / 2, 0.0)
    oy = max(abs(ly) - h / 2, 0.0)
    return math.hypot(ox, oy)


def seg_seg_dist(a, b, c, d):
    """Shortest distance between two 2-D segments."""
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    d1, d2 = cross(c, d, a), cross(c, d, b)
    d3, d4 = cross(a, b, c), cross(a, b, d)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(seg_point_dist(a, b, c), seg_point_dist(a, b, d),
               seg_point_dist(c, d, a), seg_point_dist(c, d, b))


class Obstacles:
    """Coarse spatial index of everything already on F.Cu, so a candidate
    trace can be tested for clearance before it is committed."""

    CELL = 12.0

    def __init__(self):
        self.discs = {}
        self.segs = {}

    def _keys(self, x0, y0, x1, y1):
        for i in range(int(math.floor(x0 / self.CELL)),
                       int(math.floor(x1 / self.CELL)) + 1):
            for j in range(int(math.floor(y0 / self.CELL)),
                           int(math.floor(y1 / self.CELL)) + 1):
                yield (i, j)

    def add_disc(self, x, y, r, net):
        for k in self._keys(x - r, y - r, x + r, y + r):
            self.discs.setdefault(k, []).append((x, y, r, net))

    def add_seg(self, a, b, net, hw=TRACK_DATA / 2):
        for k in self._keys(min(a[0], b[0]) - hw, min(a[1], b[1]) - hw,
                            max(a[0], b[0]) + hw, max(a[1], b[1]) + hw):
            self.segs.setdefault(k, []).append((a, b, net, hw))

    def seg_clear(self, a, b, net, hw=TRACK_DATA / 2, gap=0.25):
        pad = 3.0
        for k in self._keys(min(a[0], b[0]) - pad, min(a[1], b[1]) - pad,
                            max(a[0], b[0]) + pad, max(a[1], b[1]) + pad):
            for x, y, r, n in self.discs.get(k, ()):
                if n == net:
                    continue
                if seg_point_dist(a, b, (x, y)) < r + hw + gap:
                    return False
            for c, d, n, h in self.segs.get(k, ()):
                if n == net:
                    continue
                if seg_seg_dist(a, b, c, d) < hw + h + gap:
                    return False
        return True

    def path_clear(self, pts, net, hw=TRACK_DATA / 2):
        return all(self.seg_clear(p, q, net, hw)
                   for p, q in zip(pts, pts[1:]))


def seg_point_dist(a, b, p):
    vx, vy = b[0] - a[0], b[1] - a[1]
    wx, wy = p[0] - a[0], p[1] - a[1]
    L = vx * vx + vy * vy
    t = 0.0 if L == 0 else max(0.0, min(1.0, (wx * vx + wy * vy) / L))
    return math.hypot(p[0] - (a[0] + t * vx), p[1] - (a[1] + t * vy))


class Keepouts:
    """Top-side copper, as discs and segments, for wire-side space hunting."""

    def __init__(self):
        self.discs = []      # (x, y, r)
        self.segs = []       # (a, b, half_width)

    def disc(self, x, y, r):
        self.discs.append((x, y, r))

    def seg(self, a, b, hw=KO_TRACE):
        self.segs.append((a, b, hw))

    def clearance(self, cx, cy, w, h, rot):
        best = 1e9
        for x, y, r in self.discs:
            best = min(best, rect_point_dist(cx, cy, w, h, rot, (x, y)) - r)
            if best < 0:
                return best
        for a, b, hw in self.segs:
            # sample the segment; hops are short so this is plenty
            n = max(2, int(math.dist(a, b) / 1.0))
            for i in range(n + 1):
                p = (a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n)
                best = min(best, rect_point_dist(cx, cy, w, h, rot, p) - hw)
                if best < 0:
                    return best
        return best


# ============================================================ board builder
class BoardBuilder:
    def __init__(self, letter, meta, rows, sheet_uuid, sym_uuid):
        self.L = letter
        self.meta = meta
        self.rows = rows
        self.n = len(rows)
        self.sheet_uuid = sheet_uuid
        self.sym_uuid = sym_uuid
        self.g = load_outline(letter, meta)
        self.board = pcbnew.CreateEmptyBoard()
        self.nets = {}
        self.ko = Keepouts()
        self.obs = Obstacles()          # F.Cu
        self.obs_b = Obstacles()        # B.Cu
        self.unrouted = []
        self.bottom_rects = []          # (cx, cy, w, h, rot)
        self.ndist = max(1, round(self.n / 30))

        c = self.g["c"]
        self.centre = c
        self.rad = [math.dist(c, (float(r["PosX_mm"]), float(r["PosY_mm"])))
                    for r in rows]
        self.rmin, self.rmax = min(self.rad), max(self.rad)
        # free annular bands lie half a pitch away from every LED ring
        lo = self.rmin - PITCH / 2
        bands = [lo + PITCH * i for i in
                 range(int(round((self.rmax - self.rmin) / PITCH)) + 2)]
        self.bands = [b for b in bands if self._band_ok(b)]

    # ---------------------------------------------------------- infra
    def _band_ok(self, r):
        if r <= 6:
            return False
        if self.g["kind"] == "circle":
            return r + 8 < self.g["r"] - PART_EDGE_KEEP
        return (self.g["rI"] + PART_EDGE_KEEP + 8 < r
                < self.g["rO"] - PART_EDGE_KEEP - 8)

    def net(self, name):
        if name not in self.nets:
            ni = pcbnew.NETINFO_ITEM(self.board, name)
            self.board.Add(ni)
            self.nets[name] = ni
        return self.nets[name]

    def fp(self, lib, ref, value, x, y, rot, bottom=False, uuid_key=None):
        f = pcbnew.FootprintLoad(str(FPDIR / (lib[0] + ".pretty")), lib[1])
        if f is None:
            raise RuntimeError(f"footprint not found: {lib}")
        self.board.Add(f)
        f.SetReference(ref)
        f.SetValue(value)
        f.SetPosition(V(x, y))
        if bottom:
            f.SetLayerAndFlip(pcbnew.B_Cu)
        f.SetOrientationDegrees(rot)
        f.Reference().SetVisible(True)
        f.Value().SetVisible(False)
        u = self.sym_uuid.get(uuid_key or ref)
        if u:
            f.SetPath(pcbnew.KIID_PATH(f"/{self.sheet_uuid}/{u}"))
        return f

    def pad(self, f, number):
        return f.FindPadByNumber(str(number))

    @staticmethod
    def mm(p):
        return p if isinstance(p, tuple) else (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y))

    def wire(self, p1, p2, net, width=TRACK_DATA, layer=pcbnew.F_Cu):
        t = pcbnew.PCB_TRACK(self.board)
        t.SetStart(p1 if isinstance(p1, pcbnew.VECTOR2I) else V(*p1))
        t.SetEnd(p2 if isinstance(p2, pcbnew.VECTOR2I) else V(*p2))
        t.SetWidth(MM(width))
        t.SetLayer(layer)
        t.SetNet(net)
        self.board.Add(t)
        idx = self.obs if layer == pcbnew.F_Cu else self.obs_b
        idx.add_seg(self.mm(p1), self.mm(p2), net.GetNetname(), width / 2)

    def register_pads(self, f):
        """Index every pad of `f`, on whichever copper layers it reaches."""
        for p in f.Pads():
            pos, sz = p.GetPosition(), p.GetSize()
            r = math.hypot(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y)) / 2
            x, y = pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y)
            if p.IsOnLayer(pcbnew.F_Cu):
                self.obs.add_disc(x, y, r, p.GetNetname())
            if p.IsOnLayer(pcbnew.B_Cu):
                self.obs_b.add_disc(x, y, r, p.GetNetname())

    def route(self, pts, net, width=TRACK_DATA, layer=pcbnew.F_Cu):
        for p, q in zip(pts, pts[1:]):
            self.wire(p, q, net, width, layer)

    def via(self, x, y, net):
        v = pcbnew.PCB_VIA(self.board)
        v.SetPosition(V(x, y))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetWidth(MM(VIA_D))
        v.SetDrill(MM(VIA_DRILL))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(net)
        self.board.Add(v)
        self.obs.add_disc(x, y, VIA_D / 2, net.GetNetname())
        self.obs_b.add_disc(x, y, VIA_D / 2, net.GetNetname())
        return v

    # ---------------------------------------------------------- build
    def build(self):
        self.setup()
        self.edge_cuts()
        self.leds_and_caps()
        self.data_chain()
        self.wire_side()
        self.add_zones()
        return self.board

    def setup(self):
        b = self.board
        bds = b.GetDesignSettings()
        bds.SetCopperLayerCount(2)
        bds.m_TrackMinWidth = MM(0.2)
        bds.m_MinClearance = MM(0.2)
        bds.m_ViasMinSize = MM(0.6)
        bds.m_MinThroughDrill = MM(0.3)
        bds.m_CopperEdgeClearance = MM(0.3)
        b.SetCopperLayerCount(2)

        pts = outline_polygon(self.g)
        minx = min(p[0] for p in pts)
        miny = min(p[1] for p in pts)
        maxx = max(p[0] for p in pts)
        maxy = max(p[1] for p in pts)
        self.off = (round(30 - minx), round(30 - miny))
        w, h = maxx - minx + 60, maxy - miny + 60
        # PAGE_INFO is not exposed to swig; patched into the file after Save()
        self.page = "A1" if (w <= 841 and h <= 594) else "A0"
        # keep KiCad's readout identical to the placement CSV
        bds.SetAuxOrigin(V(*self.off))
        bds.SetGridOrigin(V(*self.off))
        tb = b.GetTitleBlock()
        tb.SetTitle(f"Sun LED Disc — Design {self.L} "
                    f"({self.meta['kind']}, {self.n} px)")
        tb.SetRevision("A")
        tb.SetCompany("the-sun")
        tb.SetComment(0, "LEDs + 100nF + R1 + data on F.Cu; WAGOs + bulk caps "
                         "on B.Cu; GND pour F.Cu, +5V pour B.Cu")
        tb.SetComment(1, "Placement is design-%s-placement.csv verbatim; grid "
                         "origin = that CSV's (0,0)" % self.L)
        b.SetTitleBlock(tb)

    def P(self, x, y):
        """CSV coordinates -> board coordinates."""
        return V(x + self.off[0], y + self.off[1])

    def xy(self, x, y):
        return (x + self.off[0], y + self.off[1])

    def unxy(self, x, y):
        """Board coordinates -> placement-CSV coordinates."""
        return (x - self.off[0], y - self.off[1])

    # ---------------------------------------------------------- outline
    def edge_cuts(self):
        b, g = self.board, self.g
        def shape():
            s = pcbnew.PCB_SHAPE(b)
            s.SetLayer(pcbnew.Edge_Cuts)
            s.SetWidth(MM(0.1))
            b.Add(s)
            return s
        if g["kind"] == "circle":
            cx, cy = g["c"]
            s = shape()
            s.SetShape(pcbnew.SHAPE_T_CIRCLE)
            s.SetCenter(self.P(cx, cy))
            s.SetEnd(self.P(cx + g["r"], cy))
            return
        c, v, rO, rI = g["c"], g["v"], g["rO"], g["rI"]
        def mid(p, q, r):
            a1 = math.atan2(p[1] - c[1], p[0] - c[0])
            a2 = math.atan2(q[1] - c[1], q[0] - c[0])
            while a2 - a1 > math.pi:
                a2 -= 2 * math.pi
            while a1 - a2 > math.pi:
                a2 += 2 * math.pi
            a = (a1 + a2) / 2
            return (c[0] + r * math.cos(a), c[1] + r * math.sin(a))
        for p, q, r in ((v[0], v[1], rO), (v[2], v[3], rI)):
            s = shape()
            s.SetShape(pcbnew.SHAPE_T_ARC)
            s.SetArcGeometry(self.P(*p), self.P(*mid(p, q, r)), self.P(*q))
        for p, q in ((v[1], v[2]), (v[3], v[0])):
            s = shape()
            s.SetShape(pcbnew.SHAPE_T_SEGMENT)
            s.SetStart(self.P(*p))
            s.SetEnd(self.P(*q))

    # ---------------------------------------------------- LEDs + 100nF
    def leds_and_caps(self):
        v5, gnd = self.net("+5V"), self.net("GND")
        self.led_fp, self.cap_fp = [], []
        self._pending_pads = []
        self.led_pose = []          # (x, y, rot, cap_side) per LED, board mm
        pos = [(float(r["PosX_mm"]), float(r["PosY_mm"])) for r in self.rows]
        for k, row in enumerate(self.rows, 1):
            x, y = float(row["PosX_mm"]), float(row["PosY_mm"])
            rot = float(row["Rot_deg"])
            if FLIP_TO_CHAIN:
                nxt = pos[k] if k < self.n else pos[k - 1]
                prv = pos[k - 2] if k > 1 else pos[k - 1]
                dx, dy = nxt[0] - prv[0], nxt[1] - prv[1]
                t = math.radians(rot)
                if dx * math.cos(t) - dy * math.sin(t) < 0:   # +X faces backward
                    rot = (rot + 180.0) % 360.0
            bx, by = self.xy(x, y)
            f = self.fp(LED_FP, f"LED{k}", LED_VAL, bx, by, rot)
            f.Reference().SetLayer(pcbnew.F_Fab)     # silk is far too dense here
            self.led_fp.append(f)
            self._pending_pads.append(f)
            self.pad(f, 1).SetNet(gnd)
            self.pad(f, 3).SetNet(v5)
            self.pad(f, 2).SetNet(self.net(f"D{k}"))
            self.pad(f, 4).SetNet(self.net("DATA_OUT" if k == self.n
                                           else f"D{k+1}"))
            self.ko.disc(bx, by, KO_LED)

            # VDD stitching via, just outside the courtyard below pad 3
            vx, vy = local_to_board(bx, by, rot, 2.55, VIA_DY)
            self.via(vx, vy, v5)
            self.wire(self.pad(f, 3).GetPosition(), V(vx, vy), v5, TRACK_STUB)
            self.ko.disc(vx, vy, KO_VIA)

            # 100 nF beside the LED, side alternating ring by ring
            side = 1 if round((self.rad[k - 1] - self.rmin) / PITCH) % 2 == 0 else -1
            self.led_pose.append((bx, by, rot, side))
            cx, cy = local_to_board(bx, by, rot, 0.0, side * CAP_DY)
            c = self.fp(C_FP, f"C{k}", C_VAL, cx, cy, rot)
            c.Reference().SetLayer(pcbnew.F_Fab)
            self.cap_fp.append(c)
            self._pending_pads.append(c)
            gpad, ppad = (1, 2) if side > 0 else (2, 1)
            self.pad(c, gpad).SetNet(gnd)
            self.pad(c, ppad).SetNet(v5)
            self.ko.disc(cx, cy, KO_CAP)
            pp = self.pad(c, ppad).GetPosition()
            lx = -0.775 if ppad == 1 else 0.775
            cvx, cvy = local_to_board(cx, cy, rot, lx, side * 1.6)
            self.via(cvx, cvy, v5)
            self.wire(pp, V(cvx, cvy), v5, TRACK_STUB)
            self.ko.disc(cvx, cvy, KO_VIA)
        for f in self._pending_pads:
            self.register_pads(f)

    # ------------------------------------------------------ data chain
    @staticmethod
    def to_local(pose, p):
        """Board mm -> that LED's local frame."""
        px, py, rot, _ = pose
        t = math.radians(rot)
        dx, dy = p[0] - px, p[1] - py
        return (dx * math.cos(t) - dy * math.sin(t),
                dx * math.sin(t) + dy * math.cos(t))

    @staticmethod
    def wrap_path(A, B, a, b, coef_a, lane_a, coef_b, lane_b, cross):
        """Trace that leaves DOUT sideways, crosses the package in a lane and
        comes back down onto the next DIN."""
        L = lambda P, lx, ly: local_to_board(P[0], P[1], P[2], lx, ly)
        ya, yb = coef_a * A[3] * lane_a, coef_b * B[3] * lane_b
        return [a,
                L(A, WRAP_X, -1.6),
                L(A, WRAP_X, ya),
                L(A, -cross, ya),
                L(B, cross, yb),
                L(B, -WRAP_X, yb),
                L(B, -WRAP_X, 1.6),
                b]

    def hop_candidates(self, k):
        """Ordered candidate routes for LED k .DOUT -> LED k+1 .DIN (1-based).

        The LEDs stay tangent-aligned the whole way round a ring but the chain
        serpentines, so on every other ring DOUT faces *away* from the next
        pixel and a straight pad-to-pad trace would run back across the LED's
        own body.  Those hops wrap around the package instead: out through the
        lane on the cap-free side, in through a lane on the cap side, swapping
        over in the empty gap between the two packages.  Putting both halves on
        the same side cannot work — whichever one runs closer in gets cut by
        the other's stub down to the pad row.
        """
        A, B = self.led_pose[k - 1], self.led_pose[k]
        a = self.mm(self.pad(self.led_fp[k - 1], 4).GetPosition())
        b = self.mm(self.pad(self.led_fp[k], 2).GetPosition())
        out = []
        # forward when the next DIN sits off the DOUT end and vice versa
        if self.to_local(A, b)[0] > 3.0 and self.to_local(B, a)[0] < -3.0:
            out.append([a, b])
        for coef_a, lane_a, coef_b, lane_b in LANES:
            for cross in CROSSES:
                out.append(self.wrap_path(A, B, a, b, coef_a, lane_a,
                                          coef_b, lane_b, cross))
        return out

    def data_chain(self):
        self.wrapped = 0
        for k in range(1, self.n):
            net = self.net(f"D{k+1}")
            cands = self.hop_candidates(k)
            pts = next((c for c in cands
                        if self.obs.path_clear(c, net.GetNetname())), None)
            if pts is None:
                pts = cands[-1]
                self.unrouted.append(f"D{k+1}")
            if len(pts) > 2:
                self.wrapped += 1
            self.route(pts, net)
            for p, q in zip(pts, pts[1:]):
                self.ko.seg(p, q)

    # ------------------------------------------------- wire-side parts
    def polar(self, x, y):
        return (math.dist(self.centre, (x, y)),
                math.atan2(y - self.centre[1], x - self.centre[0]))

    def find_spot(self, tgt_xy, w, h, thru=True, prefer_r=None):
        """Nearest clear slot in a free band, tangentially oriented."""
        tr, ta = self.polar(*tgt_xy)
        bands = sorted(self.bands,
                       key=lambda b: abs(b - (prefer_r if prefer_r else tr)))
        clip = outline_polygon(self.g, PART_EDGE_KEEP)
        for r in bands[:6]:
            for step in range(0, 220):
                for sgn in (1, -1):
                    a = ta + sgn * step * (2.2 / r)
                    cx = self.centre[0] + r * math.cos(a)
                    cy = self.centre[1] + r * math.sin(a)
                    rot = -math.degrees(a) + 90.0   # long axis tangential
                    if not self._corners_ok(cx, cy, w, h, rot, clip):
                        continue
                    ok = True
                    for (ox, oy, ow, oh, orot) in self.bottom_rects:
                        if rect_point_dist(ox, oy, ow + w, oh + h, orot,
                                           (cx, cy)) < BOT_CLEAR:
                            ok = False
                            break
                    if not ok:
                        continue
                    if thru and self.ko.clearance(*self.xy(cx, cy), w, h,
                                                  rot) < BOT_CLEAR:
                        continue
                    self.bottom_rects.append((cx, cy, w, h, rot))
                    return cx, cy, rot
                if step == 0:
                    continue
        raise RuntimeError(f"no room for a {w}x{h} part near {tgt_xy}")

    def _corners_ok(self, cx, cy, w, h, rot, clip):
        t = math.radians(rot)
        for sx in (-1, 1):
            for sy in (-1, 1):
                lx, ly = sx * w / 2, sy * h / 2
                px = cx + lx * math.cos(t) + ly * math.sin(t)
                py = cy - lx * math.sin(t) + ly * math.cos(t)
                if not poly_inside((px, py), clip):
                    return False
        return True

    def lane_side(self, P, lx):
        """Pick the lane side beside LED P that stays inside the board."""
        clip = outline_polygon(self.g, PART_EDGE_KEEP + 1.0)
        for s in (-P[3], P[3]):              # cap-free side first
            p = local_to_board(P[0], P[1], P[2], lx, s * END_LANE)
            if poly_inside(self.unxy(*p), clip):
                return s
        return -P[3]

    def chain_ends(self):
        """R1 at LED1, plus the stub that carries DATA_OUT clear of the last
        LED. Both leave a landing pad that J1/J2 are then placed against."""
        A, Z = self.led_pose[0], self.led_pose[-1]
        LA = lambda lx, ly: local_to_board(A[0], A[1], A[2], lx, ly)
        LZ = lambda lx, ly: local_to_board(Z[0], Z[1], Z[2], lx, ly)

        # R1 sits past LED1's DIN end, in a lane beside the package: DATA_IN on
        # its outer pad, D1 on the pad facing the LED, so neither run doubles
        # back over the other.
        fa = self.lane_side(A, -R1_X) * END_LANE
        rx, ry = LA(-R1_X, fa)
        r1 = self.fp(R_FP, "R1", R_VAL, rx, ry, A[2])
        r1.Reference().SetLayer(pcbnew.F_Fab)
        self.pad(r1, 1).SetNet(self.net("DATA_IN"))
        self.pad(r1, 2).SetNet(self.net("D1"))
        self.register_pads(r1)
        self.ko.disc(rx, ry, 2.5)
        self.route([self.mm(self.pad(r1, 2).GetPosition()),
                    LA(-WRAP_X, fa), LA(-WRAP_X, 1.6),
                    self.mm(self.pad(self.led_fp[0], 2).GetPosition())],
                   self.net("D1"))
        # DATA_IN drops straight to the wire side: the connector lives down
        # there anyway, and on the tighter designs there is no room to reach it
        # across the LED ring on F.Cu without cutting the ring's own trace.
        self.in_land = LA(-R1_X - 2.6, fa)
        self.route([self.mm(self.pad(r1, 1).GetPosition()), self.in_land],
                   self.net("DATA_IN"))
        self.via(self.in_land[0], self.in_land[1], self.net("DATA_IN"))

        fz = self.lane_side(Z, WRAP_X) * END_LANE
        self.out_land = LZ(WRAP_X, fz)
        self.route([self.mm(self.pad(self.led_fp[-1], 4).GetPosition()),
                    LZ(WRAP_X, -1.6), self.out_land], self.net("DATA_OUT"))
        self.via(self.out_land[0], self.out_land[1], self.net("DATA_OUT"))

    def connect_connector(self, f, land, net):
        """Wire-side run from the landing via to pin 3 of a WAGO, approached
        from the pin-3 end so it never crosses the +5V or GND pins."""
        p1 = self.mm(self.pad(f, 1).GetPosition())
        p3 = self.mm(self.pad(f, 3).GetPosition())
        ux, uy = p3[0] - p1[0], p3[1] - p1[1]
        L = math.hypot(ux, uy) or 1.0
        ux, uy = ux / L, uy / L
        nx, ny = -uy, ux
        cands = []
        for out in (4.0, 7.0):
            tip = (p3[0] + ux * out, p3[1] + uy * out)
            cands.append([land, tip, p3])
            for side in (1, -1):
                cands.append([land, (tip[0] + nx * side * 7.0,
                                     tip[1] + ny * side * 7.0), tip, p3])
        cands.append([land, p3])
        pts = next((c for c in cands
                    if self.obs_b.path_clear(c, net.GetNetname())), cands[0])
        self.route(pts, net, layer=pcbnew.B_Cu)

    def wire_side(self):
        v5, gnd = self.net("+5V"), self.net("GND")
        self.chain_ends()

        # --- J1 / J2 : the only two connectors, WAGO 1x3 on the wire side
        jw, jh = 16.0, 12.0                      # footprint envelope + margin
        for ref, tgt, datanet in (("J1", self.unxy(*self.in_land), "DATA_IN"),
                                  ("J2", self.unxy(*self.out_land), "DATA_OUT")):
            cx, cy, rot = self.find_spot(tgt, jw, jh)
            bx, by = self.xy(cx, cy)
            # centre the 3 pads (P3.5, pad1 at origin) on the slot
            ox, oy = local_to_board(bx, by, rot, -3.5, 0.0)
            f = self.fp(J_FP, ref, "WAGO 1x3", ox, oy, rot, bottom=True)
            self.pad(f, 1).SetNet(v5)
            self.pad(f, 2).SetNet(gnd)
            self.pad(f, 3).SetNet(self.net(datanet))
            for p in (self.pad(f, i).GetPosition() for i in (1, 2, 3)):
                self.ko.disc(pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), 2.0)
            self.register_pads(f)
            setattr(self, ref.lower() + "_fp", f)

            # bulk electrolytic right beside its connector
            cref = f"C{self.n + (1 if ref == 'J1' else 2)}"
            bw, bh = 18.0, 13.0
            bcx, bcy, brot = self.find_spot((cx, cy), bw, bh)
            bbx, bby = self.xy(bcx, bcy)
            bx2, by2 = local_to_board(bbx, bby, brot, -2.5, 0.0)
            bf = self.fp(CBULK_FP, cref, CBULK_VAL, bx2, by2, brot, bottom=True)
            self.pad(bf, 1).SetNet(v5)
            self.pad(bf, 2).SetNet(gnd)
            self.register_pads(bf)
            for p in (self.pad(bf, i).GetPosition() for i in (1, 2)):
                self.ko.disc(pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), 2.2)

        # --- close the last gap from each landing point to its connector
        for fp_, land, netname in ((self.j1_fp, self.in_land, "DATA_IN"),
                                   (self.j2_fp, self.out_land, "DATA_OUT")):
            self.connect_connector(fp_, land, self.net(netname))

        # --- distributed 22 uF, ~1 per 30 LEDs, spread along the chain
        for i in range(self.ndist):
            k = min(self.n, (i + 1) * 30) - 1
            tgt = (float(self.rows[k]["PosX_mm"]), float(self.rows[k]["PosY_mm"]))
            cx, cy, rot = self.find_spot(tgt, 8.0, 6.0, thru=False)
            bx, by = self.xy(cx, cy)
            c = self.fp(CDIST_FP, f"C{self.n + 2 + 1 + i}", CDIST_VAL,
                        bx, by, rot, bottom=True)
            c.Reference().SetLayer(pcbnew.B_Fab)
            self.pad(c, 1).SetNet(v5)
            self.pad(c, 2).SetNet(gnd)
            # GND pad needs a via up to the LED-side pour
            gx, gy = local_to_board(bx, by, rot, 1.475, 2.2)
            self.via(gx, gy, gnd)
            self.wire(self.pad(c, 2).GetPosition(), V(gx, gy), gnd,
                      TRACK_STUB, pcbnew.B_Cu)
            self.ko.disc(gx, gy, KO_VIA)

    # ---------------------------------------------------------- pours
    def add_zones(self):
        """Add the two pours unfilled. Filling has to happen after a
        save/reload round trip: ZONE_FILLER segfaults on a board built with
        CreateEmptyBoard(), which carries no PROJECT context."""
        for layer, netname, prio in ((pcbnew.F_Cu, "GND", 0),
                                     (pcbnew.B_Cu, "+5V", 0)):
            z = pcbnew.ZONE(self.board)
            z.SetLayer(layer)
            z.SetNet(self.net(netname))
            z.SetAssignedPriority(prio)
            z.SetLocalClearance(MM(0.25))
            z.SetMinThickness(MM(0.2))
            z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
            z.SetThermalReliefGap(MM(0.3))
            z.SetThermalReliefSpokeWidth(MM(0.5))
            out = z.Outline()
            out.NewOutline()
            for x, y in outline_polygon(self.g, ZONE_INSET):
                out.Append(MM(x + self.off[0]), MM(y + self.off[1]))
            self.board.Add(z)


# =================================================================== main
def schematic_uuids(letter):
    """Map reference designator -> schematic symbol uuid, for board<->sch link."""
    import re
    sch = (REPO / f"sun-pcb-design-{letter}"
           / f"sun-pcb-design-{letter}.kicad_sch").read_text()
    root = re.search(r'\(uuid "([0-9a-f-]+)"\)', sch).group(1)
    out = {}
    for m in re.finditer(r'\(uuid "([0-9a-f-]+)"\)\n\t\t\(property "Reference" '
                         r'"([^"]+)"', sch):
        out[m.group(2)] = m.group(1)
    return root, out


def main():
    design = json.loads((PACK / "docs" / "design.json").read_text())
    only = sys.argv[1:] or ["A", "B", "C"]
    for meta in design["designs"]:
        L = meta["letter"]
        if L not in only:
            continue
        rows = list(csv.DictReader(
            (PACK / meta["files"]["placementCsv"]).open()))
        root, uu = schematic_uuids(L)
        bb = BoardBuilder(L, meta, rows, root, uu)
        board = bb.build()
        out = REPO / f"sun-pcb-design-{L}" / f"sun-pcb-design-{L}.kicad_pcb"
        board.Save(str(out))
        txt = out.read_text().replace('(paper "A4")', f'(paper "{bb.page}")', 1)
        out.write_text(txt)
        nfp, ntr = len(board.GetFootprints()), len(board.GetTracks())
        del board, bb
        board = pcbnew.LoadBoard(str(out))    # reload so the filler has a PROJECT
        board.BuildConnectivity()
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
        area = [round(pcbnew.ToMM(pcbnew.ToMM(z.GetFilledArea())))
                for z in board.Zones()]
        board.Save(str(out))
        print(f"design {L}: {len(rows)} LEDs, {nfp} footprints, "
              f"{ntr} tracks+vias, pours {area} mm2 -> {out.name}")


if __name__ == "__main__":
    main()
