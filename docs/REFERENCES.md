# Endfield-Poser reference review

Reviewed on 2026-10-10: [honxi1/Endfield-Poser](https://github.com/honxi1/Endfield-Poser),
commit `2b9e6a2a1cf9c4a78063352dbcf0c955bba685e5` (AGPL-3.0).
The read-only checkout is under `references/Endfield-Poser`, excluded from
builds and distribution archives. This review does not imply copied code.

Better-Endfield already identifies Poser as a design reference for existing
squad access and shared-origin playback. EF Link's direct review covers:

* `src/game/squad.h`: typed access through the existing GamePlayer and
  SquadManager, slots 1–4, and readiness checks during loading. These are
  existing private-world members, not independent room players.
* `src/core/game_hooks.h`: observe the original PlayerController.SetMainCharacter
  call, preserve its hidden MethodInfo argument, then consume the capture on
  the logic thread. A switch-depth guard prevents writing an outgoing model
  while the game replaces it. This is observation of a game switch, not
  implementation of control over a newly allocated room actor.
* `src/poser.cpp`: restore owned state before discarding an outgoing capture;
  check native object liveness; retry delayed capture after scene changes.
* `src/game/mmd_squad.h`: retain participating objects, save component state
  before changing it, and arm rollback before the first mutation. Restoration
  checks liveness and shutdown state before touching saved objects.

These lifecycle patterns are relevant to EF Link's control leases and cleanup.
Offsets or cached object pointers from Poser must not be assumed valid in a
different game version. EF Link resolves native metadata and checks its own
entity identity leases before mutation.

The inspected functionality does not implement room admission, authoritative
enemy simulation, independent map rules or dungeon replication. Those remain
EF Link responsibilities and need actual two-instance acceptance evidence.
The existing working EF Link Dear ImGui renderer is retained; Poser's separate
overlay window is not adopted.
