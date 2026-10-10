"""Read two real game adapters; own teleport actions only, no screen/input automation."""
import argparse
import json
from pathlib import Path
import time
from rpc import request

root=Path(__file__).resolve().parents[1]
guest_index=root/'test-environments/dual-instance/profiles/guest/AppData/Local/BetterEndfield/third-party/index.json'
p=argparse.ArgumentParser()
p.add_argument('--module',default='eflink.dev3')
p.add_argument('--connect',action='store_true')
p.add_argument('--teleport',action='store_true')
a=p.parse_args()
def call(body,guest=False):
    return request(body,a.module,guest_index if guest else None)['body']
if a.connect:
    call({'action':'host','room_name':'EF Link world test','port':47615,'max_players':8})
    time.sleep(.5)
    call({'action':'join','address':'127.0.0.1:47615'},True)
    time.sleep(2)
states=[call({'action':'status'},guest) for guest in (False,True)]
result={name:{'role':s['role'],'hook_ready':s['hook_ready'],'damage_hook_ready':s['damage_hook_ready'],
             'game':s['game'],'world_session':s['world_session'],'peers':s['peers']}
        for name,s in zip(('host','guest'),states)}
if a.teleport:
    before=states[1]['game']['world_replication']['teleports_completed']
    call({'action':'teleport_to_player','id':1},True)
    until=time.monotonic()+12
    while time.monotonic()<until:
        s=call({'action':'status'},True)
        if s['game']['world_replication']['teleports_completed']>before:
            result['teleport_observed_complete']=True
            result['guest_after_teleport']=s['game']
            result['peers_after_teleport']=s['peers']
            break
        time.sleep(.2)
    else:result['teleport_observed_complete']=False
path=root/'reports'/'world-live.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
for name in ('host','guest'):
    s=result[name]
    print(name,json.dumps({'role':s['role'],'world':s['world_session'],
                         'native':s['game']['world_replication'],
                         'markers':s['game']['markers']},ensure_ascii=True))
if a.teleport:print('teleport_observed_complete',result['teleport_observed_complete'])
