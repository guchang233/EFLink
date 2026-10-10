"""Control a bounded native gameplay observer; never print captured payloads."""
import argparse
import json
from pathlib import Path
from rpc import request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('start', 'status', 'stop', 'drain'))
    parser.add_argument('--module', default='eflink.transport2')
    parser.add_argument('--index', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.action == 'drain' and not args.output:
        parser.error('drain requires a local --output path')
    result = request({'action': args.action}, args.module, args.index)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    body = result.get('body', {})
    print(json.dumps({key: body[key] for key in ('observing', 'receive_calls', 'send_calls', 'unclassified', 'captured_messages', 'captured_base64_bytes',
        'capture_failed', 'capture_dropped', 'authority_established', 'error') if key in body}))
    if result.get('result') != 0:
        raise RuntimeError('Observer request failed')


if __name__ == '__main__':
    main()
