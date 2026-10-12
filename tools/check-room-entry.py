"""Controlled local two-client join/leave crash reproduction.

Requires two already logged-in clients, an open host room, and explicit player
authorization for testing. Sends only join/leave/status to their local EFML RPC.
Does not log in, cast skills, award items, or assert full cooperation acceptance.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

from rpc import request


def snapshot(body):
    game = body.get('game', {})
    native = game.get('world_replication', {})
    entry = body.get('world_session', {}).get('entry', {})
    return {
        'utc': datetime.now(timezone.utc).isoformat(), 'role': body.get('role'),
        'room': body.get('room', {}).get('id'), 'local_valid': game.get('local_valid'),
        'map': game.get('map_id'), 'level': game.get('level_id'),
        'local_model': game.get('local_model'),
        'phase': entry.get('phase'), 'active': entry.get('active'),
        'blockers': entry.get('blockers'), 'scene_ready': entry.get('local_scene_ready'),
        'protocol_compatible': body.get('world_session', {}).get('protocol', {}).get('compatible'),
        'peers': len(body.get('peers', [])), 'travel': native.get('teleport'),
        'avatars': len(game.get('avatars', [])),
        'players': [{key: actor.get(key) for key in
                     ('peer', 'entity', 'started', 'bound', 'weapon_loaded', 'error', 'weapon_error')}
                    for actor in native.get('players', [])],
        'native_operations': native.get('native_operations'),
        'native_load_parameter_retained': native.get('native_load_parameter_retained'),
        'native_error': native.get('error'), 'game_error': game.get('error'),
        'restoration_pending': native.get('native_feed', {}).get('restoration_pending'),
        'tracked_casts': game.get('player_casts', {}).get('tracked'),
        'hit_bindings': game.get('hit_provenance', {}).get('bindings'),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--host-index', required=True, type=Path)
    parser.add_argument('--guest-index', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--hold', type=float, default=30)
    parser.add_argument('--already-joined', action='store_true', help='Observe the existing first-round connection before leaving')
    args = parser.parse_args()
    if not 1 <= args.rounds <= 15 or not 10 <= args.hold <= 120:
        parser.error('rounds must be 1..15; hold must be 10..120 seconds')

    def call(index, action):
        reply = request(action, args.module, index, http_timeout=12)
        if reply.get('result') != 0:
            raise RuntimeError('Module rejected the requested diagnostic action')
        return reply['body']

    def status(index):
        return call(index, {'action': 'status'})

    host, guest = status(args.host_index), status(args.guest_index)
    expected_guest='client' if args.already_joined else 'offline'
    if host.get('role') != 'host' or guest.get('role') != expected_guest:
        raise RuntimeError('Requires an existing host room and a disconnected guest')
    if args.already_joined and host.get('room', {}).get('id') != guest.get('room', {}).get('id'):
        raise RuntimeError('Existing guest connection belongs to a different room')
    # A fresh disconnected guest intentionally does not sample its player.
    # Manual world entry must be confirmed beforehand; validate its native
    # character after connecting rather than rejecting its empty offline cache.
    if not host.get('game', {}).get('local_valid'):
        raise RuntimeError('The host must already be in its game world')
    protocols = [b.get('world_session', {}).get('protocol', {}) for b in (host, guest)]
    if any(not protocols[0].get(field) or protocols[0][field] != protocols[1].get(field)
           for field in ('build', 'game_version', 'contract_revision')):
        raise RuntimeError('Both clients must use the same build, game version, and contract')
    password = host.get('configuration', {}).get('room_password', '')
    result = {'diagnostic_only': True, 'full_cooperation_verified': False,
              'module': args.module, 'before': {'host': snapshot(host), 'guest': snapshot(guest)},
              'rounds': [], 'passed': False, 'personal_world_position_verified': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def save():
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')

    save()
    try:
        for number in range(1, args.rounds + 1):
            round_result = {'number': number, 'samples': [], 'entered': False, 'cleanup_complete': False,
                            'actors_present_during_loading': False}
            result['rounds'].append(round_result)
            if args.already_joined and number == 1:
                print('Observing already joined local room, round', number, flush=True)
            else:
                print('Joining local room, round', number, flush=True)
                call(args.guest_index, {'action': 'join',
                                       'address': '127.0.0.1:' + str(host['room']['port']),
                                       'password': password})
            deadline = time.monotonic() + 90
            entered_at = None
            while time.monotonic() < deadline:
                h, g = snapshot(status(args.host_index)), snapshot(status(args.guest_index))
                round_result['samples'].append({'host': h, 'guest': g})
                if g['travel'] in ('loading', 'loading_requested', 'starting_world', 'waiting_world_start') and g['players']:
                    round_result['actors_present_during_loading'] = True
                save()
                entered = (g['role'] == 'client' and g['protocol_compatible'] and g['local_valid']
                           and g['scene_ready'] and g['travel'] in ('idle', 'complete')
                           and any(p['started'] for p in g['players'])
                           and any(p['started'] for p in h['players']))
                if entered and entered_at is None:
                    entered_at = time.monotonic()
                if not entered:
                    entered_at = None
                if entered_at and time.monotonic() - entered_at >= args.hold:
                    round_result['entered'] = True
                    break
                time.sleep(1)
            print('Leaving local room, round', number, flush=True)
            call(args.guest_index, {'action': 'leave'})
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                g = snapshot(status(args.guest_index))
                round_result['after_leave'] = g
                save()
                if (g['role'] == 'offline' and g['travel'] in ('idle', 'complete')
                        and not g['restoration_pending'] and not g['players'] and not g['avatars']
                        and not g['tracked_casts'] and not g['hit_bindings']
                        and not g['native_load_parameter_retained']):
                    round_result['cleanup_complete'] = True
                    break
                time.sleep(1)
            round_result['passed'] = (round_result['entered'] and round_result['cleanup_complete']
                                      and not round_result['actors_present_during_loading'])
            save()
            if not round_result['passed']:
                raise RuntimeError('Native entry or personal-world restoration did not complete')
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
        try:
            call(args.guest_index, {'action': 'leave'})
        except Exception:
            result['guest_leave_unconfirmed'] = True
    finally:
        save()
    print('Entry-only reproduction passed:', result['passed'], flush=True)
    print('Evidence:', args.output.resolve(), flush=True)
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
