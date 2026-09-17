"""Generate the working-dock scenery patch from the supplied ShipD export."""
import copy
import itertools
import json
import math
import os
import numpy as np
import shipd_scenery_geometry as geo

data = geo.data
classes = {c['class']: c['schema'] for c in data['classes']}
patch = copy.deepcopy(data['changes'])
patch['description'] = 'Working dock at night: perimeter cargo, maintenance details, blackout-linked work lights and a small overhead steam plume.'
patch['operations'] = ops = []
placed = []
V = lambda x=0, y=0, z=0: dict(zip('XYZ', map(str, (x, y, z))))
R = lambda pitch=0, yaw=0, roll=0: dict(zip(('Pitch', 'Yaw', 'Roll'), map(str, (pitch, yaw, roll))))


def create(name, cls, props):
    assert not any(a['values'].get('Tag') == name or a['actor']['path'].endswith('.' + name) for a in data['actors'])
    ops.append({'op': 'create', 'id': name, 'class': cls, 'properties': props})


def mesh(name, asset, x, y, floor, scale=1, yaw=0, dimensions=None, pitch=0):
    bounds = np.array(geo.assets[asset]['bounds'])
    centre = (bounds[:3] + bounds[3:]) / 2
    scales = np.array(dimensions) / (bounds[3:] - bounds[:3]) if dimensions else np.array([scale]*3)
    points = (np.array(list(itertools.product(*zip(bounds[:3], bounds[3:])))) - centre) * scales
    points = points @ geo.rotation(np.array([pitch, yaw, 0])).T
    loc = np.array([x, y, floor - points[:, 2].min() + .15])
    lo, hi = points.min(0) + loc, points.max(0) + loc
    create(name, 'Engine.StaticMeshActor', {
        'Location': V(*loc), 'Rotation': R(pitch, yaw), 'PrePivot': V(*centre),
        'StaticMesh': f"StaticMesh'{asset}'", 'DrawType': 'DT_StaticMesh',
        'DrawScale3D': V(*scales), 'bHidden': 'False', 'bCollideActors': 'True',
        'bBlockActors': 'True', 'bBlockPlayers': 'True', 'bBlockCamera': 'True',
        'bBlockZeroExtentTraces': 'True', 'bBlockNonZeroExtentTraces': 'True',
        'bStaticLighting': 'False', 'bApplyToStaticLighting': 'False',
        'bUseDynamicLights': 'True', 'bUnlit': 'False', 'AmbientGlow': '8'})
    placed.append({'name': name, 'asset': asset, 'lo': lo, 'hi': hi})
    return hi[2]


pallet = 'Terminus_STM.spy_spawn.IND03_Palette'
crate = 'FAC_STM.fac_woodbox'
toolbox = 'Terminus_STM.hangar.Ind03_caisseOutilFerme'
for i, (x, y) in enumerate(((1810, 1740), (1940, 1740)), 1):
    top = mesh(f'Dock_LoadPallet{i}', pallet, x, y, -127, .5)
    mesh(f'Dock_LoadCrate{i}', crate, x, y, top, .5)
mesh('Dock_LoadTools', toolbox, 1680, 1800, -127, .8)
mesh('Dock_LoadCone', 'Prison_STC.common.conesecu', 2040, 1535, -127, .12, yaw=16384)

mesh('Dock_MaintTools', toolbox, 1750, -1430, -1, .8)
mesh('Dock_MaintCaution', 'Terminus_STM.hangar.Ind03_PannoCaution', 1780, -1260, -125, .35)
mesh('Dock_MaintPipe', 'GAR_STM.STM.GAR_pipe_chauferie', 1630, -1455, 61, dimensions=(18, 10, 140), pitch=16384)

for i, y in enumerate((360, 470), 1):
    mesh(f'Dock_QuayCrate{i}', crate, -2600, y, -129, .65)
for i, y in enumerate((40, 125, 210), 1):
    mesh(f'Dock_QuayBarrel{i}', 'Terminus_STM.spy_spawn.IND03_Abandon_Baril', -2620, y, -129, .7)

for i, x in enumerate((-1500, -1330), 1):
    top = mesh(f'Dock_StorePallet{i}', pallet, x, -1540, -127, .5)
    if i == 1:
        mesh('Dock_StoreCrate', crate, x, -1540, top, .5)
mesh('Dock_StoreBin', 'Aquarium_STM.Ext.aqua_ext_poub01', -1150, -1590, -127, .7)

for label, x, y, floor in [('Load', 1660, 1690, -127), ('Quay', -2580, 575, -129)]:
    top = mesh(f'Dock_{label}Lamp', 'STM_ORP.Eclairage.ORP_lampemerc', x, y, floor, .8)
    create(f'Dock_{label}Light', 'SBase.STriggerLight', {
        'Location': V(x-30, y-25, top-15), 'LightHue': '25', 'LightSaturation': '140',
        'LightBrightness': '40', 'LightRadius': '12', 'LightType': 'LT_Steady',
        'TriggerMethode': 'TriggerToggle', 'bInitialyOn': 'True', 'bDynamicLight': 'True',
        'bStaticLighting': 'False', 'bApplyToStaticLighting': 'False',
        'bApplyToInGameLighting': 'True', 'bMovable': 'True', 'bHidden': 'True'})

# The blackout uses explicit ObjectLights references, not just the light Tag.
manager = next(a for a in data['actors'] if a['values'].get('Tag') == 'OvergroundLightManager' and a['values']['ObjectLights'])
ops.append({'op': 'update', 'actor': manager['actor'], 'before': copy.deepcopy(manager['values']),
            'properties': {'ObjectLights': manager['values']['ObjectLights'] +
                           [{'$ref': 'Dock_LoadLight'}, {'$ref': 'Dock_QuayLight'}]}})

create('Dock_MaintSteam', 'Engine.Emitter', {
    'Location': V(1559, -1455, 70), 'AutoDestroy': 'False', 'AutoReset': 'False',
    'bHidden': 'False', 'bCollideActors': 'False', 'bBlockActors': 'False',
    'bBlockPlayers': 'False', 'bStaticLighting': 'False'})
ran = lambda lo, hi: {'Min': str(lo), 'Max': str(hi)}
vr = lambda x, y, z: dict(zip('XYZ', (ran(*x), ran(*y), ran(*z))))
ops.append({'op': 'component', 'id': 'Dock_SteamParticles', 'owner': 'Dock_MaintSteam',
            'class': 'Engine.SpriteEmitter', 'properties': {
                'Texture': "Texture'sfx.Emitter.fumee_diffuse'", 'DrawStyle': 'PTDS_AlphaBlend',
                'CoordinateSystem': 'PTCS_Independent', 'Disabled': 'False',
                'AutoDestroy': 'False', 'RespawnDeadParticles': 'True',
                'AutomaticInitialSpawning': 'False', 'SpawnAmount': '8',
                'ParticlesPerSecond': '4', 'InitialParticlesPerSecond': '4',
                'LifetimeRange': ran(1.0, 1.3), 'StartSizeRange': vr((4, 7), (4, 7), (4, 7)),
                'UniformSize': 'True', 'StartLocationRange': vr((-1, 1), (-1, 1), (0, 1)),
                'StartVelocityRange': vr((-2, 1), (1, 3), (14, 20)),
                'UseCollision': 'False', 'UseActorForces': 'False', 'UseColorScale': 'True',
                'ColorScale': [{'RelativeTime': '0', 'Color': {'R': '170', 'G': '180', 'B': '185', 'A': '45'}},
                               {'RelativeTime': '1', 'Color': {'R': '170', 'G': '180', 'B': '185', 'A': '0'}}],
                'FadeIn': 'True', 'FadeInEndTime': '.2', 'FadeInFactor': {'W': '1'},
                'FadeOut': 'True', 'FadeOutStartTime': '.7', 'FadeOutFactor': {'W': '1'},
                'TextureUSubdivisions': '1', 'TextureVSubdivisions': '1', 'ZTest': 'True', 'ZWrite': 'False'}})


def validate(schema, value):
    kind = schema['kind']
    if kind in ('ArrayProperty', 'FixedArray'):
        assert isinstance(value, list)
        if kind == 'FixedArray': assert len(value) == schema['dimension']
        for item in value: validate(schema['inner'], item)
    elif kind == 'StructProperty':
        assert set(value) == set(schema['fields'])
        for key, item in value.items(): validate(schema['fields'][key], item)
    elif isinstance(value, dict):
        assert kind == 'ObjectProperty' and set(value) == {'$ref'}
        assert value['$ref'] in {o.get('id') for o in ops}
    else:
        assert isinstance(value, str)
        if schema.get('choices'): assert value in schema['choices'], (value, schema['choices'])


for op in ops:
    cls = op.get('class', op.get('actor', {}).get('class'))
    for k, v in op['properties'].items():
        assert k in classes[cls], (cls, k)
        validate(classes[cls][k], v)

# Check no new shared positions or overlaps between the new solid meshes.
positions = [a['position'] for a in data['actors'] if a.get('position')]
for op in ops:
    if op['op'] == 'create':
        pos = [float(op['properties']['Location'][k]) for k in 'XYZ']
        assert all(math.dist(pos, p) > 1 for p in positions)
        positions.append(pos)
for a, b in itertools.combinations(placed, 2):
    assert not np.all(np.minimum(a['hi'], b['hi']) - np.maximum(a['lo'], b['lo']) > .01), (a['name'], b['name'])

out = geo.SOURCE.with_name('ShipD-working-dock-scenery.changes.json')
out.write_text(json.dumps(patch, indent=2) + '\n')
manifest = [{k: v.tolist() if isinstance(v, np.ndarray) else v for k, v in a.items()} for a in placed]
out.with_suffix('.bounds.json').write_text(json.dumps(manifest, indent=2) + '\n')
geo.draw(os.path.join(os.environ['TEMP'], 'shipd-dock-proposed.png'), placed)
print(out)
print('PASS: schemas, blackout references, unique positions and non-overlapping new mesh bounds')
print('operations', len(ops), 'meshes', len(placed))
for a in placed:
    hits = [b['name'] for b in geo.meshes if np.all(np.minimum(a['hi'], b['hi']) - np.maximum(a['lo'], b['lo']) > 1)]
    if hits: print('existing bounds intersection', a['name'], hits)
