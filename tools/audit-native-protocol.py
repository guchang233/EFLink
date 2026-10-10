"""Compare installed-client protobuf FIELD NUMBERS with a pinned source reference.

Only requests type metadata/literal constants from the read-only development probe.
Does not read game-message payloads or infer world authority from schema similarity.
"""
import argparse
import json
import re
from pathlib import Path
from rpc import request

CLASSES = (
    'CSHead', 'SC_CHANGE_SCENE_BEGIN_NOTIFY', 'SC_ENTER_SCENE_NOTIFY',
    'SC_LEAVE_SCENE_NOTIFY', 'SC_SELF_SCENE_INFO', 'SCENE_OBJECT_DETAIL_CONTAINER',
    'SCENE_OBJECT_COMMON_INFO', 'SCENE_CHARACTER', 'SCENE_MONSTER',
    'SCENE_INTERACTIVE', 'SCENE_NPC', 'SCENE_SUMMON', 'LEVEL_SCRIPT_INFO',
    'CS_SCENE_LOAD_FINISH', 'CS_ENTER_SCENE', 'CS_MOVE_OBJECT_MOVE',
    'SC_MOVE_OBJECT_MOVE', 'MOVE_OBJECT_MOVE_INFO', 'MOTION_INFO',
    'SC_OBJECT_ENTER_VIEW', 'SC_OBJECT_LEAVE_VIEW', 'BATTLE_MGR_INFO',
    'BATTLE_INFO', 'BATTLE_CLIENT_DATA', 'BATTLE_CLIENT_OP_DATA', 'CS_BATTLE_OP',
    'SC_ENTITY_PROPERTY_CHANGE', 'SC_RESET_BATTLE_STATUS',
    'BATTLE_ENTITY_VALUE_MODIFY_DATA', 'BATTLE_ENTITY_DIE_OP_DATA',
    'BATTLE_SKILL_START_CAST_OP_DATA', 'BATTLE_SKILL_END_CAST_OP_DATA',
    'BATTLE_OP_MODIFY_POISE_VALUE_OP_DATA', 'BATTLE_OP_MODIFY_BATTLE_STATE_OP_DATA',
)


def normalize(name):
    return name.replace('_', '').lower()


def source_fields(directory):
    messages = {}
    # The reference uses ordinary unencrypted .proto files. Nested declarations
    # are skipped, so their fields cannot be attributed to an enclosing message.
    for path in sorted(directory.rglob('*.proto')):
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(encoding='utf-8'), flags=re.S)
        for match in re.finditer(r'\bmessage\s+(\w+)\s*\{', text):
            depth, pos = 1, match.end()
            while pos < len(text) and depth:
                depth += (text[pos] == '{') - (text[pos] == '}')
                pos += 1
            if depth:
                raise ValueError(f'Unbalanced message in {path}')
            body = text[match.end():pos-1]
            # oneof fields remain relevant; nested message/enum fields do not.
            body = re.sub(r'\b(?:message|enum)\s+\w+\s*\{[^{}]*\}', '', body)
            fields = {normalize(n): int(v) for n, v in re.findall(r'\b(\w+)\s*=\s*(\d+)\s*(?:\[[^\]]*\])?\s*;', body)}
            messages[match.group(1)] = {'source': str(path), 'fields': fields}
    return messages


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--module', default='eflink.probe4')
    parser.add_argument('--index', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    reference = source_fields(args.reference)
    results = []
    for name in CLASSES:
        result = request({'class': name, 'namespace': 'Proto', 'assembly': 'Beyond.Network.Proto.dll'}, args.module, args.index)
        native = result.get('body', {})
        ref = reference.get(name)
        row = {'class': name, 'native_resolved': result.get('result') == 0,
               'reference_present': ref is not None}
        if result.get('result') != 0:
            row['error'] = native.get('error', 'No metadata result')
        elif ref is not None:
            fields = {normalize(k.removesuffix('FieldNumber')): v for k, v in native.get('literals', {}).items() if k.endswith('FieldNumber')}
            row.update(native_fields=fields, reference_fields=ref['fields'],
                       changed_numbers={k: [v, fields[k]] for k, v in ref['fields'].items() if k in fields and v != fields[k]},
                       only_native={k: v for k, v in fields.items() if k not in ref['fields']},
                       only_reference={k: v for k, v in ref['fields'].items() if k not in fields})
        results.append(row)
        print(name, 'resolved' if row['native_resolved'] else 'unresolved',
              'changed=', len(row.get('changed_numbers', {})), 'new=', len(row.get('only_native', {})))
    report = {'scope': 'selected_message_field_numbers_only', 'world_authority_proven': False,
              'complete_protocol_compatibility_proven': False, 'reference': str(args.reference.resolve()), 'messages': results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
