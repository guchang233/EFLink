"""Show a bounded subset of previously captured read-only metadata."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('classes', nargs='+')
p.add_argument('--match', default='State,Stage,Create,Init,Trigger,Tick,Pause,Sync,Enable,Register,Clone')
a = p.parse_args()
root = Path(__file__).resolve().parents[1] / 'reports/world-contracts'
for name in a.classes:
    data = json.loads((root / (name + '.json')).read_text(encoding='utf-8'))['body']
    klass = data['hierarchy'][0]
    words = a.match.split(',')
    methods = [m for m in klass['methods'] if any(w in m['name'] for w in words) and 'iFix' not in m['name']]
    print(name)
    print(json.dumps({'fields': klass['fields'], 'methods': methods[:36]}, ensure_ascii=True))
