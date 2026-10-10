# EF Link native world server core

This is an unfinished server implementation. It does not yet accept real EF
clients, isolate their official gameplay transport, execute full battle/AI logic,
or initialize the complete native scene. It must not report production admission.

`src/Room.zig` stores shared scene instances and an entity registry for all room members. Declining a host's map invitation keeps a member in the old server scene; accepting it requires loading the new scene before movement is admitted.
Members receive the same scene instance and shared object IDs through native
`SC_SELF_SCENE_INFO` structures. `importHostScene` can adopt a trusted host's
native snapshot without renumbering its objects or battle instances. It keeps
characters, monsters, interactives, NPCs, summons, scripts, drops, unlocked areas
and scene mode; guest operators and per-connection battle sequence state remain
separate. `beginNativeHostScene` takes the real host scene instance ID while
retaining per-member follow consent. Callers must serialize all room access.
Ownership, scene/generation/sequence checks
reject foreign movement, stale casts, unknown skills and guest HP/death claims.
Skill intents require a future server battle engine before anything is settled.
Importing a snapshot does not implement interactions, NPC behavior, loot rewards,
dungeon scripts or native client transport. These payloads currently supply the
shared bootstrap context; no real game client consumes it yet.

`src/SceneSession.zig` enforces the native change/load/bootstrap acknowledgment
sequence. A map-load acknowledgment alone never admits movement. Each member's
team uses server-owned leader IDs and each connection has one battle sequence
across character and scene changes. Native battle owners resolve through battle
instance IDs, which may differ from scene object IDs.
The native adapter required to supply lifecycle/isolation evidence is unfinished.

`src/NativeAuthority.zig` exposes the experimental `EFLinkNative_GetApiV1` C ABI
from `eflink-native-authority.dll`; its public contract is
`../include/EndfieldLink/NativeAuthority.h`. It accepts a trusted native host
snapshot, allocates host-approved operator records and queues standard scene
change/enter/bootstrap bodies. Load-finish acknowledgments and native-applied
proofs are checked through `SceneSession`. Inputs are deep copied; polling uses
caller-owned buffers and leaves a short-buffer message queued. Context access
must be serialized. Limits are eight members, 64 queued messages and 1 MiB per
body. Queue publication follows successful encoding, so an encoding failure
does not advance the handshake.

This is an initial kernel interface, not a live server transport or game adapter.
It is not loaded into either EF client. Operator templates require trusted host
approval, and the proof API requires actual game-thread lifecycle evidence; a
network peer's claim is insufficient. Guest removal now closes its scene session,
removes its owned entities/battle indices and purges pending recipient messages;
room-wide transition IDs remain fresh when a member ID is reused. Native
character/account mapping, map follow/return routing, timeout/abort recovery,
live disconnect callbacks and combat execution remain to be connected. Context destruction frees pending
payloads. The DLL does not call native game handlers, redirect sockets or send
official messages.

Build the DLL with `../tools/build-native-server.ps1 -Step authority` (or `all`).
`../tests/native_authority_abi_test.py` loads the actual DLL outside the game and
checks ABI layout, identity preservation, borrowed-input ownership, caller
buffers, message ordering and rejected/stale acknowledgments. These checks use
synthetic protobuf bodies and cannot establish real-client admission.

The `fixtures` build step emits synthetic native messages. Run
`../tools/verify-native-wire.py` with a loaded development native-wire module to
check them through the installed game's protobuf parser. This touches no game
world or transport and cannot establish world authority.

The LR-S protobuf codec/generator is reused under AGPL-3.0. The baseline battle
schema is corrected using current-client metadata; provenance is recorded in
`vendor/lr-proto/NOTICE.md`. No game tables or resources are included.

With Zig 0.17.0, run from this directory:

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = 'D:\EF Link\build\zig-global-cache'
& 'D:\EF Link\build\toolchains\zig-x86_64-windows-0.17.0\zig.exe' build test -Doptimize=safe --cache-dir 'D:\EF Link\build\server-cache' --prefix 'D:\EF Link\build\native-server'
```

Tests verify fixed native protobuf wire fixtures and shared in-memory ownership.
They cannot replace the two-game acceptance criteria in `../docs/SERVER_GOAL.md`.
