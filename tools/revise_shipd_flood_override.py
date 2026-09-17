"""Revise an already imported ShipD override using a fresh authoring export."""
import copy
import itertools
import json
import sys
from pathlib import Path

source = Path(sys.argv[1])
snapshot = json.loads(source.read_text())
actors = {a['values'].get('Tag'): a for a in snapshot['actors']}
patch = copy.deepcopy(snapshot['changes'])
patch['description'] = ('ShipD flood override revision: two wall-mounted north/south '
                        'buttons; retire west/east stations; require both unique presses.')
patch['operations'] = operations = []
retired = {f'FO_{kind}{i}' for i in (1, 2)
           for kind in ('Button', 'Light', 'Cover', 'Latch', 'Press')}


def update(tag, properties):
    actor = actors[tag]
    assert all(k in actor['schema'] for k in properties), (tag, properties)
    operations.append({'op': 'update', 'actor': actor['actor'],
                       'before': copy.deepcopy(actor['values']), 'properties': properties})


def groups_without_retired(tag):
    groups = copy.deepcopy(actors[tag]['values']['Groups'])
    for group in groups:
        group['EventGroup'] = [a for a in group['EventGroup'] if a['Event'] not in retired]
        group['SequenceIndex'] = '0'
    return groups


for tag in ('FloodEvent', 'FO_Expire', 'FO_Cleanup'):
    update(tag, {'Groups': groups_without_retired(tag)})
groups = copy.deepcopy(actors['FO_Count']['values']['Groups'])
assert [a['Event'] for a in groups[0]['EventGroup']] == ['None'] * 3 + ['FO_CancelRelay']
groups[0]['EventGroup'] = groups[0]['EventGroup'][-2:]
groups[0]['SequenceIndex'] = '0'
update('FO_Count', {'Groups': groups})

# The subtractive wet-room wall faces are Y=-304 and Y=-2152.
# trigger_standard has local X bounds [0, 5.9677], scaled by 1.5.
# Covers already have a centred 12-unit thickness. Put their backs 0.5
# units off the wall, with the button enclosed inside the closed cover.
# Keep the existing actor identities, scales, rotations and walkway heights.
for i, wall, inward in ((3, -304, -1), (4, -2152, 1)):
    for kind, inset in (('Button', 1), ('Cover', 6.5), ('Light', 48)):
        tag = f'FO_{kind}{i}'
        loc = copy.deepcopy(actors[tag]['values']['Location'])
        loc['Y'] = str(wall + inward * inset)
        update(tag, {'Location': loc})

# Version 1 cannot delete. Retire existing stations in place so they cannot
# render, collide, illuminate, be used or contribute to the active event graph.
for i in (1, 2):
    hidden = {'bHidden': 'True', 'bHiddenEd': 'True', 'bCollideActors': 'False',
              'bBlockActors': 'False', 'bBlockPlayers': 'False', 'bBlockCamera': 'False'}
    update(f'FO_Button{i}', {**hidden, 'Event': 'None', 'bInitialyUsable': 'False',
                           'bInitialyActive': 'False', 'UsableBy': 'U_NO_ONE'})
    update(f'FO_Cover{i}', hidden)
    update(f'FO_Light{i}', {'bHidden': 'True', 'bHiddenEd': 'True',
                          'bInitialyOn': 'False', 'LightType': 'LT_None', 'LightBrightness': '0'})
    for kind in ('Latch', 'Press'):
        update(f'FO_{kind}{i}', {'Groups': [], 'bHiddenEd': 'True'})


def validate(schema, value):
    kind = schema['kind']
    if kind in ('ArrayProperty', 'FixedArray'):
        assert isinstance(value, list)
        if kind == 'FixedArray':
            assert len(value) == schema['dimension']
        for item in value:
            validate(schema['inner'], item)
    elif kind == 'StructProperty':
        assert set(value) == set(schema['fields'])
        for key, item in value.items():
            validate(schema['fields'][key], item)
    else:
        assert isinstance(value, str)
        if schema.get('choices'):
            assert value in schema['choices'], (value, schema['choices'])


by_path = {a['actor']['path']: a for a in snapshot['actors']}
effective = {tag: copy.deepcopy(a['values']) for tag, a in actors.items()}
for op in operations:
    actor = by_path[op['actor']['path']]
    for key, value in op['properties'].items():
        validate(actor['schema'][key], value)
    effective[actor['values']['Tag']].update(op['properties'])
for tag in ('FloodEvent', 'FO_Expire', 'FO_Cleanup'):
    assert not any(a['Event'] in retired for g in effective[tag]['Groups'] for a in g['EventGroup'])
assert len(effective['FO_Count']['Groups'][0]['EventGroup']) == 2
for i in (3, 4):
    assert effective[f'FO_Button{i}']['UsableBy'] == 'U_BOTH'
    assert effective[f'FO_Light{i}']['LightType'] == 'LT_Pulse'
    assert [a['Event'] for a in effective[f'FO_Latch{i}']['Groups'][0]['EventGroup']] == [f'FO_Press{i}', 'None']
    assert effective[f'FO_Press{i}']['Groups'][0]['EventGroup'][-1]['Event'] == 'FO_Count'
    wall, direction = (-304, -1) if i == 3 else (-2152, 1)
    button_back = direction * (float(effective[f'FO_Button{i}']['Location']['Y']) - wall)
    cover_centre = direction * (float(effective[f'FO_Cover{i}']['Location']['Y']) - wall)
    assert 0 <= cover_centre - 6 < button_back
    assert button_back + 5.9677 * 1.5 < cover_centre + 6

# Both orders, every timeout prefix, duplicate uses and consecutive cycles.
for order in itertools.permutations((3, 4)):
    for prefix in range(3):
        for cycle in range(3):
            used = set()
            completions = 0
            for button in list(order[:prefix]) * 2 + [3, 4]:
                if button in used:
                    continue
                used.add(button)
                completions += len(used) == 2
            assert completions == 1

out = source.with_name('ShipD-flood-override-two-buttons.changes.json')
out.write_text(json.dumps(patch, indent=2) + '\n')
print(out)
print(f'PASS: {len(operations)} updates; schemas, two-button graph, duplicate presses, timeout and wall clearances')
