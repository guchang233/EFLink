# Source provenance

The protobuf generator, codec and baseline schema are from LR-S, commit
`936df33f69f7af2d2ddf52e650521e2667df0256`:
https://git.xeondev.com/LR/S

Copyright belongs to the original LR-S contributors. Distributed under the
GNU Affero General Public License v3; the original license is included here.

EF Link modifies the baseline schema's `BATTLE_CLIENT_OP_DATA` declaration
to include the payload oneof, reconstructed from the current installed client's
read-only getter types and literal field numbers. The baseline omits these fields.
Do not replace them with unverified packet-parser schemas from another version.

No game data tables, assets, credentials or captured messages are included.

EF Link also corrects signed varint encoding and adds bounded nesting, known-field
wire type validation, invalid tag/varint rejection and truncated-message errors to
the codec. Unknown enum values fail explicitly instead of becoming zero.
The native frame contract in `../../src/Frame.zig` is adapted from LR-S's
`gamesv/src/Session.zig` and `gamesv/src/network.zig` at the same pinned commit.
Ordinary proto3 enum default values are omitted to match the native serializer;
the generator's optional Zig enum representation is not explicit wire presence.
