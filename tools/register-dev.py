"""Add a new EF Link development package to a Better-Endfield module index.

Creates immutable build/dev/<id> packages, keeps all existing module records and
never prints authentication tokens. Use a new ID for a DLL already loaded in a game.
For normal installation, import the release ZIP with Better-Endfield instead.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--id", default="eflink.core")
    parser.add_argument("--dll", type=Path, default=ROOT / "build/eflink.core.dll")
    parser.add_argument("--index", type=Path, default=Path(os.environ["LOCALAPPDATA"]) / "BetterEndfield/third-party/index.json")
    parser.add_argument("--reuse", action="store_true", help="Register the identical package in another mirrored index")
    parser.add_argument("--ownership-record", type=Path,
                        help="Optional installer ownership manifest tracking the chosen index's SHA-256")
    args = parser.parse_args()
    if not re.fullmatch(r"eflink\.[a-z0-9_.-]{1,64}", args.id):
        raise RuntimeError("Development module IDs must start with eflink.")
    raw = args.index.read_bytes()
    index = json.loads(raw.decode("utf-8-sig"))
    if index.get("schema") != 1 or len(index["modules"]) >= 128:
        raise RuntimeError("Invalid/full module index")
    if any(m["id"] == args.id for m in index["modules"]):
        raise RuntimeError("ID is already registered; use a new ID or import an updated release and restart")
    ownership = None
    if args.ownership_record:
        ownership = json.loads(args.ownership_record.read_text(encoding="utf-8"))
        tracked = next((f for f in ownership["files"] if Path(f["path"]).resolve() == args.index.resolve()), None)
        if tracked and tracked["sha256"] != hashlib.sha256(raw).hexdigest():
            raise RuntimeError("Index differs from its installer ownership record; preserving it")
    package = ROOT / "build/dev" / args.id
    library = package / f"native/windows-x64/{args.id}.dll"
    if package.exists():
        if not args.reuse or not library.is_file() or library.read_bytes() != args.dll.read_bytes():
            raise RuntimeError("Existing package preserved; use a fresh ID")
    else:
        package.mkdir(parents=True)
        library.parent.mkdir(parents=True)
        shutil.copy2(args.dll, library)
    manifest = json.loads((ROOT / "module.json").read_text(encoding="utf-8"))
    manifest["id"] = args.id
    manifest["libraries"]["windows-x64"] = library.relative_to(package).as_posix()
    (package / "module.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    shutil.copy2(ROOT / "LICENSE", package / "LICENSE")
    index["modules"].append({"id": args.id, "directory": str(package), "generation": args.id + "-" + manifest["version"],
                             "enabled": True, "configuration": manifest["default_configuration"]})
    encoded = json.dumps(index, ensure_ascii=False).encode("utf-8")
    temporary = args.index.with_name(args.index.name + ".eflink-new")
    with temporary.open("xb") as stream:
        stream.write(encoded)
    try:
        os.replace(temporary, args.index)
        if ownership is not None:
            for f in ownership["files"]:
                if Path(f["path"]).resolve() == args.index.resolve():
                    f["sha256"] = hashlib.sha256(encoded).hexdigest()
            args.ownership_record.write_text(json.dumps(ownership, indent=2), encoding="utf-8")
    except BaseException:
        args.index.write_bytes(raw)
        raise
    print(f"Registered {args.id} from {package}. Existing modules preserved.")


if __name__ == "__main__":
    main()
