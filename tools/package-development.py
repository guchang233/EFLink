"""Package one tested, immutable development DLL for manual Better-Endfield import."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--id',required=True)
a=p.parse_args()
if not re.fullmatch(r'eflink\.dev[0-9]+',a.id):
    raise SystemExit('Choose a development ID, such as eflink.dev10')
dll=root/'build'/f'{a.id}.dll'
manifest=json.loads((root/'module.json').read_text(encoding='utf-8'))
manifest['id']=a.id
manifest['name']+=f' · {a.id} 测试版'
manifest['libraries']['windows-x64']=f'native/windows-x64/{a.id}.dll'
dest=root/'dist'/f'EFLink-{manifest["version"]}-{a.id.removeprefix("eflink.")}-test.zip'
dest.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    z.writestr('module.json',json.dumps(manifest,ensure_ascii=False,indent=2))
    z.write(dll,manifest['libraries']['windows-x64'])
    for name in ('README.md','LICENSE','THIRD-PARTY-NOTICES.md','docs/WORLD.md','docs/VALIDATION.md'):
        z.write(root/name,name)
    z.write(root/'vendor/imgui/LICENSE.txt','licenses/imgui.txt')
    z.write(root/'vendor/nlohmann-LICENSE.MIT','licenses/nlohmann.txt')
with zipfile.ZipFile(dest) as z:
    assert z.testzip() is None
    assert json.loads(z.read('module.json'))['id']==a.id
    assert z.read(manifest['libraries']['windows-x64'])==dll.read_bytes()
dest.with_suffix('.sha256').write_text(hashlib.sha256(dest.read_bytes()).hexdigest()+'  '+dest.name+'\n',encoding='ascii')
print(dest)
