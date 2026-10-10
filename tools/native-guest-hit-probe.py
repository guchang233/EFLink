"""Exercise native guest attacks and always return to the starting position.

Requires two already admitted development clients in the overworld. Uses only
local authenticated mod RPC, with game work queued to the native game thread.
No protocol hit fabrication or computer/input automation.
"""
import argparse
import json
from pathlib import Path
import time
from rpc import request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', required=True)
    parser.add_argument('--guest-index', type=Path, required=True)
    parser.add_argument('--host-index', type=Path)
    parser.add_argument('--automatic', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()

    def world(index):
        reply = request({'action': 'world_state'}, args.module, index)
        if reply.get('result') != 0:
            raise RuntimeError('World RPC failed')
        return reply['body']

    def distance(a, b):
        return sum((x-y)**2 for x, y in zip(a, b))**.5

    host = world(args.host_index)
    initial = world(args.guest_index)
    if not host['session']['entry']['authority']['scene_rules']:
        raise RuntimeError('Host must be in the overworld')
    policy = initial['native']['guest_policy']
    if not initial['session']['entry']['active'] or not policy['ready'] or not policy['restricted']:
        raise RuntimeError('Guarded admitted guest required')
    status = request({'action': 'status'}, args.module, args.guest_index)['body']
    peer = status['local_id']

    def position(state):
        return next(p['native_state']['position'] for p in state['world']['players'] if p['peer'] == peer)

    origin = position(initial)
    enemy = min((e for e in initial['world']['entities'] if e['alive'] and e['hp'] > 10000),
                key=lambda e: distance(origin, e['position']))
    target = list(enemy['position']); target[0] += 1.5
    report = {'module': args.module, 'automatic': args.automatic, 'key': enemy['key'],
              'initial_host_hp': enemy['hp'], 'baseline': initial['native']['combat'], 'records': []}

    def travel(destination):
        request({'action': 'native_travel_probe', 'position': destination}, args.module, args.guest_index)
        deadline = time.monotonic()+25
        while time.monotonic() < deadline:
            state = world(args.guest_index)
            if state['native']['teleport'] == 'failed':
                raise RuntimeError(state['native']['error'])
            if state['native']['teleport'] == 'complete' and distance(position(state), destination) < 3:
                return state
            time.sleep(.4)
        raise RuntimeError('Native same-level travel timed out')

    moved = False
    try:
        request({'action': 'native_owner_probe', 'peer': 1, 'key': enemy['key']}, args.module, args.host_index)
        moved = True
        nearby = travel(target)
        if not nearby['session']['entry']['active']:
            raise RuntimeError('Guest lost admission')
        for _ in range(2):
            request({'action': 'native_self_cast_probe', 'automatic': args.automatic}, args.module, args.guest_index)
            for _ in range(5):
                time.sleep(.45)
                guest = world(args.guest_index); host = world(args.host_index)
                report['records'].append({'guest_combat': guest['native']['combat'],
                    'host_combat': host['native']['combat'], 'guest_error': guest['native']['error'],
                    'diagnostics': guest['native'].get('diagnostics', [])[-2:],
                    'host_hp': next((e['hp'] for e in host['world']['entities'] if e['key'] == enemy['key']), None),
                    'guest_policy': guest['native']['guest_policy']})
    finally:
        try:
            if moved:
                report['returned'] = distance(position(travel(origin)), origin) < 3
        finally:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    last = report['records'][-1]
    accepted = any(d.get('result', {}).get('accepted') for d in last['diagnostics']
                   if d.get('action') == 'native_self_cast_probe')
    sent = last['guest_combat']['guest_hits_sent'] - report['baseline']['guest_hits_sent']
    print(json.dumps({'native_cast_accepted': accepted, 'new_native_guest_hits': sent,
                     'host_hp_before': report['initial_host_hp'], 'host_hp_after': last['host_hp'],
                     'returned': report.get('returned', False)}))
    if not accepted or sent <= 0 or not report.get('returned'):
        raise SystemExit('Native attack probe did not pass; inspect its report')


if __name__ == '__main__':
    main()
