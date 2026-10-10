"""Guest-only native verification of pre-sequence teardown suppression.

Exercises the real EF Link feed and operator filter on the game thread. This is
one-client lifecycle evidence, not shared-world or two-client acceptance.
"""
import argparse
import importlib.util
import json
import time
import uuid
from pathlib import Path

from rpc import request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('world_feed', ROOT / 'tools/world-feed.py')
feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(feed)


def diagnostic(module, body):
    body = dict(body, action='native_feed_lifecycle_probe', command_id=uuid.uuid4().hex)
    response = request(body, module, feed.GUEST_INDEX)
    if response.get('result') != 0:
        raise RuntimeError(response.get('body', {}).get('error', 'diagnostic RPC rejected'))
    until = time.monotonic() + 30
    while time.monotonic() < until:
        state = request({'action': 'status'}, module, feed.GUEST_INDEX)['body']
        records = state.get('game', {}).get('world_replication', {}).get('diagnostics', [])
        for record in records:
            if record.get('command_id') == body['command_id']:
                if 'error' in record:
                    raise RuntimeError(record['error'])
                return record['result']
        time.sleep(.25)
    raise TimeoutError('Guest game-thread diagnostic has not completed')


def run(core, observer, identifier, track_enter, report):
    def observe(body):
        return feed.command(observer, body)

    def status():
        return request({'action': 'status'}, core, feed.GUEST_INDEX)['body']

    state = status()
    if state['role'] != 'offline' or not state['op_filter_ready']:
        raise RuntimeError('This experiment requires an offline guest with the native op filter ready')
    before = observe({'action': 'enemies'})
    if not before.get('enemies') or not before.get('player'):
        raise RuntimeError('The guest is not in a playable scene')
    if observe({'action': 'node', 'id': identifier})['data']:
        raise RuntimeError('Temporary ID already in use')
    original_ids = {enemy['server_id'] for enemy in before['enemies']}
    source = min(before['enemies'], key=lambda enemy: sum(
        (a-b)**2 for a, b in zip(enemy['position'], before['player'])))
    data = {'multiplayer_acceptance': False, 'id': identifier, 'track_enter': track_enter,
            'before': before, 'filter_before': state['game']['world_replication']['native_feed']}
    entered = False
    try:
        observe({'action': 'traceops', 'enabled': True, 'reset': True, 'seconds': 60})
        diagnostic(core, {'phase': 'enter', 'id': identifier, 'template': source['template'],
                          'position': source['position'], 'track_enter': track_enter})
        entered = True
        until = time.monotonic()+8
        while time.monotonic() < until:
            data['native_spawn'] = observe({'action': 'node', 'id': identifier})
            if data['native_spawn'].get('has_entity'):
                break
            time.sleep(.25)
        # Let all entry operations already created flush before measuring leave.
        time.sleep(1)
        data['entry_origins'] = observe({'action': 'optraces'})
        data['entry_wire'] = observe({'action': 'drain'})
        data['filter_before_leave'] = status()['game']['world_replication']['native_feed']
        observe({'action': 'traceops', 'enabled': True, 'reset': True, 'seconds': 60})
    except Exception as error:
        data['error'] = str(error)
    finally:
        try:
            if entered:
                diagnostic(core, {'phase': 'leave', 'id': identifier})
            time.sleep(2)
            data['filter_after'] = status()['game']['world_replication']['native_feed']
            data['cleanup_origins'] = observe({'action': 'optraces'})
            data['cleanup_wire'] = observe({'action': 'drain'})
            data['cleanup_node'] = observe({'action': 'node', 'id': identifier})
            data['cleanup_entity'] = observe({'action': 'entity', 'id': identifier})
            data['after'] = observe({'action': 'enemies'})
            remaining = {enemy['server_id'] for enemy in data['after']['enemies']}
            data['originals_still_visible'] = original_ids <= remaining
            data['temporary_removed'] = not data['cleanup_node']['data'] and not data['cleanup_entity']['exists']
        except Exception as error:
            data['cleanup_error'] = str(error)
        finally:
            observe({'action': 'traceops', 'enabled': False})
            report.parent.mkdir(parents=True, exist_ok=True)
            report.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    summary = {k: data.get(k) for k in ('error','cleanup_error','temporary_removed','originals_still_visible')}
    summary['entry_packets'] = len(data.get('entry_wire', {}).get('sent', []))
    summary['cleanup_packets'] = len(data.get('cleanup_wire', {}).get('sent', []))
    if 'filter_after' in data and 'filter_before_leave' in data:
        summary['cleanup_ops_suppressed'] = data['filter_after']['ops_suppressed']-data['filter_before_leave']['ops_suppressed']
    print(json.dumps(summary, ensure_ascii=False))
    if data.get('error') or data.get('cleanup_error') or not data.get('temporary_removed') or not data.get('originals_still_visible'):
        raise RuntimeError('Native lifecycle verification failed; inspect the local report before continuing')
    if not data.get('native_spawn', {}).get('has_entity'):
        raise RuntimeError('No real native spawn was observed')
    if summary['cleanup_packets'] != 0 or summary.get('cleanup_ops_suppressed', 0) <= 0:
        raise RuntimeError('Teardown isolation was not established')
    if track_enter and summary['entry_packets'] != 0:
        raise RuntimeError('Host-feed entry operation isolation was not established')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', required=True)
    parser.add_argument('--observer', required=True)
    parser.add_argument('--id', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--track-enter', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.core, args.observer, args.id, args.track_enter, args.output)
