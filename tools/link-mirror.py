"""EF Link single-computer test peer.

Joins an EF Link room as an extra player that copies the host: same operator model, same
pose, standing a little to the side and slightly behind in time. With one game running and
its room open, the mirror shows up in the game exactly like a friend from another computer.

    python tools/link-mirror.py                       # join 127.0.0.1:47615
    python tools/link-mirror.py --offset 0 0 3 --delay 0.5 --name 镜像
"""
import argparse
import socket
import struct
import threading
import time

MAGIC, PROTOCOL = 0x4B4C4645, 1
T_HELLO, T_WELCOME, T_REJECT, T_INFO, T_LEAVE, T_CHAT, T_DATA, T_BYE, T_KEEPALIVE = range(1, 10)
U_HELLO, U_WELCOME, U_STATE = 3, 4, 5


def fnv64(text):
    value = 14695981039346656037
    for byte in text.encode('utf-8'):
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


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
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--address', default='127.0.0.1:47615')
    parser.add_argument('--password', default='')
    parser.add_argument('--name', default='镜像测试')
    parser.add_argument('--offset', type=float, nargs=3, default=[2.0, 0.0, 0.0], metavar=('X', 'Y', 'Z'),
                        help='world offset from the host in Endfield units (metres)')
    parser.add_argument('--delay', type=float, default=0.3, help='seconds the mirror lags behind the host')
    parser.add_argument('--duration', type=float, default=0, help='leave after this many seconds (0 = until Ctrl+C)')
    args = parser.parse_args()
    host, _, port = args.address.partition(':')
    port = int(port or 47615)

    tcp = socket.create_connection((host, port), timeout=5)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(('0.0.0.0', 0))
    udp.settimeout(0.05)
    proof = fnv64('0:' + args.password) if args.password else 0
    hello = struct.pack('<IH', MAGIC, PROTOCOL) + text('mirror', 32) + text(args.name, 64) + struct.pack('<QH', proof, udp.getsockname()[1])
    tcp.sendall(frame(T_HELLO, hello))
    tcp.settimeout(0.05)

    state = {'id': 0, 'room': 0, 'token': 0, 'udp_ok': False, 'source': None, 'running': True}
    pending, buffer = [], b''
    sequence, last_keepalive, last_hello, started = 0, 0, 0, time.time()
    target = (socket.gethostbyname(host), port)
    print(f'connecting to {host}:{port} as {args.name} …')

    def udp_header(kind):
        return struct.pack('<IHBBI', MAGIC, PROTOCOL, kind, 0, state['room'])

    while state['running']:
        now = time.time()
        if args.duration and now - started > args.duration:
            break
        try:
            chunk = tcp.recv(65536)
            if not chunk:
                print('host closed the connection')
                break
            buffer += chunk
        except socket.timeout:
            pass
        while len(buffer) >= 5:
            length, kind = struct.unpack_from('<IB', buffer)
            if len(buffer) < 4 + length:
                break
            r = Reader(buffer[5:4 + length])
            buffer = buffer[4 + length:]
            if kind == T_WELCOME:
                state['id'], state['room'], state['token'] = r.take('H'), r.take('I'), r.take('I')
                print(f'joined room “{r.text(64)}” as player {state["id"]}')
            elif kind == T_REJECT:
                print('rejected:', r.text(128))
                state['running'] = False
            elif kind == T_INFO:
                info = read_info(r)
                if info['id'] == 1:
                    print(f'host model: {info["model"]} map {info["map"]} bones {len(info["bones"])}')
                    tcp.sendall(frame(T_INFO, write_info(info, args.name)))
            elif kind == T_CHAT:
                sender = r.take('H')
                print(f'chat from {sender}: {r.text(1024)}')
            elif kind == T_BYE:
                print('room closed')
                state['running'] = False
        if state['id'] and not state['udp_ok'] and now - last_hello > 0.5:
            last_hello = now
            udp.sendto(udp_header(U_HELLO) + struct.pack('<HI', state['id'], state['token']), target)
        try:
            while True:
                data, _ = udp.recvfrom(2048)
                magic, protocol, kind, _, session = struct.unpack_from('<IHBBI', data)
                if magic != MAGIC or protocol != PROTOCOL:
                    continue
                if kind == U_WELCOME:
                    state['udp_ok'] = True
                    print('pose channel ready')
                elif kind == U_STATE and struct.unpack_from('<H', data, 12)[0] == 1:
                    pending.append((now + args.delay, bytearray(data[12:])))
        except (socket.timeout, ConnectionResetError):
            pass
        while pending and pending[0][0] <= now:
            _, body = pending.pop(0)
            sequence += 1
            struct.pack_into('<HI', body, 0, state['id'], sequence)
            x, y, z = struct.unpack_from('<3f', body, 17)
            struct.pack_into('<3f', body, 17, x + args.offset[0], y + args.offset[1], z + args.offset[2])
            if state['udp_ok']:
                udp.sendto(udp_header(U_STATE) + bytes(body), target)
        if now - last_keepalive > 2:
            last_keepalive = now
            tcp.sendall(frame(T_KEEPALIVE))
    try:
        tcp.sendall(frame(T_BYE, text('mirror finished', 128)))
    except OSError:
        pass
    tcp.close()
    print('left the room')


if __name__ == '__main__':
    main()
