"""Read-only spatial context helpers for the ShipD scenery patch."""
import itertools
import json
import math
import re
from pathlib import Path
import numpy as np

SOURCE = Path(r'C:/Users/ryans/Desktop/Enhanced SCCT Versus 3.6 (Map editing copy)/Packages/MapsEd/map-authoring.json')
data = json.loads(SOURCE.read_text())
assets = {a['path']: a for a in data['assets']['StaticMesh']}


def vector(block, key, default=(0, 0, 0), axes='XYZ'):
    m = re.search(r'^\s*' + key + r'=\(([^)]*)\)', block, re.M)
    vals = dict(re.findall(r'(\w+)=([^,()]+)', m[1])) if m else {}
    return np.array([float(vals.get(k, default[i])) for i, k in enumerate(axes)])


def rotation(v):
    p, y, r = v * (2 * math.pi / 65536)
    cp, sp, cy, sy, cr, sr = math.cos(p), math.sin(p), math.cos(y), math.sin(y), math.cos(r), math.sin(r)
    return np.array([[cp*cy, sr*sp*cy-cr*sy, -(cr*sp*cy+sr*sy)],
                     [cp*sy, sr*sp*sy+cr*cy, cy*sr-cr*sp*sy],
                     [sp, -sr*cp, cr*cp]])


def scalar(block, key, default=1):
    m = re.search(r'^\s*' + key + r'=([^\r\n]+)', block, re.M)
    return float(m[1]) if m else default


brushes, meshes = [], []
for block in re.findall(r'Begin Actor\b.*?End Actor', data['geometryT3d'], re.S):
    name = re.search(r'Name=(\w+)', block)[1]
    loc, pre = vector(block, 'Location'), vector(block, 'PrePivot')
    rot = rotation(vector(block, 'Rotation', axes=('Pitch', 'Yaw', 'Roll')))
    if 'Begin Brush' in block:
        main = re.search(r'MainScale=\(Scale=\(([^)]+)', block)
        post = re.search(r'PostScale=\(Scale=\(([^)]+)', block)
        def scale(m):
            vals = dict(re.findall(r'(\w+)=([^,]+)', m[1])) if m else {}
            return np.array([float(vals.get(k, 1)) for k in 'XYZ'])
        polys = []
        for poly in re.findall(r'Begin Polygon.*?End Polygon', block, re.S):
            vs = np.array([[float(x) for x in m.split(',')] for m in re.findall(r'Vertex\s+([^\r\n]+)', poly)])
            if len(vs):
                vs = ((vs - pre) * scale(main)) @ rot.T * scale(post) + loc
                polys.append(vs)
        brushes.append({'name': name, 'polys': polys, 'block': block,
                        'subtract': 'CSG_Subtract' in block})
    m = re.search(r"^\s*StaticMesh=StaticMesh'([^']+)'", block, re.M)
    if m and m[1] in assets:
        box = assets[m[1]]['bounds']
        points = np.array(list(itertools.product(*zip(box[:3], box[3:]))))
        points = ((points - pre) * vector(block, 'DrawScale3D', (1, 1, 1)) * scalar(block, 'DrawScale')) @ rot.T + loc
        meshes.append({'name': name, 'asset': m[1], 'lo': points.min(0), 'hi': points.max(0)})


def draw(path, additions=()):
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (1500, 1150), '#18212b'); d = ImageDraw.Draw(im)
    def xy(v): return ((v[0]+3300)*.26, 1040-(v[1]+2200)*.26)
    for b in brushes:
        for poly in b['polys']:
            if poly[:, 2].max() < -135 or poly[:, 2].min() > 300: continue
            if np.ptp(poly[:, 2]) < .1 and -135 < poly[0,2] < -40:
                d.polygon([xy(v) for v in poly], fill='#253741', outline='#354950')
    for b in brushes:
        for p in b['polys']:
            if p[:,2].min() > 0 or p[:,2].max() < 0: continue
            points=[]
            for a,z in zip(p, np.roll(p,-1,axis=0)):
                if (a[2]<0<z[2]) or (z[2]<0<a[2]): points.append(a+(z-a)*(-a[2]/(z[2]-a[2])))
            if len(points)==2:d.line([xy(v) for v in points], fill='#8b9aa6',width=2)
    for m in meshes:
        if m['hi'][2]<-130 or m['lo'][2]>300:continue
        a,b=xy(m['lo']),xy(m['hi']);box=(min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1]))
        d.rectangle(box, outline='#547cad')
        if m['asset'].endswith(('ForkLift','IND03_Palette','machine1','machine2','echaGrue_b','echaGrue_c')):d.text(a,m['name'].replace('StaticMeshActor','M'),fill='#adc3de')
    for a in data['actors']:
        if a.get('position') and -150<a['position'][2]<300 and any(t in a['actor']['class'] for t in ['ObjectiveTrigger','Interrupteur','PlayerStart']):
            x,y=xy(a['position']);d.ellipse((x-4,y-4,x+4,y+4),outline='#e5666a',width=2)
    for m in additions:
        a,b=xy(m['lo']),xy(m['hi']);d.rectangle((min(a[0],b[0]),min(a[1],b[1]),max(a[0],b[0]),max(a[1],b[1])),outline='#edd675',width=2);d.text(a,m['name'],fill='#edd675')
    for x in range(-3000,2501,500):d.text(xy((x,-2050)),str(x),fill='white')
    for y in range(-2000,2001,500):d.text(xy((-3150,y)),str(y),fill='white')
    d.text((25,20),'ShipD overground: walls / existing mesh bounds (blue) / access and objectives (red)',fill='white')
    im.save(path)


if __name__ == '__main__':
    import sys
    draw(sys.argv[1])

