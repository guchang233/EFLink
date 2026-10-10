"""Private, authenticated diagnostic access to a local Better-Endfield Host.

Player connections use the native menu. This CLI is for development and testing only.
Never prints the host authentication token. Nothing is sent outside this computer.
"""
from pathlib import Path
import argparse
import json
import os
import time
import urllib.request
import uuid


def request(body=None, module="eflink.core", index=None):
    path = Path(index) if index else Path(os.environ["LOCALAPPDATA"]) / "BetterEndfield/third-party/index.json"
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    endpoint = f'http://127.0.0.1:{config["port"]}'

    def call(path, data=None):
        req = urllib.request.Request(endpoint + path,
            data=json.dumps(data).encode() if data is not None else None,
            headers={"Authorization": "Bearer " + config["token"], "Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=4) as response:
            return json.load(response)

    if body is None:
        return call("/status")
    serial = uuid.uuid4().hex
    accepted = call("/send", {"module_id": module, "request_id": serial, "body": body})
    if not accepted.get("accepted"):
        raise RuntimeError("Host did not accept request")
    until = time.monotonic() + 15
    while time.monotonic() < until:
        for item in call("/poll", {"module_id": module}).get("messages", []):
            if item.get("kind") == "reply" and item.get("request_id") == serial:
                return item
        time.sleep(.1)
    raise TimeoutError("No reply from the module")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--body", type=json.loads)
    parser.add_argument("--module", default="eflink.core")
    parser.add_argument("--index", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = request(args.body, args.module, args.index)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))
