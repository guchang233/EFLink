"""Check two real clients' native census transport; never certify gameplay.

Uses only the local EFML diagnostic credential. Official login data is neither
requested nor copied. Always closes its diagnostic room, including on failure.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import time

from rpc import request


def body(reply):
    if reply.get("result") != 0:
        raise RuntimeError("EFML diagnostic command failed")
    value = reply["body"]
    return json.loads(value) if isinstance(value, str) else value


def run(args):
    indices = [args.host_index, args.guest_index]

    def call(slot, action, **values):
        return body(request({"action": action, **values}, args.module, indices[slot], 4))

    def status(slot):
        value = call(slot, "status")
        authority = value["world_session"]["world_authority"]
        if value["module"] != args.module or authority["operation_adapter_verified"] or authority["full_sync_verified"]:
            raise RuntimeError("This tool only exercises an unsupported, read-only adapter")
        if value["game"]["authority_architecture"]["native_gameplay_replay"]:
            raise RuntimeError("Native gameplay replay must remain disabled")
        return value

    initial = [status(slot) for slot in (0, 1)]
    if any(value["role"] != "offline" for value in initial):
        raise RuntimeError("Refusing to replace an existing room")
    protocols = [value["world_session"]["protocol"] for value in initial]
    if protocols[0]["build"] != protocols[1]["build"] or protocols[0]["game_version"] != protocols[1]["game_version"]:
        raise RuntimeError("Client build or native version mismatch")

    started = False
    report = {"module": args.module, "build": protocols[0]["build"],
              "game_version": protocols[0]["game_version"], "full_sync_verified": False,
              "test_kind": "real_client_read_only_census_transport", "gameplay_acceptance": False}
    try:
        started = True
        call(0, "host", room_name="EF Link census transport check", max_players=2, port=args.port)
        call(1, "join", address=f"127.0.0.1:{args.port}", password="")
        deadline = time.monotonic() + 20
        aligned = None
        while time.monotonic() < deadline:
            values = [status(slot) for slot in (0, 1)]
            states = [call(slot, "world_state") for slot in (0, 1)]
            host_revision = states[0]["session"]["world_authority"]["host"]["revision"]
            guest_revision = states[1]["session"]["world_authority"]["replica_revision"]
            census = values[0]["game"]["world_replication"]["native_census"]
            player = census.get("player", {})
            squads_ready = not args.party_size or all(
                value["game"]["world_replication"].get("native_squad_source", {}).get("real_entities") == args.party_size
                for value in values)
            if player.get("error"):
                raise RuntimeError("Native player census: " + player["error"])
            if (all(value["world_session"]["ready"] and value["world_session"]["protocol"]["compatible"] for value in values)
                    and squads_ready and player.get("observed") and player.get("objects", 0) > 0
                    and states[0]["objects"] and host_revision == guest_revision
                    and states[0]["objects"] == states[1]["objects"]):
                aligned = (values, states)
                break
            time.sleep(.15)
        if aligned is None:
            raise RuntimeError("No matching native object baseline arrived within 20 seconds")
        values, states = aligned
        counts = Counter(item["kind"] for item in states[0]["objects"])
        if counts["mineral"] < 1:
            raise RuntimeError("No native minerals were enumerated")
        if any(value["world_session"]["entry"]["active"] for value in values):
            raise RuntimeError("Read-only transport must not admit gameplay")
        squads = [value["game"]["world_replication"].get("native_squad_source", {}) for value in values]
        if args.party_size and (counts["player"] != args.party_size or any(
                squad.get("real_slots") != args.party_size or squad.get("real_entities") != args.party_size for squad in squads)):
            raise RuntimeError("Both native real squads and the host ledger must contain the required party size")
        report.update(object_kinds=dict(counts), objects_match=True,
                      native_census=values[0]["game"]["world_replication"]["native_census"],
                      native_squad_source=values[0]["game"]["world_replication"].get("native_squad_source", {}),
                      native_squad_sources=squads,
                      scene_generation=states[0]["session"]["epoch"],
                      native_objects=states[0]["objects"])
        first = values[0]["world_session"]["world_authority"]["transport"]
        beginning = time.monotonic()
        while time.monotonic() - beginning < args.seconds:
            current = [status(slot) for slot in (0, 1)]
            if any(not value["world_session"]["ready"] or value["world_session"]["entry"]["active"] for value in current):
                raise RuntimeError("Native scene was withdrawn or gameplay unexpectedly admitted")
            if any(value["world_session"]["world_authority"]["handoff"]["phase"] != "personal" for value in current):
                raise RuntimeError("A read-only test must not hand off the personal world")
            time.sleep(.25)
        elapsed = time.monotonic() - beginning
        last = current[0]["world_session"]["world_authority"]["transport"]
        report["authority_transport"] = {key: last[key] - first[key] for key in
                                          ("baselines_sent", "deltas_sent", "heartbeats_sent", "bytes_sent")}
        report["measurement_seconds"] = elapsed
        report["authority_bytes_per_second"] = report["authority_transport"]["bytes_sent"] / elapsed
        report["gameplay_enabled"] = False
    finally:
        if started:
            errors = []
            for slot in (1, 0):
                try:
                    call(slot, "leave")
                except Exception as error:
                    errors.append(f"slot {slot}: {type(error).__name__}")
            if errors:
                raise RuntimeError("Diagnostic room cleanup failed: " + "; ".join(errors))
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                current = [status(slot) for slot in (0, 1)]
                if all(value["role"] == "offline" and not value["world_session"]["ready"] and
                       value["world_session"]["world_authority"]["observed"] == 0 and
                       not value["world_session"]["world_authority"]["handoff"]["personal_snapshot_retained"] for value in current):
                    report["diagnostic_room_cleanup_verified"] = True
                    break
                time.sleep(.15)
            else:
                raise RuntimeError("Diagnostic room references were not cleared")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", required=True)
    base = Path(__file__).resolve().parent.parent / "test-environments/light-help/temp/AppSandbox_Endfield"
    parser.add_argument("--host-index", type=Path, default=base / "_0/AppData/Local/EFML/modules/index.json")
    parser.add_argument("--guest-index", type=Path, default=base / "_1/AppData/Local/EFML/modules/index.json")
    parser.add_argument("--port", type=int, default=48667)
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--party-size", type=int, choices=range(1, 5), default=None)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535 or not 1 <= args.seconds <= 40:
        parser.error("Invalid bounded test duration or port")
    report = run(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key not in ("native_objects", "native_census")}, ensure_ascii=False))
