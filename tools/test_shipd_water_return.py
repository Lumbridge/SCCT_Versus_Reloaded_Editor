"""Exercise the generated event graph with Mover's busy-command rejection."""
import copy
import heapq
import itertools
import json
import sys
from pathlib import Path

snapshot = json.loads(Path(sys.argv[1]).read_text())
patch = json.loads(Path(sys.argv[2]).read_text())
base = {a['values'].get('Tag'): a['values'] for a in snapshot['actors']}
paths = {a['actor']['path']: a['values']['Tag'] for a in snapshot['actors']}
for op in patch['operations']:
    tag = op['id'] if op['op'] == 'create' else paths[op['actor']['path']]
    base[tag] = {**base.get(tag, {}), **op['properties']}
duration = float(base['FloodWater']['MoveTime'])

for cancellation in (0, .1, 5, 19.99, 20, 20.01, 34.99, 35, 35.01, 39.99, None):
    state = copy.deepcopy(base)
    now = 0
    physical = 'home'
    for cycle in range(3):
        queue = []
        serial = itertools.count()
        rearmed = []
        home_at = []
        closing = []
        rejected = []

        def schedule(when, tag, kind='EVT_Trigger'):
            heapq.heappush(queue, (when, next(serial), tag, kind))

        start = now
        stop = 40 if cancellation is None else cancellation
        schedule(start, 'FloodWater')
        # A completed override cancels remaining timed FloodEvent actions.
        if cancellation is None or cancellation >= 35:
            schedule(start + 35, 'FO_WaterReturnRequest')
        schedule(start + stop, 'FO_Cleanup')
        # Duplicate requests must never become a second downward/upward toggle.
        schedule(start + stop + .2, 'FO_WaterReturnRequest')
        schedule(start + stop + .3, 'FO_WaterReturnRequest')
        while queue:
            now, _, tag, kind = heapq.heappop(queue)
            if tag == 'FloodWater':
                if physical in ('rising', 'falling'):
                    rejected.append(now)
                    continue
                assert kind == 'EVT_Trigger'
                if physical == 'home':
                    physical = 'rising'
                    schedule(now + duration, '@top')
                else:
                    physical = 'falling'
                    closing.append(now)
                    schedule(now + duration, '@home')
            elif tag == '@top':
                physical = 'top'
                schedule(now, state['FloodWater']['OpenedEvent'])
            elif tag == '@home':
                physical = 'home'
                home_at.append(now)
                schedule(now, state['FloodWater']['ClosedEvent'])
            elif tag == 'FloodEventHackPanel':
                assert physical == 'home', 'Rearmed during water movement'
                rearmed.append(now)
            elif tag in state and 'Groups' in state[tag]:
                for group in state[tag]['Groups']:
                    entries = group['EventGroup']
                    if not entries or int(group['Repeat']) < 0:
                        continue
                    sequential = group['Sequence'].lower() == 'true'
                    chosen = [entries[int(group['SequenceIndex'])]] if sequential else entries
                    for entry in chosen:
                        if entry['ValidOn'] not in (kind, 'EVT_TriggerUntrigger', 'EVT_UntriggerTrigger'):
                            continue
                        if sequential:
                            group['SequenceIndex'] = str((int(group['SequenceIndex']) + 1) % len(entries))
                        # Run the water/rearm part of cleanup; other existing
                        # cleanup effects and the flood-stop relay are external.
                        if tag == 'FO_Cleanup' and entry['Event'] not in ('FO_WaterReturnRequest', 'FO_FloodRearm'):
                            continue
                        schedule(now + float(entry['Delay']), entry['Event'], entry['Type'])
        assert physical == 'home' and len(closing) == len(home_at) == len(rearmed) == 1
        assert not rejected, rejected
        assert rearmed[0] >= max(home_at[0], start + stop + 20)
        for tag in ('FO_WaterAtTop', 'FO_WaterReturnRequest', 'FO_WaterDrain', 'FO_FloodRearm'):
            assert state[tag]['Groups'][0]['SequenceIndex'] == '0', tag
        now += 1
print('PASS: 11 cancellation/timeout timings x 3 consecutive cycles; duplicate requests, busy rejection, return and rearm')
