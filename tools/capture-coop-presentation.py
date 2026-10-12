"""Read-only two-client presentation/performance evidence. No login or combat input."""
import argparse
import json
from pathlib import Path
import statistics
import time
from rpc import request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--module', required=True)
parser.add_argument('--seconds', type=int, default=30)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
if not 1 <= args.seconds <= 120:
    parser.error('seconds must be 1..120')
root = Path(__file__).resolve().parents[1]
indexes = [root / f'test-environments/light-help/temp/AppSandbox_Endfield/_{i}/AppData/Local/EFML/modules/index.json' for i in (0, 1)]
samples = []
until = time.monotonic() + args.seconds
while time.monotonic() < until:
    row = []
    for index in indexes:
        started = time.monotonic()
        body = request({'action': 'status'}, args.module, index)['body']
        game = body.get('game', {})
        native = game.get('world_replication', {})
        row.append({'role': body.get('role'), 'rpc_ms': (time.monotonic()-started)*1000,
                    'presentation': game.get('presentation'), 'avatars': game.get('avatars'),
                    'actors': native.get('players'), 'perf': native.get('perf'),
                    'session_age_ms': body.get('world_session', {}).get('age_ms'),
                    'presentation_transport': body.get('world_session', {}).get('presentation'),
                    'peers': [{'id': p.get('id'), 'rtt_ms': p.get('rtt_ms'), 'state_age_ms': p.get('state_age_ms')} for p in body.get('peers', [])],
                    'scene_ready': body.get('world_session', {}).get('entry', {}).get('local_scene_ready')})
    samples.append(row)
    time.sleep(.5)
args.output.parent.mkdir(parents=True, exist_ok=True)
result = {'module': args.module, 'full_sync_verified': False, 'samples': samples}
args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
for slot in (0, 1):
    rows = [r[slot] for r in samples]
    values = [r['perf']['frame_ms'] for r in rows if r['perf']]
    print(slot, 'samples', len(rows), 'frame median/max ms', statistics.median(values) if values else None, max(values) if values else None,
          'actors last', len(rows[-1]['actors'] or []), 'error', (rows[-1]['presentation'] or {}).get('error'))
print('Saved evidence:', args.output)
