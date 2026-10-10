"""Read metadata-resolved gameplay methods; disassemble bounded local copies.

Capstone Python API: https://www.capstone-engine.org/lang_python.html
Analysis stays under reports/, and contains no host credentials or account data.
"""
import argparse
import json
from pathlib import Path
import sys
from rpc import request

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'build/analysis-deps'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

p = argparse.ArgumentParser()
p.add_argument('type')
p.add_argument('--method')
p.add_argument('--namespace', default='Beyond.Gameplay.Core')
p.add_argument('--assembly', default='Gameplay.Beyond.dll')
p.add_argument('--bytes', type=int, default=0)
p.add_argument('--offset', type=int, default=0)
p.add_argument('--module', default='eflink.probe1')
a = p.parse_args()
body = {'class': a.type, 'namespace': a.namespace, 'assembly': a.assembly, 'bytes': a.bytes, 'offset': a.offset}
if a.method:
    body['method'] = a.method
result = request(body, a.module)
dest = root / 'reports/native-contracts'
dest.mkdir(exist_ok=True)
stem = a.type.replace('/', '-') + ('-' + a.method if a.method else '')
if a.offset:
    stem += '-offset-' + str(a.offset)
dest.joinpath(stem + '.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
if result.get('result') != 0:
    raise RuntimeError(result.get('body', {}))
methods = result['body']['methods']
md = Cs(CS_ARCH_X86, CS_MODE_64)
symbols = {}
# Related methods provide symbolic labels for direct native calls.
index_path = dest / (a.type.replace('/', '-') + '.json')
if index_path.exists():
    for method in json.loads(index_path.read_text())['body']['methods']:
        if 'rva' in method:
            symbols[method['rva']] = method['name']
lines = []
for method in methods:
    print(method['name'], hex(method.get('rva', 0)), method['parameters'])
    lines.append(f"\n{method['name']}({method['parameters']}):")
    if 'code' not in method:
        continue
    code = bytes.fromhex(method['code'])
    for address, size, mnemonic, operands in md.disasm_lite(code, method.get('code_rva', method['rva'])):
        label = ''
        if mnemonic in ('call', 'jmp') and operands.startswith('0x'):
            target = int(operands, 16)
            label = ' ; ' + symbols.get(target, '') if target in symbols else ''
        lines.append(f'{address:08x} {mnemonic:8} {operands}{label}')
        if mnemonic == 'int3':
            break  # MSVC alignment padding after the current function
dest.joinpath(stem + '.asm.txt').write_text('\n'.join(lines), encoding='utf-8')
print('literals', result['body']['literals'])
print(dest / (stem + '.asm.txt'))
