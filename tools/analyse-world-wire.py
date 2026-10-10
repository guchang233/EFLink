"""Summarize bounded local gameplay captures, without printing payloads or IDs.

This is a wire-shape diagnostic, not a battle engine or world-admission proof.
"""
import argparse
import base64
from collections import Counter
import json
from pathlib import Path
import struct


def fields(data):
    at = 0

    def varint():
        nonlocal at
        value = 0
        for shift in range(0, 70, 7):
            if at >= len(data):
                raise ValueError('Truncated protobuf varint')
            byte = data[at]
            at += 1
            if shift == 63 and byte > 1:
                raise ValueError('Invalid protobuf varint')
            value |= (byte & 127) << shift
            if byte < 128:
                return value
        raise ValueError('Invalid protobuf varint')

    output = {}
    while at < len(data):
        tag = varint()
        number, wire = tag >> 3, tag & 7
        if number == 0:
            raise ValueError('Invalid field number')
        if wire == 0:
            value = varint()
        elif wire in (1, 5):
            size = 8 if wire == 1 else 4
            if at + size > len(data):
                raise ValueError('Truncated protobuf scalar')
            value = data[at:at + size]
            at += size
        elif wire == 2:
            size = varint()
            if at + size > len(data):
                raise ValueError('Truncated protobuf message')
            value = data[at:at + size]
            at += size
        else:
            raise ValueError('Unsupported wire type')
        output.setdefault(number, []).append((wire, value))
    return output


def integer(message, number, default=0):
    values = message.get(number, [])
    if not values:
        return default
    if values[-1][0] != 0:
        raise ValueError('Wrong integer wire type')
    return values[-1][1]


def submessages(message, number):
    for wire, value in message.get(number, []):
        if wire != 2:
            raise ValueError('Wrong message wire type')
        yield fields(value)


def analyse(messages):
    counts, opcodes = Counter(), Counter()
    operators, last_by_operator = set(), {}
    global_regressions = operator_regressions = total_ops = damage_ops = 0
    negative_damage_ops = 0
    last_global = None
    scenes = []
    for item in messages:
        name = item['type']
        counts[name] += 1
        data = base64.b64decode(item['protobuf_base64'], validate=True)
        packet = fields(data)
        if name == 'CS_BATTLE_OP':
            for batch in submessages(packet, 1):
                for op in submessages(batch, 1):
                    total_ops += 1
                    owner, generation, seq = integer(op, 1), integer(op, 3), integer(op, 5)
                    operators.add(owner)
                    opcodes[str(integer(op, 2))] += 1
                    key = owner, generation
                    if key in last_by_operator and seq <= last_by_operator[key]:
                        operator_regressions += 1
                    if last_global is not None and seq <= last_global:
                        global_regressions += 1
                    last_by_operator[key] = seq
                    last_global = seq
                    for damage in submessages(op, 111):
                        damage_ops += 1
                        for wire, value in damage.get(7, []):
                            if wire != 1:
                                raise ValueError('Wrong damage delta wire type')
                            if struct.unpack('<d', value)[0] < 0:
                                negative_damage_ops += 1
        elif name == 'SC_SELF_SCENE_INFO':
            detail = next(submessages(packet, 3), {})
            team = next(submessages(packet, 16), {})
            scenes.append({'scene_num_id': integer(packet, 1),
                           'has_instance_id': bool(integer(packet, 2)),
                           'characters': len(detail.get(1, [])), 'monsters': len(detail.get(2, [])),
                           'interactives': len(detail.get(3, [])), 'npcs': len(detail.get(4, [])),
                           'level_scripts': len(packet.get(8, [])),
                           'has_team_leader': bool(integer(team, 4)),
                           'scene_impl': 'dungeon' if 11 in packet else 'empty' if 12 in packet else 'missing'})
    return {'authority_established': False, 'message_types': dict(counts), 'battle_ops': total_ops,
            'battle_opcodes': dict(opcodes), 'operator_count': len(operators),
            'global_sequence_nonincreases': global_regressions,
            'same_operator_generation_sequence_nonincreases': operator_regressions,
            'damage_ops': damage_ops, 'negative_delta_ops': negative_damage_ops, 'scenes': scenes}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    capture = json.loads(args.capture.read_text(encoding='utf-8'))
    body = capture.get('body', capture)
    result = analyse(body.get('messages', []))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
