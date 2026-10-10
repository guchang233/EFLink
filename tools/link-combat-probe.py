"""EF Link scripted co-op guest for single-game combat checks (development only).

Joins a room like tools/link-mirror.py (copying the host's operator and pose with an
offset), then speaks the hosted-world protocol: it requests admission with
entry_prepare, reports resolved hits ("hit") against one room enemy until the host
reports it dead, and logs enemy casts and enemy hits forwarded to this player.
It never touches the game process; all effects go through the host's EF Link.

    python tools/link-combat-probe.py --address 127.0.0.1:47625 --seconds 60
"""
import argparse
import json
import math
import socket
import struct
import time

MAGIC, PROTOCOL = 0x4B4C4645, 1
T_HELLO, T_WELCOME, T_REJECT, T_INFO, T_LEAVE, T_CHAT, T_DATA, T_BYE, T_KEEPALIVE = range(1, 10)
U_HELLO, U_WELCOME, U_STATE = 3, 4, 5


def fnv(text, bits):
    if bits == 32:
        value, prime, mask = 2166136261, 16777619, 0xFFFFFFFF
    else:
        value, prime, mask = 14695981039346656037, 1099511628211, 0xFFFFFFFFFFFFFFFF
    for byte in text.encode('utf-8'):
        value = ((value ^ byte) * prime) & mask
    return value


WORLD = fnv('eflink.world.v1', 32)


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def take(self, fmt):
        size = struct.calcsize(fmt)
        values = struct.unpack_from('<' + fmt, self.data, self.at)
        self.at += size
        return values if len(values) > 1 else values[0]

    def text(self, limit=255):
        n = self.take('B' if limit <= 255 else 'H')
        value = self.data[self.at:self.at + n].decode('utf-8', 'replace')
        self.at += n
        return value

    def raw(self, n):
        value = self.data[self.at:self.at + n]
        self.at += n
        return value


def text(value, limit=255):
    raw = value.encode('utf-8')[:limit]
    return struct.pack('<B' if limit <= 255 else '<H', len(raw)) + raw


def frame(kind, payload=b''):
    return struct.pack('<IB', len(payload) + 1, kind) + payload


def read_info(r):
    info = {'id': r.take('H'), 'name': r.text(64), 'model': r.text(512), 'map': r.text(64), 'level': r.text(64), 'skeleton': r.take('H')}
    info['bones'] = [r.text(96) for _ in range(r.take('B'))]
    return info


def write_info(info, name):
    out = struct.pack('<H', 0) + text(name, 64) + text(info['model'], 512) + text(info['map'], 64) + text(info['level'], 64)
    out += struct.pack('<HB', info['skeleton'], len(info['bones']))
    return out + b''.join(text(b, 96) for b in info['bones'])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--address', default='127.0.0.1:47615')
    p.add_argument('--name', default='战斗探针')
    p.add_argument('--offset', type=float, nargs=3, default=[1.5, 0.0, 0.0])
    p.add_argument('--seconds', type=float, default=60)
    p.add_argument('--hit-fraction', type=float, default=0.2, help='each reported hit as a fraction of the target max HP')
    p.add_argument('--hit-interval', type=float, default=0.6)
    p.add_argument('--report', help='write a JSON summary here')
    p.add_argument('--near-enemy', action='store_true', help='stand 2 m from the nearest live room enemy (to provoke it)')
    p.add_argument('--no-hits', action='store_true', help='only observe enemy attacks; never report hits')
    p.add_argument('--hit-limit', type=int, default=0, help='stop reporting hits after this many (0 = no limit)')
    p.add_argument('--simulate-owned', action='store_true',
                   help='act as the owner of enemies the host assigns: report their movement (a small circle) and casts')
    p.add_argument('--cast-skill', default='', help='skill ID to report for an owned enemy every 3 s')
    a = p.parse_args()
    host, _, port = a.address.partition(':')
    port = int(port or 47615)
    target = (socket.gethostbyname(host), port)

    tcp = socket.create_connection((host, port), timeout=5)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(('0.0.0.0', 0))
    udp.settimeout(0.02)
    tcp.sendall(frame(T_HELLO, struct.pack('<IH', MAGIC, PROTOCOL) + text('probe', 32) + text(a.name, 64) + struct.pack('<QH', 0, udp.getsockname()[1])))
    tcp.settimeout(0.02)

    st = {'id': 0, 'room': 0, 'token': 0, 'udp_ok': False}
    world = {'epoch': 0, 'revision': 0, 'world': None}
    summary = {'owned': {}, 'states_sent': 0, 'casts_reported': 0, 'admitted_at': None, 'hits_sent': 0, 'enemy_casts': [], 'player_hits': [], 'kills': [], 'blockers': None, 'events': []}
    buf, seq, last = b'', 0, {'keep': 0, 'hello': 0, 'entry': 0, 'hit': 0}
    hit_seq, victim, start = 0, None, time.time()
    entry_token = 0xEF1234

    def log(msg, **kw):
        line = {'t': round(time.time() - start, 2), 'msg': msg, **kw}
        summary['events'].append(line)
        print(json.dumps(line, ensure_ascii=False), flush=True)

    def send_world(body, to=1):
        body = dict(body, schema=2, room=st['room'])
        raw = json.dumps(body).encode('utf-8')
        tcp.sendall(frame(T_DATA, struct.pack('<HHII', st['id'], to, WORLD, len(raw)) + raw))

    def udp_header(kind):
        return struct.pack('<IHBBI', MAGIC, PROTOCOL, kind, 0, st['room'])

    def my_entity():
        for player in (world['world'] or {}).get('players', []):
            if player.get('peer') == st['id']:
                return player.get('entity')
        return None

    while time.time() - start < a.seconds:
        now = time.time()
        try:
            chunk = tcp.recv(1 << 20)
            if not chunk:
                log('host closed the connection')
                break
            buf += chunk
        except socket.timeout:
            pass
        while len(buf) >= 5:
            length, kind = struct.unpack_from('<IB', buf)
            if len(buf) < 4 + length:
                break
            r = Reader(buf[5:4 + length])
            buf = buf[4 + length:]
            if kind == T_WELCOME:
                st['id'], st['room'], st['token'] = r.take('H'), r.take('I'), r.take('I')
                log('joined', player=st['id'])
            elif kind == T_REJECT:
                log('rejected', reason=r.text(128))
                return
            elif kind == T_INFO:
                info = read_info(r)
                if info['id'] == 1:
                    tcp.sendall(frame(T_INFO, write_info(info, a.name)))
            elif kind == T_DATA:
                sender, _, channel, size = r.take('H'), r.take('H'), r.take('I'), r.take('I')
                if channel != WORLD:
                    continue
                body = json.loads(r.raw(size))
                k = body.get('kind')
                if k == 'snapshot':
                    world.update(epoch=body['epoch'], revision=body['revision'], world=body['world'])
                elif k == 'entry_result':
                    active = body.get('active')
                    if active and not summary['admitted_at']:
                        summary['admitted_at'] = round(now - start, 2)
                        log('admitted by host')
                    if not active and summary['blockers'] != body.get('blockers'):
                        summary['blockers'] = body.get('blockers')
                        log('entry blocked', blockers=body.get('blockers'))
                elif k == 'enemy_cast':
                    summary['enemy_casts'].append(body)
                    log('enemy cast', key=body['key'], skill=body['skill'], target=body['target'])
                elif k == 'player_hit':
                    summary['player_hits'].append(body)
                    log('hit by enemy', amount=body['amount'], key=body.get('key'))
            elif kind == T_BYE:
                log('room closed')
                return
        if st['id'] and not st['udp_ok'] and now - last['hello'] > 0.5:
            last['hello'] = now
            udp.sendto(udp_header(U_HELLO) + struct.pack('<HI', st['id'], st['token']), target)
        try:
            while True:
                data, _ = udp.recvfrom(2048)
                magic, protocol, kind, _, _ = struct.unpack_from('<IHBBI', data)
                if magic != MAGIC or protocol != PROTOCOL:
                    continue
                if kind == U_WELCOME:
                    st['udp_ok'] = True
                elif kind == U_STATE and struct.unpack_from('<H', data, 12)[0] == 1 and st['udp_ok']:
                    body = bytearray(data[12:])
                    seq += 1
                    struct.pack_into('<HI', body, 0, st['id'], seq)
                    x, y, z = struct.unpack_from('<3f', body, 17)
                    struct.pack_into('<3f', body, 17, x + a.offset[0], y + a.offset[1], z + a.offset[2])
                    udp.sendto(udp_header(U_STATE) + bytes(body), target)
        except (socket.timeout, ConnectionResetError):
            pass
        w = world['world']
        entity = my_entity()
        if a.near_enemy and w and not st.get('placed'):
            alive = [e for e in w.get('entities', []) if e['hp'] > 0 and e.get('alive', True)]
            if alive:
                me = w['position']
                foe = min(alive, key=lambda e: math.dist(e['position'], me))
                a.offset = [foe['position'][0] - me[0] + 2.0, foe['position'][1] - me[1], foe['position'][2] - me[2]]
                st['placed'] = True
                log('standing next to enemy', key=foe['key'], distance_from_host=round(math.dist(foe['position'], me), 1))
        if w and entity and now - last['entry'] > 0.5:
            last['entry'] = now
            send_world({'kind': 'entry_prepare', 'epoch': world['epoch'], 'revision': world['revision'], 'token': entry_token,
                        'map': w['map'], 'level': w['level'], 'actor': entity,
                        'replica': {'map_loaded': True, 'entities_ready': True, 'world_isolated': True, 'logical_players': True}})
        if w and summary['admitted_at'] and a.simulate_owned and now - last.get('own', 0) > 0.1:
            last['own'] = now
            states = []
            for e in w.get('entities', []):
                if e.get('owner') != st['id'] or e['hp'] <= 0 or not e.get('alive', True):
                    continue
                if e['key'] not in summary['owned']:
                    summary['owned'][e['key']] = {'since': round(now - start, 2), 'origin': e['position']}
                    log('host gave us an enemy', key=e['key'])
                o = summary['owned'][e['key']]['origin']
                angle = (now - start) * 0.8
                states.append({'key': e['key'], 'position': [o[0] + 2 * math.cos(angle), o[1], o[2] + 2 * math.sin(angle)],
                               'rotation': [0, math.sin(angle / 2), 0, math.cos(angle / 2)], 'engaged': True})
            if states:
                send_world({'kind': 'enemy_state', 'epoch': world['epoch'], 'states': states[:64]})
                summary['states_sent'] += len(states)
                if a.cast_skill and now - last.get('cast', 0) > 3:
                    last['cast'] = now
                    send_world({'kind': 'enemy_cast', 'epoch': world['epoch'], 'key': states[0]['key'], 'skill': a.cast_skill, 'target': 1})
                    summary['casts_reported'] += 1
        if a.hit_limit and summary['hits_sent'] >= a.hit_limit:
            pass
        elif w and summary['admitted_at'] and not a.no_hits and now - last['hit'] > a.hit_interval:
            last['hit'] = now
            alive = [e for e in w.get('entities', []) if e['hp'] > 0 and e.get('alive', True)]
            if victim and not any(e['key'] == victim for e in alive):
                summary['kills'].append(victim)
                log('host reports enemy dead', key=victim)
                victim = None
            if not victim and alive:
                me = w['position']
                alive.sort(key=lambda e: math.dist(e['position'], me))
                victim = alive[0]['key']
                log('target chosen', key=victim, hp=alive[0]['hp'], max_hp=alive[0]['max_hp'])
            if victim:
                e = next(x for x in alive if x['key'] == victim)
                hit_seq += 1
                send_world({'kind': 'hit', 'epoch': world['epoch'], 'seq': hit_seq, 'key': victim, 'amount': e['max_hp'] * a.hit_fraction})
                summary['hits_sent'] += 1
        if now - last['keep'] > 2:
            last['keep'] = now
            tcp.sendall(frame(T_KEEPALIVE))
    try:
        tcp.sendall(frame(T_BYE, text('probe finished', 128)))
    except OSError:
        pass
    tcp.close()
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: v for k, v in summary.items() if k != 'events'}, ensure_ascii=False)[:2000])


if __name__ == '__main__':
    main()
