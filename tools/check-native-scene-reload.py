"""One real client's same-level reload and permission diagnostic; no multiplayer acceptance claim."""
import argparse
import json
import math
import os
from pathlib import Path
import time
from rpc import request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    index = Path(os.environ['LOCALAPPDATA']) / 'EFML/modules/index.json'

    def call(body):
        reply = request(body, args.module, index, http_timeout=12)
        if reply.get('result') != 0:
            raise RuntimeError('native scene diagnostic RPC failed')
        return reply['body']

    def state():
        return call({'action': 'world_state'})

    original = state()
    world = original['world']
    if not original['session']['entry']['active'] or original['native']['players']:
        raise RuntimeError('requires an active local host without other peers')
    home = world['position']
    destination = [home[0] + 4, home[1], home[2]]
    result = {'diagnostic_only': True, 'real_clients_verified': False, 'module': args.module,
              'home': home, 'destination': destination, 'legs': []}

    def travel(position, label):
        initial = state()
        epoch = initial['session']['epoch']
        actor = next(p['entity'] for p in initial['world']['players'] if p['peer'] == 1)
        call({'action': 'native_travel_probe', 'position': position, 'command_id': label})
        samples = []
        deadline = time.monotonic() + 65
        while time.monotonic() < deadline:
            current = state()
            session, native, current_world = current['session'], current['native'], current['world']
            samples.append({'epoch': session['epoch'], 'ready': session['ready'],
                            'active': session['entry']['active'], 'phase': session['entry']['phase'],
                            'local_scene_ready': session['entry']['local_scene_ready'], 'teleport': native['teleport']})
            if native['teleport'] == 'failed':
                raise RuntimeError('native travel failed: ' + native['error'])
            if (session['epoch'] > epoch and session['entry']['active'] and current_world['map'] == world['map']
                    and current_world['level'] == world['level'] and math.dist(current_world['position'], position) < 2):
                new_actor = next(p['entity'] for p in current_world['players'] if p['peer'] == 1)
                suspended = any(not s['active'] and not s['local_scene_ready'] for s in samples)
                return {'label': label, 'before_epoch': epoch, 'after_epoch': session['epoch'],
                        'actor_changed': new_actor != actor, 'loading_permission_withdrawn': suspended,
                        'position': current_world['position'], 'samples': samples,
                        'passed': suspended and new_actor != actor}
            time.sleep(.15)
        raise RuntimeError('native same-level reload did not produce a new initialized epoch')

    try:
        print('Checking same-level native reload permissions', flush=True)
        result['legs'].append(travel(destination, 'scene-reload-outward'))
    except Exception as error:
        result['error'] = str(error)
    finally:
        try:
            print('Restoring the original level position', flush=True)
            result['legs'].append(travel(home, 'scene-reload-homeward'))
        except Exception as error:
            result['restore_error'] = str(error)
    result['passed'] = len(result['legs']) == 2 and all(leg['passed'] for leg in result['legs']) and 'error' not in result and 'restore_error' not in result
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'legs'} | {
        'legs': [{k: v for k, v in leg.items() if k != 'samples'} for leg in result['legs']]}, ensure_ascii=False), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
