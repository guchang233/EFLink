"""Record public hook diagnostics while the player operates the game manually.

Only sends action=status to the local EFML module. Does not open a room, cast,
travel, switch modules, or print the local RPC authentication token.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import time

from rpc import request


def pick(value, fields):
    return {key: value[key] for key in fields if key in value}


def public_sample(reply):
    if reply.get('result') != 0:
        raise RuntimeError('Module status request failed')
    body = reply['body']
    game = body.get('game', {})
    native = game.get('world_replication', {})
    session = body.get('world_session', {})
    return {
        'utc': datetime.now(timezone.utc).isoformat(),
        **pick(body, ('module', 'role', 'local_id', 'player_cast_observer_ready',
                      'damage_hook_ready', 'transferred_hook_ready', 'hook_error')),
        'room_id': body.get('room', {}).get('id'),
        'local_model': game.get('local_model'),
        'protocol': pick(session.get('protocol', {}),
                         ('build', 'game_version', 'world_schema', 'contract_revision')),
        'epoch': session.get('epoch'),
        'snapshot_ready': session.get('ready'),
        'entry': pick(session.get('entry', {}),
                      ('active', 'phase', 'local_scene_ready', 'authority', 'blockers')),
        'player_casts': pick(game.get('player_casts', {}),
                             ('events_sent', 'tracked', 'last', 'replay_enabled')),
        'hit_provenance': pick(game.get('hit_provenance', {}),
                               ('matched', 'missing', 'bindings', 'last', 'error', 'observation_only')),
        'combat': pick(native.get('combat', {}),
                       ('settlement_policy', 'native_damage_verified', 'legacy_numeric_damage',
                        'damage_hook_calls', 'hit_claims_pending', 'room_hits_applied')),
        'cleanup': {
            'native_entities': native.get('native_entities'),
            'players': len(native.get('players', [])),
            'avatars': len(game.get('avatars', [])),
            **pick(native.get('native_feed', {}),
                   ('restoration_pending', 'withheld_pending', 'error')),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--seconds', type=float, default=90)
    parser.add_argument('--interval', type=float, default=1)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--index', type=Path,
                        default=Path(os.environ.get('LOCALAPPDATA', '.')) / 'EFML/modules/index.json')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600 or not .5 <= args.interval <= 30:
        parser.error('seconds must be 1..3600; interval must be .5..30')
    result = {'diagnostic_only': True, 'manual_game_operations': True,
              'real_clients_verified': False, 'module': args.module, 'samples': []}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.seconds
    try:
        while time.monotonic() < deadline:
            try:
                sample = public_sample(request({'action': 'status'}, args.module,
                                               args.index, http_timeout=12))
                if sample.get('module') != args.module:
                    raise RuntimeError('Reply module identity differs from requested module')
            except Exception as error:
                sample = {'utc': datetime.now(timezone.utc).isoformat(), 'capture_error': str(error)}
            result['samples'].append(sample)
            args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            time.sleep(max(0, min(args.interval, deadline - time.monotonic())))
    except KeyboardInterrupt:
        pass
    print(f'Saved {len(result["samples"])} read-only samples: {args.output.resolve()}')


if __name__ == '__main__':
    main()
