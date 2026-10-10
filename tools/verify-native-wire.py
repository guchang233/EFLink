"""Verify synthetic server fixtures through EF's actual protobuf parser.

This never delivers the parsed messages to gameplay, proves no admission and
reads no account/session objects. Run after loading a fresh native-wire module.
"""
import argparse
import json
import runpy
from pathlib import Path
from rpc import request

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--module', default='eflink.nativewire2')
    parser.add_argument('--index', type=Path)
    parser.add_argument('--fixtures', type=Path, default=ROOT / 'build/native-server/share/native-fixtures.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'reports/authority-native-wire.json')
    args = parser.parse_args()
    fixtures = json.loads(args.fixtures.read_text(encoding='utf-8'))
    wire_fields = runpy.run_path(str(ROOT / 'tools/analyse-world-wire.py'))['fields']
    outcomes = []
    for fixture in fixtures:
        reply = request({'action': 'roundtrip', **fixture}, args.module, args.index)
        result = reply.get('body', {})
        if reply.get('result') != 0 or not result.get('native_parser_accepted') or result.get('authority_established'):
            raise RuntimeError(f'{fixture["type"]}: native fixture mismatch: {result.get("error", "wire differs")}')
        # Field order is not semantic in protobuf. The corrected team bootstrap
        # includes field 16 before oneof field 12; EF canonically writes 12 first.
        # Compare field values/order within each repeated field, not byte order
        # between distinct fields. Nested fixtures are encoded in schema order.
        field_values_match = wire_fields(bytes.fromhex(fixture['hex'])) == wire_fields(bytes.fromhex(result['output_hex']))
        if not field_values_match:
            raise RuntimeError(f'{fixture["type"]}: native re-encoding changed fixture fields')
        result['top_level_wire_fields_match'] = True
        fields = result.get('native_fields', {})
        if fixture['type'] == 'BATTLE_CLIENT_OP_DATA':
            assert fields == {'owner_id': 42, 'chapter_map_id': 103, 'entity_inst_id': 501, 'delta_value': -25, 'hp': 100}, fields
        if fixture['type'] in ('SC_SELF_SCENE_INFO', 'SC_ENTER_SCENE_NOTIFY'):
            assert fields == {'scene_num_id': 103, 'scene_id': 7001}, fields
        outcomes.append(result)
        print(f'{fixture["type"]}: native fields match; exact bytes {result["exact_wire_roundtrip"]}; typed fields {fields}')
    malformed = request({'action': 'roundtrip', 'type': 'CS_BATTLE_OP', 'hex': '0a80'}, args.module, args.index)
    if malformed.get('result') == 0:
        raise RuntimeError('Native parser accepted a truncated submessage')
    report = {'synthetic_fixtures_only': True, 'authority_established': False,
              'world_state_modified': False, 'fixtures': outcomes, 'malformed_rejected': malformed}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'{len(outcomes)} native fixtures passed; truncated submessage rejected. World admission remains unproven.')


if __name__ == '__main__':
    main()
