"""Check native component/container evidence in both running game instances.

This is a read-only milestone check. It does not certify combat, scene isolation,
dungeon rules, or complete multiplayer admission.
"""
import argparse
import json
from pathlib import Path
from rpc import request

p = argparse.ArgumentParser()
p.add_argument('--module', required=True)
p.add_argument('--require-growth', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
guest_index = root / 'test-environments/dual-instance/profiles/guest/AppData/Local/BetterEndfield/third-party/index.json'
evidence = {}
for label, index in [('primary', None), ('isolated', guest_index)]:
    state = request({'action': 'status'}, a.module, index)['body']
    players = state['game']['world_replication']['players']
    assert state['hook_ready'], f'{label}: native tick hook unavailable'
    assert len(players) == 1, f'{label}: expected exactly one remote actor'
    actor = players[0]
    assert actor['registered'] and actor['inited'] and actor['started'], f'{label}: actor not started/registered'
    assert actor['skills'] > 0 and actor['max_hp'] > 0 and not actor['error'], f'{label}: actor combat components unavailable'
    avatar = next(x for x in state['game']['avatars'] if x['id'] == actor['peer'])
    assert avatar['spawned'] and avatar['native_entity'] and not avatar['error'], f'{label}: visible model does not belong to native actor'
    if state['role'] == 'host':
        assert actor['bound'], f'{label}: room identity not bound to native actor'
    if a.require_growth:
        world = request({'action': 'world_state'}, a.module, index)['body']['world']
        confirmed = next(x for x in world['players'] if x['peer'] == actor['peer'])
        assert actor['growth'] and actor['growth'] == confirmed['growth'], f'{label}: actor differs from host-confirmed growth'
        assert actor['max_hp'] > 0, f'{label}: no native attributes after reconstruction'
    evidence[label] = {'role': state['role'], 'actor': actor, 'avatar': avatar,
                       'entry': state['world_session']['entry']}
    print(f'{label}: native actor started, container-registered, {actor["skills"]} skills, model owned by entity', flush=True)
    if a.require_growth:
        print(f'{label}: native growth matches host confirmation; native max HP {actor["max_hp"]}', flush=True)
dest = root / 'reports' / (a.module + '-native-actor-check.json')
dest.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
print('Milestone verified; complete host-world admission remains unverified.')
