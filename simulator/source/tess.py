import json, struct, base64, numpy as np, sys
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TDocStd import TDocStd_Document
from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ColorSurf, XCAFDoc_ColorGen
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDF import TDF_LabelSequence, TDF_Label
from OCP.TDataStd import TDataStd_Name
from OCP.IFSelect import IFSelect_RetDone
from OCP.TopLoc import TopLoc_Location
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.TopExp import TopExp_Explorer
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.BRep import BRep_Tool
from OCP.Quantity import Quantity_Color
from OCP.TopoDS import TopoDS

F="/root/.claude/uploads/d40cdf4f-8d7e-5f00-aa8b-8147e93e6357/89c480e7-BIG_LIGHT_9.1.step"
LIN, ANG = float(sys.argv[1]), float(sys.argv[2])
doc = TDocStd_Document(TCollection_ExtendedString("doc"))
r = STEPCAFControl_Reader(); r.SetNameMode(True); r.SetColorMode(True)
assert r.ReadFile(F) == IFSelect_RetDone; r.Transfer(doc)
st = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main()); ct = XCAFDoc_DocumentTool.ColorTool_s(doc.Main())
def name(l):
    n = TDataStd_Name(); return n.Get().ToExtString() if l.FindAttribute(TDataStd_Name.GetID_s(), n) else ''
def color_of(lbl, shape):
    c = Quantity_Color()
    for t in (XCAFDoc_ColorSurf, XCAFDoc_ColorGen):
        if ct.GetColor_s(lbl, t, c): return [round(c.Red(),3), round(c.Green(),3), round(c.Blue(),3)]
    return None
parts=[]
def walk(lbl, loc, depth):
    ref = TDF_Label()
    if st.IsReference_s(lbl): st.GetReferredShape_s(lbl, ref)
    else: ref = lbl
    l2 = loc.Multiplied(st.GetLocation_s(lbl)) if st.IsReference_s(lbl) else loc
    if st.IsAssembly_s(ref):
        comps = TDF_LabelSequence(); st.GetComponents_s(ref, comps)
        for i in range(1, comps.Length()+1): walk(comps.Value(i), l2, depth+1)
        return
    shp = st.GetShape_s(ref).Moved(l2)
    BRepMesh_IncrementalMesh(shp, LIN, False, ANG, True)
    P=[]; I=[]; base=0
    ex = TopExp_Explorer(shp, TopAbs_FACE)
    while ex.More():
        f = TopoDS.Face_s(ex.Current()); L = TopLoc_Location(); tri = BRep_Tool.Triangulation_s(f, L)
        if tri is not None:
            T = L.Transformation(); rev = f.Orientation() == TopAbs_REVERSED
            n = tri.NbNodes()
            for k in range(1, n+1):
                p = tri.Node(k).Transformed(T); P.append((p.X(), p.Y(), p.Z()))
            for k in range(1, tri.NbTriangles()+1):
                a,b,c = tri.Triangle(k).Get()
                I.append((base+a-1, base+c-1, base+b-1) if rev else (base+a-1, base+b-1, base+c-1))
            base += n
        ex.Next()
    col = color_of(ref, st.GetShape_s(ref)) or color_of(lbl, shp)
    parts.append(dict(name=name(ref), inst=name(lbl), color=col, P=np.array(P,np.float32), I=np.array(I,np.uint32)))
roots = TDF_LabelSequence(); st.GetFreeShapes(roots)
for i in range(1, roots.Length()+1): walk(roots.Value(i), TopLoc_Location(), 0)
tot=0
for p in parts:
    tot += len(p['I']); print(len(p['I']), 'tris', p['name'][:36], p['color'])
print('TOTAL tris', tot, 'verts', sum(len(p['P']) for p in parts))
np.save('parts_P.npy', np.array([p['P'] for p in parts], dtype=object), allow_pickle=True)
np.save('parts_I.npy', np.array([p['I'] for p in parts], dtype=object), allow_pickle=True)
json.dump([dict(name=p['name'], inst=p['inst'], color=p['color']) for p in parts], open('parts_meta.json','w'))
