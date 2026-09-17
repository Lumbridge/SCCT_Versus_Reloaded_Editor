"""Repair the already-imported flood override from a current map export."""
import copy
import json
import sys
from pathlib import Path

source = Path(sys.argv[1])
snapshot = json.loads(source.read_text())
actors = {a['values'].get('Tag'): a for a in snapshot['actors']}
patch = copy.deepcopy(snapshot['changes'])
patch['description'] = ('Repair flood water return: latch cancellation until the water can '
                        'reverse, prevent duplicate returns, and rearm only after water is home.')
patch['operations'] = ops = []


def action(tag='None', delay=0, kind='EVT_Trigger', valid='EVT_Trigger'):
    return {'Event': tag, 'Delay': str(delay), 'Type': kind, 'ValidOn': valid}


def magic(tag, actions, sequence=False):
    assert tag not in actors, f'{tag} already exists; export and revise the repair instead.'
    ops.append({'op': 'create', 'id': tag, 'class': 'SBase.SMagicEvent',
                'properties': {'Location': {'X': '-2200', 'Y': '-1523', 'Z': '-1400'},
                               'Groups': [{'EventGroup': actions, 'Sequence': str(sequence),
                                           'Repeat': '0', 'SequenceIndex': '0'}]}})


def update(tag, properties):
    actor = actors[tag]
    ops.append({'op': 'update', 'actor': actor['actor'],
                'before': copy.deepcopy(actor['values']), 'properties': properties})


# Each of these two latches contributes once per physical water cycle.
# Waiting for both a return request and completion of the rise avoids Mover's
# IsTriggerable rejection while KeyNum != PrevKeyNum. The 0.05-second dispatch
# also lets FinishedOpening return before starting the downward interpolation.
magic('FO_WaterReturnRequest', [action('FO_WaterDrain'), action(valid='EVT_Untrigger')], True)
magic('FO_WaterAtTop', [action('FO_WaterDrain'), action(valid='EVT_Untrigger')], True)
magic('FO_WaterDrain', [action(), action('FloodWater', .05)], True)

# An early override may finish its cooldown before the water is down. Require
# BOTH water home and the cooldown before accepting another flood activation.
magic('FO_FloodRearm', [action(), action('FloodEventHackPanel', .05)], True)
magic('FO_WaterHome', [action('FO_WaterReturnRequest', .01, 'EVT_Untrigger'),
                       action('FO_WaterAtTop', .02, 'EVT_Untrigger'),
                       action('FO_FloodRearm', .03)])

water = actors['FloodWater']['values']
assert water['OpenedEvent'] == 'None' and water['ClosedEvent'] == 'None'
assert water['NumKeys'] == '2' and water['KeyNum'] == '0'
update('FloodWater', {'InitialState': 'TriggerToggle', 'OpenedEvent': 'FO_WaterAtTop',
                      'ClosedEvent': 'FO_WaterHome'})
for tag in ('FloodEvent', 'FO_Cleanup'):
    groups = copy.deepcopy(actors[tag]['values']['Groups'])
    returns = 0
    for group in groups:
        for entry in group['EventGroup']:
            if entry['Event'] == 'FloodWater' and (tag == 'FO_Cleanup' or float(entry['Delay']) > 0):
                entry['Event'] = 'FO_WaterReturnRequest'
                entry['Type'] = 'EVT_Trigger'
                returns += 1
            if tag == 'FO_Cleanup' and entry['Event'] == 'FloodEventHackPanel':
                entry['Event'] = 'FO_FloodRearm'
    assert returns == 1, (tag, returns)
    update(tag, {'Groups': groups})


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


classes = {c['class']: c['schema'] for c in snapshot['classes']}
by_path = {a['actor']['path']: a for a in snapshot['actors']}
for op in ops:
    schema = classes[op['class']] if op['op'] == 'create' else by_path[op['actor']['path']]['schema']
    for key, value in op['properties'].items():
        assert key in schema, key
        validate(schema[key], value)

out = source.with_name('ShipD-flood-water-return-fix.changes.json')
out.write_text(json.dumps(patch, indent=2) + '\n')
print(out)
print('PASS: schema validation; 5 create operations and 3 updates')
