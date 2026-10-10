"""Exercise the actual Zig DLL through C ABI; no EF client or game acceptance."""
import argparse
import ctypes as c
import importlib.util
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_pb import u, i32, d, s, m

spec = importlib.util.spec_from_file_location('wire', ROOT / 'tools/analyse-world-wire.py')
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)
P = c.c_void_p
U64, U32, U16, I32 = c.c_uint64, c.c_uint32, c.c_uint16, c.c_int32


class Message(c.Structure):
    _fields_ = [('world_id', U64), ('transition_id', U64), ('message_id', U32),
                ('size', U32), ('recipient', U16), ('reserved', U16), ('reserved2', U32)]


class Proof(c.Structure):
    _fields_ = [(name, U64) for name in ('transition_id', 'world_id', 'scene_id', 'leader_id', 'private_feed_generation')]


FUNCTIONS = [
    ('create', c.CFUNCTYPE(I32, U64, U64, I32, c.POINTER(P))),
    ('destroy', c.CFUNCTYPE(None, P)),
    ('add_member', c.CFUNCTYPE(I32, P, U16)),
    ('import_host_scene', c.CFUNCTYPE(I32, P, U16, P, U32)),
    ('allocate_approved_operator', c.CFUNCTYPE(I32, P, U16, P, U32, c.POINTER(U64))),
    ('begin_scene', c.CFUNCTYPE(I32, P, U16, U64, U64, c.POINTER(U64))),
    ('scene_loaded', c.CFUNCTYPE(I32, P, U16, U64, U64, P, U32)),
    ('native_applied', c.CFUNCTYPE(I32, P, U16, c.POINTER(Proof))),
    ('poll', c.CFUNCTYPE(I32, P, c.POINTER(Message), P, U32)),
    ('remove_member', c.CFUNCTYPE(I32, P, U16)),
]


class Api(c.Structure):
    _fields_ = [('struct_size', U32), ('version', U32)] + FUNCTIONS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, default=ROOT / 'build/native-server/bin/eflink-native-authority.dll')
    args = parser.parse_args()
    library = c.CDLL(str(args.dll.resolve()))
    entry = library.EFLinkNative_GetApiV1
    entry.restype = c.POINTER(Api)
    api = entry().contents
    checks = 0

    def check(condition, label):
        nonlocal checks
        if not condition:
            raise AssertionError(label)
        checks += 1
        print('PASS', label)

    check(c.sizeof(Message) == 32 and c.sizeof(Proof) == 40, 'native message/proof ABI layout')
    check(api.version == 1 and api.struct_size == c.sizeof(Api), 'C function table identity and size')
    ctx = P(123)
    check(api.create(0, 7001, 103, c.byref(ctx)) == 1 and not ctx.value, 'invalid creation clears caller output')
    check(api.add_member(None, 1) == 1, 'null contexts are rejected')
    check(api.create(9001, 7001, 103, c.byref(ctx)) == 0 and ctx.value, 'shared kernel context created')
    try:
        check(api.add_member(ctx, 1) == 0 and api.add_member(ctx, 2) == 0, 'authenticated connection IDs have separate sessions')
        check(api.add_member(ctx, 2) == 2 and api.add_member(ctx, 0) == 1, 'duplicate and zero members rejected')
        common = u(1, 1001) + s(3, 'synthetic_host') + i32(7, 103) + d(8, 300)
        character = m(1, common) + m(5, u(3, 7) + u(4, 90001))
        monster = m(1, u(1, 2001) + i32(2, 3) + s(3, 'synthetic_enemy') + i32(7, 103) + d(8, 500)) + m(5, u(3, 9) + u(4, 90002))
        snapshot = i32(1, 103) + u(2, 7001) + m(3, m(1, character) + m(2, monster)) + m(12, b'') + m(16, u(4, 1001) + u(3, 22))
        source = c.create_string_buffer(snapshot)
        check(api.import_host_scene(ctx, 2, source, len(snapshot)) == 2, 'guest cannot import the host world')
        check(api.import_host_scene(ctx, 1, source, len(snapshot)) == 0, 'native host entity and battle IDs imported')
        c.memset(source, 0xFF, len(snapshot))
        approved = c.create_string_buffer(character)
        guest = U64()
        check(api.allocate_approved_operator(ctx, 2, approved, len(character), c.byref(guest)) == 0 and guest.value not in (0, 1001, 2001, 90001, 90002), 'host-approved operator allocation avoids imported identities')
        transition = U64()
        check(api.begin_scene(ctx, 2, 22, 9, c.byref(transition)) == 0 and transition.value == 1, 'change and enter messages published in one transition')
        duplicate = U64(999)
        check(api.begin_scene(ctx, 2, 22, 9, c.byref(duplicate)) == 2 and duplicate.value == 0, 'pending native transition cannot restart')

        def poll(expected):
            head = Message()
            check(api.poll(ctx, c.byref(head), None, 0) == 4 and head.message_id == expected, 'size query preserves queued native message')
            destination = c.create_string_buffer(head.size)
            check(api.poll(ctx, c.byref(head), destination, max(0, head.size - 1)) == 4, 'short caller buffer preserves queued message')
            check(api.poll(ctx, c.byref(head), destination, head.size) == 0 and head.recipient == 2 and head.world_id == 9001 and head.transition_id == 1, 'native body copied to caller memory with routing metadata')
            return wire.fields(destination.raw[:head.size])

        poll(838)
        enter = poll(24)
        check(wire.integer(enter, 3) == 7001, 'native enter names the shared scene instance')
        load = c.create_string_buffer(i32(1, 103))
        check(api.scene_loaded(ctx, 2, 9001, 2, load, 2) == 2, 'stale acknowledgment leaves transition recoverable')
        check(api.scene_loaded(ctx, 2, 9001, 1, load, 2) == 0, 'real load-finish protobuf produces native bootstrap')
        bootstrap = poll(25)
        detail = next(wire.submessages(bootstrap, 3))
        host = next(record for record in wire.submessages(detail, 1) if wire.integer(next(wire.submessages(record, 1)), 1) == 1001)
        check(wire.integer(next(wire.submessages(host, 5)), 4) == 90001, 'borrowed input mutation cannot change preserved host battle ID')
        enemy = next(wire.submessages(detail, 2))
        check(wire.integer(next(wire.submessages(enemy, 1)), 1) == 2001, 'guest bootstrap retains the shared monster ID')
        team = next(wire.submessages(bootstrap, 16))
        check(wire.integer(team, 4) == guest.value, 'guest bootstrap retains its independent leader')
        head = Message()
        check(api.poll(ctx, c.byref(head), None, 0) == 5 and head.size == 0, 'empty queue does not expose stale metadata')
        proof = Proof(1, 9001, 7001, guest.value, 8)
        check(api.native_applied(ctx, 2, c.byref(proof)) == 2, 'map acknowledgment cannot substitute for private-feed proof')
        proof.private_feed_generation = 9
        check(api.native_applied(ctx, 2, c.byref(proof)) == 0, 'trusted proof completes only the core handshake')
        check(api.import_host_scene(ctx, 1, approved, len(character)) == 2, 'active bootstrap cannot be replaced by a new host import')
        check(api.begin_scene(ctx, 2, 22, 10, c.byref(transition)) == 0 and transition.value == 2, 'next transition has a fresh ID')
        check(api.remove_member(ctx, 1) == 2, 'host removal requires whole world teardown')
        check(api.remove_member(ctx, 2) == 0, 'disconnect cancels a guest during native loading')
        check(api.poll(ctx, c.byref(head), None, 0) == 5, 'disconnected guest receives no queued scene payload')
        check(api.scene_loaded(ctx, 2, 9001, 2, load, 2) == 2, 'disconnected guest cannot complete the old transition')
        check(api.add_member(ctx, 2) == 0, 'authenticated member ID can be reused after disconnect')
        check(api.allocate_approved_operator(ctx, 2, approved, len(character), c.byref(guest)) == 0, 'reconnected member receives a fresh server operator')
        check(api.begin_scene(ctx, 2, 22, 9, c.byref(transition)) == 0 and transition.value == 3, 'transition token is not reused with the member ID')
        check(api.scene_loaded(ctx, 2, 9001, 1, load, 2) == 2, 'previous connection acknowledgment cannot admit the reused ID')
        # Destroy with unconsumed payloads: all queued buffers are library-owned.
    finally:
        api.destroy(ctx)
    for _ in range(32):
        instance = P()
        if api.create(9001, 7001, 103, c.byref(instance)) != 0:
            raise AssertionError('repeat creation')
        api.destroy(instance)
    api.destroy(None)
    check(True, 'contexts and queued payloads survive repeated create/destroy')
    print(f'{checks} C ABI checks passed; no real-client acceptance claimed')


if __name__ == '__main__':
    main()
