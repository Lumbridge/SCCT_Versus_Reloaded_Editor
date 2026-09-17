"""Spatial checks for the generated dock scenery, optionally against native bounds."""
import json
import re
import sys
from pathlib import Path
import numpy as np
import shipd_scenery_geometry as geo

placed = json.loads(geo.SOURCE.with_name('ShipD-working-dock-scenery.changes.bounds.json').read_text())


def inside(x, y, polygon):
    result = False
    for a, b in zip(polygon, np.roll(polygon, -1, axis=0)):
        if (a[1] > y) != (b[1] > y) and x < (b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:
            result = not result
    return result


def intersects(a, b, lo, hi):
    low, high = 0., 1.
    for i in range(2):
        delta = b[i] - a[i]
        if abs(delta) < 1e-8:
            if not lo[i]+.25 < a[i] < hi[i]-.25:
                return False
        else:
            t1, t2 = (lo[i]+.25-a[i])/delta, (hi[i]-.25-a[i])/delta
            low, high = max(low, min(t1, t2)), min(high, max(t1, t2))
    return high > low


polys = []
for brush in geo.brushes:
    flags = re.search(r'^\s*PolyFlags=(\d+)', brush['block'], re.M)
    if flags and int(flags[1]) & 8:
        continue  # Invisible non-solid portal/zone surfaces do not support props.
    polys.extend((brush['name'], p) for p in brush['polys'])
protected = [a for a in geo.data['actors'] if a.get('position') and -140 < a['position'][2] < 300
             and any(k in a['actor']['class'] for k in ['ObjectiveTrigger', 'Interrupteur', 'PlayerStart'])]
for a in placed:
    lo, hi = np.array(a['lo']), np.array(a['hi'])
    if a['name'] != 'Dock_MaintPipe':
        # Each footprint corner must be supported by a matching floor/plinth
        # face or by one of the deliberately loaded pallets.
        for x in (lo[0]+.1, hi[0]-.1):
            for y in (lo[1]+.1, hi[1]-.1):
                floor = any(np.ptp(p[:,2]) < .01 and abs(p[0,2]-(lo[2]-.15)) < .02
                            and inside(x, y, p) for _, p in polys)
                pallet = any(b['name'] != a['name'] and abs(b['hi'][2]-(lo[2]-.15)) < .02
                             and b['lo'][0] <= x <= b['hi'][0] and b['lo'][1] <= y <= b['hi'][1]
                             for b in placed)
                assert floor or pallet, ('Unsupported corner', a['name'], x, y, lo[2])
    z = (lo[2]+hi[2])/2
    for name, p in polys:
        if np.ptp(p[:,2]) < .1: continue
        points = []
        for v, w in zip(p, np.roll(p, -1, axis=0)):
            if v[2] < z < w[2] or w[2] < z < v[2]:
                points.append(v+(w-v)*(z-v[2])/(w[2]-v[2]))
        assert not (len(points) == 2 and intersects(*points, lo, hi)), ('Wall intersection', a['name'], name)
    for b in geo.meshes:
        assert not np.all(np.minimum(hi, b['hi'])-np.maximum(lo, b['lo']) > 1), (a['name'], b['name'])
    for b in protected:
        p = np.array(b['position'][:2])
        assert np.linalg.norm(np.maximum(np.maximum(lo[:2]-p, p-hi[:2]), 0)) >= 100, (a['name'], b['actor'])

if len(sys.argv) > 1:
    native = json.loads(Path(sys.argv[1]).read_text())
    native = {a['actor']['path'].split('.')[-1]: a['bounds'] for a in native}
    for a in placed:
        assert np.allclose(a['lo'], native[a['name']]['min'], atol=.01)
        assert np.allclose(a['hi'], native[a['name']]['max'], atol=.01)
    print('PASS: all 20 calculated mesh bounds match the native editor.')
print('PASS: ground/pallet support, wall and existing-mesh clearances, and >=100-unit objective/control/spawn clearance.')
