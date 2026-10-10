"""Enable/disable only an EF Link development module; preserve other modules."""
from pathlib import Path
import argparse
import hashlib
import json
import os

parser=argparse.ArgumentParser()
parser.add_argument("module")
parser.add_argument("state",choices=["on","off"])
parser.add_argument("--index",type=Path,default=Path(os.environ["LOCALAPPDATA"])/"BetterEndfield/third-party/index.json")
parser.add_argument("--ownership-record",type=Path)
args=parser.parse_args()
if not args.module.startswith("eflink."):
    raise SystemExit("This tool only manages eflink.* modules")
raw=args.index.read_bytes();index=json.loads(raw.decode("utf-8-sig"))
matches=[m for m in index["modules"] if m["id"]==args.module]
if len(matches)!=1:
    raise SystemExit("Module is not registered")
ownership=None
if args.ownership_record:
    ownership=json.loads(args.ownership_record.read_text(encoding="utf-8"))
    for f in ownership["files"]:
        if Path(f["path"]).resolve()==args.index.resolve() and f["sha256"]!=hashlib.sha256(raw).hexdigest():
            raise SystemExit("Changed index preserved")
matches[0]["enabled"]=args.state=="on"
encoded=json.dumps(index,ensure_ascii=False).encode("utf-8")
temp=args.index.with_name(args.index.name+".eflink-new")
with temp.open("xb") as stream:stream.write(encoded)
os.replace(temp,args.index)
if ownership:
    for f in ownership["files"]:
        if Path(f["path"]).resolve()==args.index.resolve():f["sha256"]=hashlib.sha256(encoded).hexdigest()
    args.ownership_record.write_text(json.dumps(ownership,indent=2),encoding="utf-8")
print(f"{args.module}: {args.state}")
