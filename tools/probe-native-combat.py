"""Exercise an owned host actor's real native cast and observe NPC retaliation.

Development builds only. This never controls a window or submits synthetic damage.
Passing this milestone does not certify normal client input or room admission.
"""
import argparse
import json
import time
from pathlib import Path
from rpc import request

p = argparse.ArgumentParser()
p.add_argument('--module', required=True)
p.add_argument('--host', choices=['primary', 'isolated'], default='isolated')
p.add_argument('--peer', type=int, required=True)
p.add_argument('--key', required=True)
p.add_argument('--seconds', type=int, default=15, choices=range(1, 31))
p.add_argument('--require-retaliation', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
index = (root / 'test-environments/dual-instance/profiles/guest/AppData/Local/BetterEndfield/third-party/index.json') if a.host == 'isolated' else None

def sample():
    state = request({'action': 'status'}, a.module, index)['body']
    if state['role'] != 'host':
        raise RuntimeError('Selected instance must own the room')
    native = state['game']['world_replication']
    actor = next((x for x in native['players'] if x['peer'] == a.peer), None)
    if actor is None:
        raise RuntimeError('Selected peer has no native room actor in this scene')
    if not (actor['started'] and actor['registered'] and actor['bound']) or actor['error']:
        raise RuntimeError('Owned room actor is not ready')
    enemy = next((x for x in native['enemy_targets'] if x['key'] == a.key), None)
    if enemy is None:
        raise RuntimeError('Selected room NPC is absent or has unloaded; choose a current scene key')
    return {'native': native, 'actor': actor, 'enemy': enemy}

baseline = sample()
scale = baseline['actor'].get('movement_diagnostics', {}).get('tick_group', {}).get('time_scale')
if scale is not None and scale <= 0:
    raise RuntimeError('Native actor time is paused; combat observation would be inconclusive')
identity = baseline['actor']['entity']
old_hits = {x['at'] for x in baseline['native']['native_hits']}
records = [baseline]
command = request({'action': 'native_skill_probe', 'peer': a.peer,
                   'key': a.key, 'move_owned': True}, a.module, index)['body']
time.sleep(.8)
until = time.monotonic() + a.seconds
while time.monotonic() < until:
    record = sample()
    if record['actor']['entity'] != identity:
        raise RuntimeError('Actor allocation changed during combat observation')
    records.append(record)
    diag = record['enemy'].get('ai_diagnostics', {})
    print(json.dumps({'hp': record['actor']['hp'], 'ai_target': diag.get('target_actor'),
                      'mode': diag.get('mode_name'), 'casting': diag.get('casting'),
                      'behaviours': diag.get('behaviours'), 'error': diag.get('error')},
                     ensure_ascii=False), flush=True)
    time.sleep(1)

new_hits = {x['at']: x for r in records[1:] for x in r['native']['native_hits']
            if x['at'] not in old_hits}
outgoing = [x for x in new_hits.values() if not x['player_target']
            and x['source_actor'] == identity and x['key'] == a.key
            and x['hp_after'] < x['hp_before']]
incoming = [x for x in new_hits.values() if x['player_target']
            and x['target_actor'] == identity and x['hp_after'] < x['hp_before']]
result = {'module': a.module, 'host': a.host, 'key': a.key, 'actor': identity,
          'command': command, 'native_outgoing_hits': outgoing,
          'native_incoming_hits': incoming, 'records': records,
          'native_pause_observed': any(r['actor'].get('movement_diagnostics', {}).get('tick_group', {}).get('time_scale', 1) <= 0 for r in records),
          'complete_room_admission_verified': False}
destination = root / 'reports' / (a.module + '-native-combat-' + a.key.rsplit(':', 1)[-1] + '-' + str(time.time_ns()) + '.json')
destination.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(f'Native outgoing hits: {len(outgoing)}; native incoming hits: {len(incoming)}')
print(destination)
print('Complete multiplayer admission remains unverified.')
if result['native_pause_observed'] or not outgoing or (a.require_retaliation and not incoming):
    raise SystemExit(1)
