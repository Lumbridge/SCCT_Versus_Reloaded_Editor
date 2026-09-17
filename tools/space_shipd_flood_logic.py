"""Separate flood logic actors without editing their event connections."""
import copy
import json
import math
import sys
from pathlib import Path

source = Path(sys.argv[1])
snapshot = json.loads(source.read_text())
actors = snapshot['actors']
targets = sorted((a for a in actors if a['actor']['class'] == 'SBase.SMagicEvent'
                  and a['values'].get('Tag', '').startswith('FO_')),
                 key=lambda a: a['values']['Tag'])
assert targets, 'No flood logic actors in this export.'
target_paths = {a['actor']['path'] for a in targets}


def position(values):
    return tuple(float(values['Location'][axis]) for axis in 'XYZ')


occupied = [position(a['values']) for a in actors
            if a['actor']['path'] not in target_paths and 'Location' in a['values']]
patch = copy.deepcopy(snapshot['changes'])
patch['description'] = 'Arrange flood logic actors in a spaced grid to clear same-location warnings.'
patch['operations'] = []
slot = 0
for actor in targets:
    assert 'Location' in actor['schema']
    while True:
        candidate = (-2328 + (slot % 5) * 64, -1619 + (slot // 5) * 64, -1400)
        slot += 1
        assert slot < 100, 'No clear grid positions nearby.'
        if all(math.dist(candidate, other) >= 32 for other in occupied):
            break
    occupied.append(candidate)
    patch['operations'].append({'op': 'update', 'actor': actor['actor'],
                                'before': copy.deepcopy(actor['values']),
                                'properties': {'Location': dict(zip('XYZ', map(str, candidate)))}})

# Verify the effective snapshot: only positions differ, and every moved actor
# has clearance from all other exported actors, including other logic actors.
effective = copy.deepcopy(actors)
changes = {o['actor']['path']: o['properties'] for o in patch['operations']}
for actor in effective:
    actor['values'].update(changes.get(actor['actor']['path'], {}))
for before, after in zip(actors, effective):
    if before['actor']['path'] in changes:
        assert {k: v for k, v in before['values'].items() if k != 'Location'} == {
            k: v for k, v in after['values'].items() if k != 'Location'}
        assert all(math.dist(position(after['values']), position(other['values'])) >= 32
                   for other in effective if other is not after and 'Location' in other['values'])

out = source.with_name('ShipD-flood-logic-spacing.changes.json')
out.write_text(json.dumps(patch, indent=2) + '\n')
print(out)
print(f'PASS: {len(targets)} position-only updates, at least 32 units from every other exported actor.')
