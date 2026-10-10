# Hosted world protocol (experimental, schema 2)

EF Link 1.2.0-dev adds a host-owned world session. The game thread samples and applies
native state; the network thread only copies bounded packets. The `eflink.world.v1`
channel is reserved by the core and cannot be opened by third-party modules.

## State and authority

The host broadcasts a full map/level snapshot every 200 ms. Map or level changes advance
`epoch`; each broadcast advances `revision`. Late joiners receive a complete current
snapshot. Clients reject snapshots from anyone except peer 1, old epochs, duplicate
revisions, wrong rooms, duplicate entity keys, invalid coordinates, and invalid HP.
Disconnect discards the hosted state. No process addresses or account identifiers are
sent as entity identity. Both clients need 1.2.0-dev for world features.
The dev15 world protocol uses schema 2; schema 1 world packets are rejected. Both
ends need the same world protocol. The transport and public V1/V2 ABI are unchanged.

The listen server owns a player registry separate from transport peers and visual
avatars. It allocates unique entity IDs and sends the registry with each full snapshot.
Scene changes replace player entities and discard native bindings, skill catalogs and
entry grants. Disconnection removes the member. Operator changes invalidate the old
binding. A reserved identity is **not** a created native character: `native_bound=false`
remains explicit. Only a game adapter can bind an actual actor and its host-derived
skill catalog; no GUI setting or network request can do so.

`ready` means a fresh valid snapshot is present; it is **not** world admission.
`session.entry.active` is the separate authority gate. A guest sends `entry_prepare`
with current room/epoch/revision, attempt token and native replica facts. The host sends
`entry_result` for that exact epoch/token only. Map/entity readiness alone cannot pass:
host-owned simulation/rules/combat/logical players and guest isolation/logical players
are mandatory. A member also needs a bound host actor and must acknowledge that
actor's current entity ID. Production currently reports incomplete capabilities, so admission is
blocked honestly. Capability withdrawal, local readiness loss, three-second expiry,
map change and disconnect revoke actions. No configuration toggle can assert authority.

Enemy keys use level logic IDs or script IDs and slots. The native adapter currently
samples at most 512 loaded enemies from `ObjectContainer.allEnemies` (a per-second
scan of every Unity object cost about 25 ms per call and is now only a fallback). It does not equate local instance UIDs or account
server IDs. Missing entities are counted explicitly; a position/HP snapshot cannot
manufacture a complete native enemy. In the latest adapter, private guest enemies are
quarantined separately (AI/controller paused and original models hidden). Room replicas
are allocated from `EnemyInfo` with independent client IDs and native resource loading.
Their resource gate marks only actually ready orders. A room enemy has now been
created on a real guest in the current dungeon; this does not validate every template.
The resource gate checks the native node's complete readiness first: completed orders
can disappear from the factory and should not make a ready node wait forever. Owned
replica nodes enter AOI explicitly; readiness is never fabricated. Diagnostics include
pipeline, AOI and resource readiness to distinguish resource wait from spawn failure.
Quarantine is bounded to 512 loaded enemies and restores only state EF Link borrowed;
it does not suppress every account scene event or replace official server authority.

## Native travel

The player menu and SDK V2 expose player-to-player teleport. The host validates recent
target poses and supplies map, level, numeric level ID, and position. Same-level travel
uses `GameLevelLoader.LoadAtPosInCurrentMap`; different levels use
`GameLevelLoader.LoadAtPos`. Both enqueue the game's own client `LoadingPipeline`
(no server request) with `TeleportParam.uiType=WithLoading`, so entering a room,
following the host to a new map and leaving all show the official loading panel.
Arrival then waits for the requested map and level before applying the destination.
Completion requires an observed native position within 3 m, a started native world, and no delayed mode switch. After a
cross-level overworld load, the adapter runs the idempotent native StartWorld
lifecycle before reporting success. Loading has a 60 s deadline and no infinite
automatic retry. Cross-level travel from/to dungeon or subgame modes is currently
rejected before loading: their complete native exit and hosted entry adapters are
still unfinished. A loaded map alone is not sufficient to leave a dungeon safely.

Guests follow the host's active map. A guest cannot drag the room into another world
by selecting a peer whose map has not caught up. The host may travel to a peer; its
subsequent native map change creates a new epoch for everyone.

On entry a guest records where it stood. Leaving the room (or turning off "follow host
world") sends it back there through the same loading pipeline; the guest's private
enemies are restored first. A dungeon or subgame location falls back to a direct
teleport because its pipeline adapter is unfinished.

Each player owns their own body, as in an official co-op world. Every client
interpolates a remote player's 30 Hz pose stream 100 ms in the past and applies the
position, rotation and Biped bone pose to that player's native room actor every frame,
on the host and on guests alike. The actor's gameplay position (enemy targeting, hit
volumes) is placed on the drawn body with `MovementComponent.TeleportTo`. The 5 Hz
world snapshot is not used for drawing: it froze between packets and then jumped.

Earlier builds steered the host's actor with `CharacterController.MoveTo` at sprint
gait plus a native-physics gate. The sprint overshot a 0.15 m dead band and hunted
around the target, so a standing guest wandered about 1 m on the host. That path and
its `_NeedFixedTick` hook are removed. While the host executes an admitted guest's
native skill, the skill's root motion drives the actor until the cast ends.

## Co-op combat (dev47–55)

Admission is now granted when it is true: the host has set its account enemies aside
for room-owned enemies (`isolated_world`), players have native actors, the host
settles combat, and the scene is the overworld (`scene_rules`; dungeons and subgames
are not taken over). A guest is admitted once its map is loaded, its own private
enemies are quarantined and it has a native actor for the host. The entry handshake,
epochs and revocation rules above are unchanged.

**Players own their attacks.** A guest's attack plays natively on its own client
(animation, hit stop, damage numbers). The damage hook fills the room replica's
health first so the replica never dies locally, measures the native loss and sends a
`hit` (key, amount) to the host. The host accepts hits only from admitted players,
for live room enemies, at most the target's maximum health per hit and 40 per second,
with fresh sequence numbers and the current epoch. It applies them to its
authoritative enemy; zero health runs the native `OnHpZero → _Die → Entity.Die`
path and the death hook keeps a tombstone that hides the enemy for everyone.

**Enemies are owned by the player they fight.** The game's enemy AI (attack tokens,
formation slots, target prediction) is built around a main character; on the host it
would only stand in Battle beside a guest's stand-in actor without attacking. So, as
in official co-op, the host assigns each room enemy an owner: the host by default; a
guest who hits it, unless its current owner is already fighting it; the nearest
player when its owner is not engaged and someone else is 4 m closer; anyone within
60 m when an engaged owner is more than 35 m away. On the owner's machine the enemy's
native AI runs and fights that player's real operator with native damage, hit
reactions and death. The owner reports the enemy's position, facing and engagement
at 10 Hz (`enemy_state`) and its casts (`enemy_cast`). Everyone else keeps that
enemy's AI paused (its controller still ticks): it walks to the reported positions
with `EnemyController.MoveTo` so the enemy's own locomotion animation plays, snaps
only when more than 5 m off, and replays each cast with `TryCastSkill` aimed at the
same player. Damage from a puppet is always blocked. Health and death stay with the
host. A hit from a host-owned enemy on a guest's stand-in actor is measured and
forwarded to that guest (`player_hit`, health floor 1); the stand-in never dies.

Verified on two real clients (map01_lv001): admission without blockers; a scripted
guest's hits killed 8 room enemies through the native death path; room enemies
fought the host's operator (14 casts announced); 6 enemies handed to a real guest
by proximity entered Battle on the guest, damaged and killed its operator natively
(the game switched to the next squad member), and the host replayed their casts and
walked its puppets. Still open: a guest's own native attack reaching the damage hook
(only protocol-level hits from a scripted guest were exercised), stand-in hit
forwarding, enemy effects beyond the replayed cast, hit reactions on puppets, boss
scripts, dungeon co-op, drops/rewards and quests.

Development builds (`EFLINK_NATIVE_DIAGNOSTICS`) add `native_owner_probe`,
`native_travel_probe` and `native_self_cast_probe`. Like `native_skill_probe` they are
queued and run on the game thread; results appear in `world_replication.diagnostics`.

The previous skill-intent executor (host casts a guest's skill on its actor) remains
in the protocol and development diagnostics but is no longer used by players.

## HUD and external integrations

Native camera projection produces normalized screen coordinates and distance. The
injected Dear ImGui renderer draws labels, edge markers and other-map labels through
Unity CanvasRenderer, with UInt16 index chunks. It creates no overlay window or Graphic
raycast target. Input interception is active only while the menu is open; the menu
restores normal input and borrows the cursor only while open.

`EFLink_GetApiV2` preserves V1 and adds copied world JSON and asynchronous teleport
requests. Stable keys, map/level identity and epochs are the boundary for later
integrations. There is no Minecraft connection or MC protocol in this release.

## Architectural reference

Reference: Grasscutters/Grasscutter commit
`f373827a83d3e688fd3c6afb1e1c3759ca1d61c9` (AGPL-3.0), inspected locally under the
ignored `references/` directory. No upstream implementation or game data is copied
into the distribution. Its [World](https://github.com/Grasscutters/Grasscutter/blob/f373827a83d3e688fd3c6afb1e1c3759ca1d61c9/src/main/java/emu/grasscutter/game/world/World.java)
owns scene membership and has distinct server and host-player identities;
[Scene](https://github.com/Grasscutters/Grasscutter/blob/f373827a83d3e688fd3c6afb1e1c3759ca1d61c9/src/main/java/emu/grasscutter/game/world/Scene.java)
owns logical entities. Its ready/init/done entry handlers separate loading from player
and entity appearance. This is an open-source reimplementation, not the official
Genshin server source. EF Link's target is a local listen server on the host computer;
the reference is world ownership and lifecycle, not protocol compatibility.
