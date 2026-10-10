"""One guest-only native lifecycle experiment; never a multiplayer acceptance test.

Uses a previously unused temporary server ID and the game's native view handlers.
Always removes that ID and verifies the original visible enemy IDs afterwards.
Payloads and call stacks are saved locally under ignored reports only.
"""
import argparse
import importlib.util
import json
import time
from pathlib import Path

import native_pb as pb

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('world_feed', ROOT / 'tools/world-feed.py')
feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(feed)


def run(module, temporary_id, report):
    def command(body):
        return feed.command(module, body)

    original = command({'action': 'enemies'})
    scene = command({'action': 'scene'})
    if not scene['started'] or not original.get('player') or not original.get('enemies'):
        raise RuntimeError('Guest is not in a loaded world')
    if command({'action': 'node', 'id': temporary_id})['data']:
        raise RuntimeError('Temporary ID is already in use; nothing was changed')
    originals = {e['server_id'] for e in original['enemies']}
    # Use an already loaded template at its known terrain location. Do not touch
    # the original entity and do not invent a battle identity for this account.
    source = min(original['enemies'], key=lambda e: sum(
        (a - b) ** 2 for a, b in zip(e['position'], original['player'])))
    common = pb.u(1, temporary_id) + pb.i32(2, 3) + pb.s(3, source['template'])
    common += pb.m(4, b''.join(pb.f(i + 1, v) for i, v in enumerate(source['position'])))
    common += pb.m(5, b'') + pb.i32(7, scene['level_num']) + pb.d(8, 100)
    monster = pb.m(1, common) + pb.i32(3, 1) + pb.m(5, pb.u(3, 1) + pb.m(6, b''))
    enter = pb.m(1, pb.m(2, monster))
    leave = pb.m(1, pb.i32(1, 3) + pb.u(2, temporary_id))
    evidence = {'purpose': 'temporary native lifecycle diagnostics only',
                'multiplayer_acceptance': False, 'id': temporary_id, 'scene': scene,
                'before': original, 'entry': None, 'cleanup': None}
    try:
        command({'action': 'traceops', 'enabled': True, 'reset': True, 'seconds': 60})
        evidence['entry'] = command({'action': 'inject', 'type': 'SC_OBJECT_ENTER_VIEW',
                                     'base64': pb.b64(enter), 'via': 'handler'})
        until = time.monotonic() + 8
        while time.monotonic() < until:
            evidence['temporary_entity'] = command({'action': 'entity', 'id': temporary_id})
            if evidence['temporary_entity']['exists']:
                break
            time.sleep(.25)
        evidence['entry_node'] = command({'action': 'node', 'id': temporary_id})
        evidence['entry_origins'] = command({'action': 'optraces'})
        evidence['entry_owners'] = command({'action': 'ops'})
        evidence['entry_wire'] = command({'action': 'drain'})
        command({'action': 'traceops', 'enabled': True, 'reset': True, 'seconds': 60})
    except Exception as error:
        evidence['error'] = str(error)
    finally:
        try:
            evidence['cleanup'] = command({'action': 'inject', 'type': 'SC_OBJECT_LEAVE_VIEW',
                                           'base64': pb.b64(leave), 'via': 'handler'})
            time.sleep(2)
            evidence['cleanup_origins'] = command({'action': 'optraces'})
            evidence['cleanup_owners'] = command({'action': 'ops'})
            evidence['cleanup_node'] = command({'action': 'node', 'id': temporary_id})
            evidence['cleanup_entity'] = command({'action': 'entity', 'id': temporary_id})
            evidence['after'] = command({'action': 'enemies'})
            remaining = {e['server_id'] for e in evidence['after']['enemies']}
            evidence['originals_still_visible'] = originals <= remaining
            evidence['temporary_removed'] = not evidence['cleanup_node']['data'] and not evidence['cleanup_entity']['exists']
            evidence['callers'] = command({'action': 'callers'})
            evidence['wire'] = command({'action': 'drain'})
        except Exception as error:
            evidence['cleanup_error'] = str(error)
        finally:
            command({'action': 'traceops', 'enabled': False})
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: evidence.get(k) for k in (
        'error', 'cleanup_error', 'temporary_removed', 'originals_still_visible')}, ensure_ascii=False))
    print('entry_origin_groups', len(evidence.get('entry_origins', {}).get('origins', [])))
    print('cleanup_origin_groups', len(evidence.get('cleanup_origins', {}).get('origins', [])))
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--id', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/authority-native-lifecycle.json')
    args = parser.parse_args()
    result = run(args.module, args.id, args.output)
    if not result.get('temporary_removed') or not result.get('originals_still_visible'):
        raise SystemExit('Native cleanup verification failed; inspect the saved report before more experiments')
