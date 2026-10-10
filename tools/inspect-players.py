"""Read logical-player contracts from the existing diagnostic module (metadata only)."""
import json
from pathlib import Path
from rpc import request

root = Path(__file__).resolve().parents[1] / 'reports' / 'world-contracts'
root.mkdir(parents=True, exist_ok=True)
for namespace, name in [
    ('Beyond.Gameplay.Core', 'CharacterInfo'),
    ('Beyond.Gameplay.Core', 'CharacterFullData'),
    ('Beyond.Gameplay.Core', 'CharacterEntityFactory'),
    ('Beyond.Gameplay.Core', 'PlayerController'),
    ('Beyond.Gameplay', 'GamePlayer'),
    ('Beyond.Gameplay.Core', 'CharacterController'),
]:
    result = request({'action': 'inspect', 'assembly': 'Gameplay.Beyond.dll',
                      'namespace': namespace, 'class': name}, 'endcraft.inspect3')
    (root / (name + '.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    body = result.get('body', result)
    print(name, json.dumps(body, ensure_ascii=True)[:100], flush=True)
