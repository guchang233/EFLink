"""Reconstruct the missing native battle oneof from read-only metadata.

Only the designated vendor declaration may be changed; all payload types must
already exist in the source schema. Refuses ambiguous signatures or field numbers.
"""
import json
import re
from pathlib import Path
from rpc import request

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = ROOT / 'server/vendor/lr-proto/endfield.proto'


def snake(name):
    return re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', re.sub(r'(?<=[A-Z])(?=[A-Z][a-z])', '_', name)).lower()


def main():
    result = request({'class': 'BATTLE_CLIENT_OP_DATA', 'namespace': 'Proto',
                      'assembly': 'Beyond.Network.Proto.dll'}, 'eflink.probe4')
    if result.get('result') != 0:
        raise RuntimeError('Native battle metadata unavailable')
    body = result['body']
    declared = re.findall(r'\b(?:message|enum)\s+(\w+)\s*\{', SCHEMA.read_text(encoding='utf-8'))
    rows = []
    for key, number in body['literals'].items():
        if not key.endswith('FieldNumber') or number < 100:
            continue
        name = key.removesuffix('FieldNumber')
        getters = [m for m in body['methods'] if m['name'] == 'get_' + name and not m['parameters']]
        if len(getters) != 1 or not getters[0]['return'].startswith('Proto.'):
            raise RuntimeError('Missing/ambiguous payload getter: ' + name)
        type_name = getters[0]['return'].removeprefix('Proto.')
        if type_name not in declared:
            raise RuntimeError('Payload type absent from baseline: ' + type_name)
        rows.append((number, type_name, snake(name)))
    if len(rows) != 20 or len({r[0] for r in rows}) != len(rows):
        raise RuntimeError('Unexpected native battle oneof; source left unchanged')
    text = SCHEMA.read_text(encoding='utf-8')
    start = re.search(r'\bmessage BATTLE_CLIENT_OP_DATA\s*\{', text)
    if not start:
        raise RuntimeError('Battle declaration missing')
    depth, end = 1, start.end()
    while end < len(text) and depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if depth:
        raise RuntimeError('Unbalanced source declaration')
    # Preserve verified generic operation header fields; replace only the oneof.
    old = text[start.end():end-1]
    header = old.split('oneof op_data_impl', 1)[0].rstrip()
    expected = {'OwnerIdFieldNumber': 1, 'OpTypeFieldNumber': 2, 'GenerationFieldNumber': 3,
                'ChapterMapIdFieldNumber': 4, 'SeqIdFieldNumber': 5, 'ClientTickTmsFieldNumber': 6}
    if any(body['literals'].get(k) != v for k, v in expected.items()):
        raise RuntimeError('Native header differs; source left unchanged')
    new = header + '\n    oneof op_data_impl {\n' + ''.join(
        f'        {t} {n} = {i};\n' for i, t, n in sorted(rows)) + '    }\n'
    SCHEMA.write_text(text[:start.end()] + new + text[end-1:], encoding='utf-8')
    report = {'source': 'read_only_native_metadata', 'class': body['class'],
              'payload_fields': [{'number': i, 'type': t, 'name': n} for i, t, n in sorted(rows)],
              'world_authority_proven': False}
    (ROOT / 'reports/authority-battle-schema.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('Updated native battle oneof:', len(rows), 'verified payload fields')


if __name__ == '__main__':
    main()
