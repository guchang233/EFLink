"""Native equipment lifecycle diagnostic. An auxiliary peer is not a real-client acceptance test."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from rpc import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--address', default='127.0.0.1:47711')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    index = Path(os.environ['LOCALAPPDATA']) / 'EFML/modules/index.json'

    def status():
        reply = request({'action': 'status'}, args.module, index)
        if reply.get('result') != 0:
            raise RuntimeError('development module status failed')
        return reply['body']

    initial = status()
    if initial['role'] != 'host' or initial['game']['world_replication']['players']:
        raise RuntimeError('requires an already running host room without other peers')
    appearance = initial['game']['appearance']['local']
    weapon = appearance['weapon_template']
    if not weapon:
        raise RuntimeError('requires a natively sampled equipped weapon')
    child = subprocess.Popen([sys.executable, str(Path(__file__).with_name('link-mirror.py')),
                              '--address', args.address, '--cooperation-metadata', '--weapon-sequence',
                              '0:0', f'8:{weapon}', '16:0', '--weapon-state', '1', '--duration', '25',
                              '--name', 'Auxiliary equipment lifecycle'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, encoding='utf-8', env=dict(os.environ, PYTHONIOENCODING='utf-8'))
    proofs = {}
    started = time.monotonic()
    try:
        while child.poll() is None and time.monotonic() - started < 29:
            body = status()
            elapsed = time.monotonic() - started
            stage = min(int(elapsed // 8), 2)
            expected = weapon if stage == 1 else 0
            for player in body['game']['world_replication']['players']:
                applied = next((p for p in body['world_session']['appearances'] if p['peer'] == player['peer']), {})
                if ((player.get('appearance') or {}).get('weapon_template') == expected
                        and player.get('weapon_loaded') == bool(expected) and not player.get('weapon_error')
                        and applied.get('applied') and body['game']['appearance']['local'] == appearance):
                    proofs.setdefault(stage, {key: player[key] for key in
                                              ('entity', 'peer', 'appearance', 'weapon_loaded', 'weapon_error', 'weapon_reloads', 'weapon_state')})
            time.sleep(0.35)
        if child.poll() is None:
            raise RuntimeError('auxiliary peer did not finish')
        output, _ = child.communicate(timeout=3)
        # Give the game thread its normal cleanup ticks after the transport leave.
        deadline = time.monotonic() + 4
        final = status()
        while final['game']['world_replication']['players'] and time.monotonic() < deadline:
            time.sleep(0.2)
            final = status()
        same_actor = len(proofs) == 3 and len({p['entity'] for p in proofs.values()}) == 1
        reloaded = len(proofs) == 3 and all(proofs[i]['weapon_reloads'] < proofs[i + 1]['weapon_reloads'] for i in (0, 1))
        cleaned = not final['game']['world_replication']['players']
        result = {'diagnostic_only': True, 'real_clients_verified': False, 'module': args.module,
                  'protocol': initial['world_session']['protocol'], 'proofs': proofs,
                  'same_actor': same_actor, 'reloads_advanced': reloaded, 'native_players_cleared': cleaned,
                  'peer_exit_code': child.returncode, 'passed': same_actor and reloaded and cleaned and child.returncode == 0,
                  'peer_output': output}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps({k: v for k, v in result.items() if k not in ('protocol', 'peer_output')}, ensure_ascii=False))
        return 0 if result['passed'] else 1
    finally:
        if child.poll() is None:
            child.terminate()
            child.communicate(timeout=3)


if __name__ == '__main__':
    raise SystemExit(main())
