"""Guest-only development RPC for game-thread world-feed commands.

Captures stay in ignored reports. Do not use this tool on the main account.
No authentication tokens or protobuf payloads are printed.
"""
import argparse
import json
import time
import uuid
from pathlib import Path

from rpc import request

ROOT = Path(__file__).resolve().parents[1]
GUEST_INDEX = ROOT / 'test-environments/dual-instance/framework-guest/third-party/index.json'
QUEUED = {'inject', 'enemies', 'entity', 'node', 'attack', 'goto', 'net', 'callers', 'code', 'metadata', 'scene'}


def command(module, body):
    body = dict(body)
    serial = uuid.uuid4().hex
    body['command_id'] = serial
    response = request(body, module, GUEST_INDEX)
    if response.get('result') != 0:
        raise RuntimeError(response.get('body', {}).get('error', 'RPC failed'))
    if body.get('action') not in QUEUED:
        return response['body']
    until = time.monotonic() + 30
    while time.monotonic() < until:
        state = request({'action': 'status'}, module, GUEST_INDEX)['body']
        for record in state.get('results', []):
            if record.get('command_id') == serial:
                if 'error' in record:
                    raise RuntimeError(record['error'])
                return record['result']
        time.sleep(.25)
    raise TimeoutError('No game-thread result; check that the guest game is running')


def brief(value):
    if isinstance(value, dict):
        return {k: ('[local payload]' if k in {'protobuf_base64', 'body_base64', 'code_hex'} else brief(v))
                for k, v in value.items()}
    if isinstance(value, list):
        return [brief(v) for v in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', required=True)
    parser.add_argument('--body', type=json.loads, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = command(args.module, args.body)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(brief(result), ensure_ascii=False))


if __name__ == '__main__':
    main()
