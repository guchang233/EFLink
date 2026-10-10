"""Swap only EF Link test modules, then restore the current public two-instance room.

Uses local authenticated mod RPC and immutable DLL IDs. No computer/UI automation.
Existing room passwords are never guessed. Auth tokens are neither logged nor copied.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.error
from rpc import request

root = Path(__file__).resolve().parents[1]
dual = root / 'test-environments/dual-instance'
primary = Path(os.environ['LOCALAPPDATA']) / 'BetterEndfield/third-party/index.json'
guest = dual / 'profiles/guest/AppData/Local/BetterEndfield/third-party/index.json'
ownership = Path(r'D:\MC x ENDFIELD\.tools\install-manifest.json')
indexes = [(dual / 'framework-guest/third-party/index.json', None), (guest, None),
           (Path(r'D:\MC x ENDFIELD\.tools\framework\third-party\index.json'), ownership), (primary, None)]

def command(tool, *args):
    subprocess.run([sys.executable, str(root / 'tools' / tool), *map(str, args)], cwd=root, check=True)

def module_status(module, index):
    result = request({'action': 'status'}, module, index)
    if result.get('result') != 0:
        raise RuntimeError('Module status unavailable')
    return result['body']

def wait_status(module, index, predicate, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = module_status(module, index)
            if predicate(result):
                return result
        except urllib.error.HTTPError as error:
            # The watched index reloads asynchronously. /send briefly returns
            # 400 while a newly registered module is not loaded yet.
            if error.code != 400:
                raise
        except (RuntimeError, TimeoutError):
            pass
        time.sleep(.3)
    raise RuntimeError('Module did not reach the requested state')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--old', required=True)
    p.add_argument('--new', required=True)
    p.add_argument('--registered', action='store_true', help='Re-enable an identical package after a failed asynchronous load')
    args = p.parse_args()
    before = [(primary, module_status(args.old, primary)), (guest, module_status(args.old, guest))]
    (root / 'reports' / (args.new + '-room-before-swap.json')).write_text(
        json.dumps({str(index): state for index, state in before}, ensure_ascii=False, indent=2), encoding='utf-8')
    hosts = [(index, state) for index, state in before if state['role'] == 'host']
    clients = [(index, state) for index, state in before if state['role'] == 'client']
    if len(hosts) != 1 or len(clients) != 1 or hosts[0][1]['room']['id'] != clients[0][1]['room']['id']:
        raise RuntimeError('Preserve room: expected one connected host and one client')
    host_index, host_state = hosts[0]
    client_index, client_state = clients[0]
    room = host_state['room']
    if room['password']:
        raise RuntimeError('Preserve private room: password is unavailable for automatic restoration')
    # Validate every index and package ID before disabling the running build.
    old_registered = set()
    for index, record in indexes:
        config = json.loads(index.read_text(encoding='utf-8-sig'))
        existing = [m for m in config['modules'] if m['id'] == args.new]
        if config.get('schema') != 1 or (bool(existing) != args.registered) or len(existing) > 1:
            raise RuntimeError('Use a fresh immutable development ID, or explicitly resume a registered package')
        if existing:
            expected = root / 'build/dev' / args.new
            if Path(existing[0]['directory']).resolve() != expected.resolve() or (expected / f'native/windows-x64/{args.new}.dll').read_bytes() != (root / 'build' / (args.new + '.dll')).read_bytes():
                raise RuntimeError('Registered package changed; preserve the loaded DLL')
        old_count = sum(m['id'] == args.old for m in config['modules'])
        # The primary AppData file can serve only as RPC credentials while the
        # framework index owns the loaded module. Do not fabricate an old entry.
        if old_count == 1:
            old_registered.add(index)
        elif old_count != 0 or index != primary:
            raise RuntimeError('Old module is not registered exactly once')
        if record:
            import hashlib
            entries = json.loads(record.read_text(encoding='utf-8'))['files']
            tracked = next((f for f in entries if Path(f['path']).resolve() == index.resolve()), None)
            if tracked and tracked['sha256'] != hashlib.sha256(index.read_bytes()).hexdigest():
                raise RuntimeError('Installer ownership changed; preserve the running modules')
    library = root / 'build' / (args.new + '.dll')
    if not library.is_file():
        raise RuntimeError('Build the DLL first')
    for index, record in indexes:
        if index not in old_registered:
            continue
        command('set-enabled.py', args.old, 'off', '--index', index,
                *(['--ownership-record', record] if record else []))
    time.sleep(2)
    try:
        for number, (index, record) in enumerate(indexes):
            if args.registered:
                command('set-enabled.py', args.new, 'on', '--index', index,
                        *(['--ownership-record', record] if record else []))
            else:
                command('register-dev.py', '--id', args.new, '--dll', library, '--index', index,
                        *(['--reuse'] if number else []), *(['--ownership-record', record] if record else []))
        for index, state in before:
            wait_status(args.new, index, lambda s: s['hook_ready'])
            request({'action': 'settings', 'configuration': state['configuration']}, args.new, index)
        request({'action': 'host', 'room_name': room['name'], 'port': room['port'],
                 'max_players': room['max_players'], 'password': ''}, args.new, host_index)
        wait_status(args.new, host_index, lambda s: s['role'] == 'host')
        request({'action': 'join', 'address': f'127.0.0.1:{room["port"]}', 'password': ''}, args.new, client_index)
        wait_status(args.new, client_index, lambda s: s['role'] == 'client' and len(s['peers']) == 2)
        wait_status(args.new, host_index, lambda s: len(s['peers']) == 2)
        for index, state in before:
            if state.get('gui_visible'):
                request({'action': 'open_gui'}, args.new, index)
        print('New build loaded; original host/client roles, names, port and public room restored.')
    except BaseException:
        # Never leave two EF Link input/damage hooks active after a failed swap.
        for index, record in indexes:
            config = json.loads(index.read_text(encoding='utf-8-sig'))
            if any(m['id'] == args.new for m in config['modules']):
                command('set-enabled.py', args.new, 'off', '--index', index,
                        *(['--ownership-record', record] if record else []))
        # Pinned IDs cannot be reinitialized in every Host version. Preserve old
        # registrations for a normal restart instead of silently spawning processes.
        for index, record in indexes:
            if index not in old_registered:
                continue
            command('set-enabled.py', args.old, 'on', '--index', index,
                    *(['--ownership-record', record] if record else []))
        raise
