"""Read existing game's metadata through its local diagnostic module; no invocation."""
import json
from pathlib import Path
from rpc import request

root = Path(__file__).resolve().parents[1] / 'reports' / 'world-contracts'
root.mkdir(parents=True, exist_ok=True)
classes = [
    ('Beyond.Gameplay', 'TeleportProcessor'),
    ('Beyond.Gameplay', 'TeleportManager'),
    ('Beyond.Gameplay.Core', 'GameWorld'),
    ('Beyond.Gameplay.Core', 'WorldInfo'),
    ('Beyond.Gameplay.Core', 'Entity'),
    ('Beyond.Gameplay.Core', 'EnemyController'),
    ('Beyond.Gameplay.Core', 'AbilitySystem'),
    ('Beyond.Gameplay.Core', 'AbilitySystem/Modifier'),
]
for namespace, name in classes:
    result = request({'action': 'inspect', 'assembly': 'Gameplay.Beyond.dll',
                      'namespace': namespace, 'class': name}, 'endcraft.inspect3')
    (root / (name.replace('/', '-') + '.json')).write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(name, json.dumps(result.get('body', result), ensure_ascii=True)[:80])
