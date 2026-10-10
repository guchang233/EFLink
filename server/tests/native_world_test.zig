const std = @import("std");
const proto = @import("proto");
const pb = proto.pb;
const Room = @import("room");
const Frame = @import("frame");
const SceneSession = @import("scene_session");
const testing = std.testing;

test "host native scene retains world IDs, scripts, drops and all native object families" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const previous = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const guest = try room.spawnCharacter(2, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    var name = [_]u8{ 'h', 'o', 's', 't' };
    var skills = [_]pb.SERVER_SKILL{.{ .inst_id = 123, .is_enable = true }};
    var characters = [_]pb.SCENE_CHARACTER{.{ .common_info = .{ .id = 10010, .hp = 300, .scene_num_id = 103 }, .name = &name, .battle_info = .{
        .battle_inst_id = 55501,
        .msg_generation = 7,
        .skill_list = .{ .items = &skills, .capacity = skills.len, .pointer_stability = .{} },
    } }};
    var monsters = [_]pb.SCENE_MONSTER{.{ .common_info = .{ .id = 10110, .type = 3, .hp = 500, .scene_num_id = 103 }, .battle_info = .{ .battle_inst_id = 55502, .msg_generation = 9 } }};
    var interactives = [_]pb.SCENE_INTERACTIVE{.{ .common_info = .{ .id = 10210, .hp = 0, .scene_num_id = 103 }, .meta = 4 }};
    var npcs = [_]pb.SCENE_NPC{.{ .common_info = .{ .id = 10310, .hp = 0, .scene_num_id = 103 }, .last_interact_time_stamp = 42 }};
    var summons = [_]pb.SCENE_SUMMON{.{ .common_info = .{ .id = 10410, .hp = 10, .scene_num_id = 103 }, .battle_info = .{ .battle_inst_id = 55503, .msg_generation = 5 } }};
    var scripts = [_]pb.LEVEL_SCRIPT_INFO{.{ .script_id = 80001, .stage = 4, .state = 2 }};
    var areas = [_][]const u8{"synthetic_area"};
    var drops = [_]pb.SCENE_DROP{.{ .drop_id = 81001 }};
    const snapshot: pb.SC_SELF_SCENE_INFO = .{
        .scene_id = 7001,
        .scene_num_id = 103,
        .team_info = .{ .cur_leader_id = 10010, .team_change_token = 77 },
        .scene_impl = .{ .dungeon = .{ .dungeon_id = "synthetic_dungeon", .is_pass = true } },
        .detail = .{
            .char_list = .{ .items = &characters, .capacity = characters.len, .pointer_stability = .{} },
            .monster_list = .{ .items = &monsters, .capacity = monsters.len, .pointer_stability = .{} },
            .interactive_list = .{ .items = &interactives, .capacity = interactives.len, .pointer_stability = .{} },
            .npc_list = .{ .items = &npcs, .capacity = npcs.len, .pointer_stability = .{} },
            .summon_list = .{ .items = &summons, .capacity = summons.len, .pointer_stability = .{} },
        },
        .level_scripts = .{ .items = &scripts, .capacity = scripts.len, .pointer_stability = .{} },
        .unlock_area = .{ .items = &areas, .capacity = areas.len, .pointer_stability = .{} },
        .drop_list = .{ .items = &drops, .capacity = drops.len, .pointer_stability = .{} },
    };
    try room.importHostScene(1, snapshot);
    name[0] = 'X';
    scripts[0].stage = 99;
    try testing.expect(!room.entities.contains(previous) and room.entities.contains(guest));
    try testing.expectEqual(@as(u32, 9), room.entities.get(10110).?.generation);
    try testing.expectEqual(@as(u64, 55502), room.entities.getPtr(10110).?.battle().?.battle_inst_id);
    try testing.expect(!room.entities.contains(55502));
    // Importing a registry never provides native lifecycle/isolation proof.
    try testing.expect(room.members.get(2).?.state == .loading);
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const host_view = try room.bootstrap(1, arena.allocator());
    const guest_view = try room.bootstrap(2, arena.allocator());
    try testing.expectEqual(@as(u64, 10010), host_view.team_info.?.cur_leader_id);
    try testing.expectEqual(guest, guest_view.team_info.?.cur_leader_id);
    try testing.expectEqual(@as(u64, 10110), guest_view.detail.?.monster_list.items[0].common_info.?.id);
    try testing.expectEqualStrings("host", room.entities.get(10010).?.detail.character.name);
    try testing.expectEqual(@as(i32, 4), guest_view.level_scripts.items[0].stage);
    try testing.expectEqualStrings("synthetic_area", guest_view.unlock_area.items[0]);
    try testing.expectEqual(@as(u64, 81001), guest_view.drop_list.items[0].drop_id);
    try testing.expectEqualStrings("synthetic_dungeon", guest_view.scene_impl.?.dungeon.?.dungeon_id);
    try testing.expectEqual(@as(usize, 1), guest_view.detail.?.interactive_list.items.len);
    try testing.expectEqual(@as(usize, 1), guest_view.detail.?.npc_list.items.len);
    try testing.expectEqual(@as(usize, 1), guest_view.detail.?.summon_list.items.len);
    room.next_entity_id = 10110;
    const spawned = try room.spawnMonster(.{ .common_info = .{ .hp = 10 }, .battle_info = .{} });
    try testing.expectEqual(@as(u64, 10111), spawned);
    room.next_entity_id = 55502;
    try testing.expectEqual(@as(u64, 55504), try room.spawnMonster(.{ .common_info = .{ .hp = 10 }, .battle_info = .{} }));
    try room.acknowledgeLoaded(1, 7001);
    try room.validateBattleIntent(1, 7001, .{
        .owner_id = 55501,
        .generation = 7,
        .chapter_map_id = 103,
        .seq_id = 8,
        .op_type = .BATTLE_OP_SKILL_START_CAST,
        .op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } },
    });
    // A later full host snapshot retires stale world objects and their battle
    // lookup entries, without retiring a guest's operator or sequence space.
    var refresh = snapshot;
    refresh.detail.?.monster_list = .empty;
    refresh.detail.?.summon_list = .empty;
    try room.importHostScene(1, refresh);
    try testing.expect(!room.entities.contains(10110) and !room.battle_entities.contains(55502));
    try testing.expect(room.entities.contains(guest) and room.battle_entities.contains(guest));
    try testing.expectEqual(@as(i32, 8), room.members.get(1).?.last_battle_sequence);
}

test "native host scene import rejects guest authority and collisions without partial publication" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const host = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const guest = try room.spawnCharacter(2, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    var characters = [_]pb.SCENE_CHARACTER{.{ .common_info = .{ .id = 100, .hp = 300 }, .battle_info = .{ .battle_inst_id = 500 } }};
    var monsters = [_]pb.SCENE_MONSTER{.{ .common_info = .{ .id = guest, .type = 3, .hp = 500 }, .battle_info = .{ .battle_inst_id = 501 } }};
    var snapshot: pb.SC_SELF_SCENE_INFO = .{ .scene_id = 7001, .scene_num_id = 103, .team_info = .{ .cur_leader_id = 100 }, .scene_impl = .{ .empty = .{} }, .detail = .{
        .char_list = .{ .items = &characters, .capacity = characters.len, .pointer_stability = .{} },
        .monster_list = .{ .items = &monsters, .capacity = monsters.len, .pointer_stability = .{} },
    } };
    try testing.expectError(error.NotHost, room.importHostScene(2, snapshot));
    try testing.expectError(error.EntityCollision, room.importHostScene(1, snapshot));
    try testing.expect(room.entities.contains(host) and room.entities.contains(guest) and !room.entities.contains(100));
    try testing.expect(room.scenes.get(7001).?.native_self == null);
    monsters[0].common_info.?.id = 100;
    try testing.expectError(error.EntityCollision, room.importHostScene(1, snapshot));
    monsters[0].common_info.?.id = 101;
    monsters[0].battle_info.?.battle_inst_id = 500;
    try testing.expectError(error.EntityCollision, room.importHostScene(1, snapshot));
    monsters[0].battle_info.?.battle_inst_id = guest;
    try testing.expectError(error.EntityCollision, room.importHostScene(1, snapshot));
    monsters[0].battle_info.?.battle_inst_id = 501;
    monsters[0].common_info.?.type = 1;
    try testing.expectError(error.InvalidEntity, room.importHostScene(1, snapshot));
    monsters[0].common_info.?.type = 3;
    snapshot.scene_num_id = 104;
    try testing.expectError(error.WrongScene, room.importHostScene(1, snapshot));
    snapshot.scene_num_id = 103;
    snapshot.team_info.?.cur_leader_id = guest;
    try testing.expectError(error.MissingLeader, room.importHostScene(1, snapshot));
    try testing.expectEqual(@as(u32, 2), room.entities.count());
    try testing.expectEqual(host, room.members.get(1).?.leader_id);
}

fn nativeImportAllocationFailure(allocator: std.mem.Allocator) !void {
    var room = try Room.init(allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const host = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const guest = try room.spawnCharacter(2, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    var characters = [_]pb.SCENE_CHARACTER{.{ .common_info = .{ .id = 100, .hp = 300 }, .battle_info = .{ .battle_inst_id = 500 } }};
    var monsters = [_]pb.SCENE_MONSTER{.{ .common_info = .{ .id = 101, .type = 3, .hp = 500 }, .battle_info = .{ .battle_inst_id = 501 } }};
    const snapshot: pb.SC_SELF_SCENE_INFO = .{ .scene_id = 7001, .scene_num_id = 103, .team_info = .{ .cur_leader_id = 100 }, .scene_impl = .{ .empty = .{} }, .detail = .{
        .char_list = .{ .items = &characters, .capacity = characters.len, .pointer_stability = .{} },
        .monster_list = .{ .items = &monsters, .capacity = monsters.len, .pointer_stability = .{} },
    } };
    room.importHostScene(1, snapshot) catch |err| {
        try testing.expect(room.entities.contains(host) and room.entities.contains(guest));
        try testing.expectEqual(@as(u32, 2), room.entities.count());
        try testing.expectEqual(@as(u32, 2), room.battle_entities.count());
        try testing.expect(room.scenes.get(7001).?.native_self == null);
        try testing.expectEqual(host, room.members.get(1).?.leader_id);
        return err;
    };
    try testing.expect(!room.entities.contains(host) and room.entities.contains(guest));
    try testing.expectEqual(@as(u32, 3), room.entities.count());
}

test "native scene import allocation failure never partially changes the shared registry" {
    try testing.checkAllAllocationFailures(testing.allocator, nativeImportAllocationFailure, .{});
}

test "native scene instance IDs preserve per-member map consent and allocator wrap" {
    var room = try Room.init(testing.allocator, 9001, std.math.maxInt(u64), 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const next = try room.beginNativeHostScene(1, 88001, 104);
    try testing.expectEqual(@as(u64, 88001), next);
    try testing.expectEqual(@as(u64, std.math.maxInt(u64)), room.members.get(2).?.scene_id);
    try room.chooseFollow(2, 9001, next, false);
    try testing.expectEqual(@as(u64, std.math.maxInt(u64)), room.members.get(2).?.scene_id);
    try room.chooseFollow(2, 9001, next, true);
    try testing.expectEqual(next, room.members.get(2).?.scene_id);
    try testing.expectError(error.InvalidScene, room.beginNativeHostScene(1, next, 105));
    room.next_scene_id = std.math.maxInt(u64);
    try testing.expectEqual(@as(u64, 1), try room.beginHostScene(1, 105));
    room.next_entity_id = std.math.maxInt(u64);
    _ = try room.spawnMonster(.{ .common_info = .{ .hp = 50 }, .battle_info = .{} });
    try testing.expectEqual(@as(u64, 1), try room.spawnMonster(.{ .common_info = .{ .hp = 50 }, .battle_info = .{} }));
}

test "ordinary proto3 enum default is omitted as by the native protobuf serializer" {
    var output: std.Io.Writer.Allocating = .init(testing.allocator);
    defer output.deinit();
    try proto.encodeMessage(&output.writer, pb.SCD_SCENE_TEAM_INFO{ .team_type = .MAIN, .team_change_token = 1, .cur_leader_id = 2 });
    try testing.expectEqualSlices(u8, &.{ 24, 1, 32, 2 }, output.written());
}

test "native map loaded does not admit movement until matching world bootstrap and isolation proof" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const host_actor = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const guest_actor = try room.spawnCharacter(2, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    var session = try SceneSession.init(&room, 2);
    const begin = try session.begin(&room, 2, 17);
    try testing.expectEqual(@as(u64, 7001), begin.enter.scene_id);
    try testing.expect(!begin.change.skip_reenter_map and !begin.enter.skip_reenter_map);
    try testing.expectError(error.TransitionInProgress, session.begin(&room, 2, 18));
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    try testing.expectError(error.StaleTransition, session.sceneLoaded(&room, 9001, begin.transition_id + 1, .{ .scene_num_id = 103 }, arena.allocator()));
    try testing.expectError(error.WrongWorld, session.sceneLoaded(&room, 9002, begin.transition_id, .{ .scene_num_id = 103 }, arena.allocator()));
    const snapshot = try session.sceneLoaded(&room, 9001, begin.transition_id, .{ .scene_num_id = 103 }, arena.allocator());
    try testing.expectEqual(guest_actor, snapshot.self.team_info.?.cur_leader_id);
    try testing.expect(room.members.get(2).?.state == .loading);
    const proof: SceneSession.NativeApplied = .{ .transition_id = begin.transition_id, .world_id = 9001, .scene_id = 7001, .leader_id = guest_actor, .private_feed_generation = 17 };
    var wrong = proof;
    wrong.leader_id = host_actor;
    try testing.expectError(error.NativeStateMismatch, session.nativeApplied(&room, wrong));
    wrong = proof;
    wrong.private_feed_generation = 16;
    try testing.expectError(error.NativeStateMismatch, session.nativeApplied(&room, wrong));
    try session.nativeApplied(&room, proof);
    try testing.expect(room.members.get(2).?.state == .active);
    try session.close(&room);
    try testing.expect(room.entities.contains(host_actor) and !room.entities.contains(guest_actor));
}

test "native frame routes battle payload with matching sequences and rejects corruption" {
    var output: std.Io.Writer.Allocating = .init(testing.allocator);
    defer output.deinit();
    var ops = [_]pb.BATTLE_CLIENT_OP_DATA{.{ .owner_id = 42, .op_type = .BATTLE_OP_SKILL_START_CAST, .op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } } }};
    const message: pb.CS_BATTLE_OP = .{ .client_data = .{ .op_list = .{
        .items = &ops,
        .capacity = ops.len,
        .pointer_stability = .{},
    } } };
    try Frame.encode(&output.writer, message, 21, 43);
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    var reader: std.Io.Reader = .fixed(output.written());
    const packet = try Frame.read(&reader, arena.allocator());
    try testing.expectEqual(@as(i32, 901), packet.head.msgid);
    try testing.expectEqual(@as(u64, 21), packet.head.up_seqid);
    try testing.expectEqual(@as(u64, 43), packet.head.down_seqid);
    var body: std.Io.Reader = .fixed(packet.body);
    const decoded = try proto.decodeMessage(&body, arena.allocator(), pb.CS_BATTLE_OP);
    try testing.expectEqual(@as(u64, 123), decoded.client_data.?.op_list.items[0].op_data_impl.?.skill_start_cast_op_data.?.inst_id);
    const bytes = output.written();
    bytes[bytes.len - 1] ^= 1;
    var corrupted: std.Io.Reader = .fixed(bytes);
    try testing.expectError(error.ChecksumMismatch, Frame.read(&corrupted, arena.allocator()));
}

test "truncated protobuf, incorrect known wire types and zero field tags are rejected" {
    var truncated: std.Io.Reader = .fixed(&.{ 8, 128 });
    try testing.expectError(error.EndOfStream, proto.decodeMessage(&truncated, testing.allocator, pb.CSHead));
    var incorrect: std.Io.Reader = .fixed(&.{ 10, 0 });
    try testing.expectError(error.MalformedProtobuf, proto.decodeMessage(&incorrect, testing.allocator, pb.CSHead));
    var zero: std.Io.Reader = .fixed(&.{0});
    try testing.expectError(error.MalformedProtobuf, proto.decodeMessage(&zero, testing.allocator, pb.CSHead));
}

test "negative signed protobuf values retain native int32 semantics" {
    var encoded: std.Io.Writer.Allocating = .init(testing.allocator);
    defer encoded.deinit();
    try proto.encodeMessage(&encoded.writer, pb.BATTLE_CLIENT_OP_DATA{ .seq_id = -1 });
    const golden = [_]u8{ 40, 255, 255, 255, 255, 255, 255, 255, 255, 255, 1 };
    try testing.expectEqualSlices(u8, &golden, encoded.written());
    var reader: std.Io.Reader = .fixed(&golden);
    const decoded = try proto.decodeMessage(&reader, testing.allocator, pb.BATTLE_CLIENT_OP_DATA);
    try testing.expectEqual(@as(i32, -1), decoded.seq_id);
}

test "battle damage oneof survives native wire encoding; not just opcode header" {
    const packet: pb.BATTLE_CLIENT_OP_DATA = .{
        .owner_id = 42,
        .op_type = .BATTLE_OP_ENTITY_VALUE_MODIFY,
        .generation = 1,
        .chapter_map_id = 103,
        .seq_id = 7,
        .op_data_impl = .{ .entity_value_modify_data = .{
            .entity_inst_id = 501,
            .scene_num_id = 103,
            .value = .{ .hp = 100 },
            .delta_value = -25,
        } },
    };
    var encoded: std.Io.Writer.Allocating = .init(testing.allocator);
    defer encoded.deinit();
    try proto.encodeMessage(&encoded.writer, packet);
    // Fixed protobuf wire fixture: field 111 contains the real typed damage data.
    const golden = [_]u8{ 8, 42, 16, 11, 24, 1, 32, 103, 40, 7, 250, 6, 25, 8, 245, 3, 16, 103, 26, 9, 9, 0, 0, 0, 0, 0, 0, 89, 64, 57, 0, 0, 0, 0, 0, 0, 57, 192 };
    try testing.expectEqualSlices(u8, &golden, encoded.written());
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    var reader: std.Io.Reader = .fixed(&golden);
    const decoded = try proto.decodeMessage(&reader, arena.allocator(), pb.BATTLE_CLIENT_OP_DATA);
    try testing.expectEqual(@as(f64, -25), decoded.op_data_impl.?.entity_value_modify_data.?.delta_value);
    try testing.expectEqual(@as(f64, 100), decoded.op_data_impl.?.entity_value_modify_data.?.value.?.hp);
}

test "two members bootstrap the same world and unique server objects, leaving preserves enemies" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const first = try room.spawnCharacter(1, .{ .common_info = .{ .id = 99, .templateid = "char_test", .hp = 300 }, .battle_info = .{} });
    const second = try room.spawnCharacter(2, .{ .common_info = .{ .id = 99, .templateid = "char_test", .hp = 300 }, .battle_info = .{} });
    const monster = try room.spawnMonster(.{ .common_info = .{ .id = 99, .templateid = "enemy_test", .hp = 500 }, .battle_info = .{} });
    try testing.expect(first != second and first != monster and second != monster);
    var arena: std.heap.ArenaAllocator = .init(testing.allocator);
    defer arena.deinit();
    const host = try room.bootstrap(1, arena.allocator());
    const guest = try room.bootstrap(2, arena.allocator());
    try testing.expectEqual(host.scene_id, guest.scene_id);
    try testing.expectEqual(first, host.team_info.?.cur_leader_id);
    try testing.expectEqual(second, guest.team_info.?.cur_leader_id);
    try testing.expectEqual(@as(usize, 2), guest.detail.?.char_list.items.len);
    // This discriminator is used by the real client, not just by the parser.
    try testing.expectEqual(@as(i32, 3), guest.detail.?.monster_list.items[0].common_info.?.type);
    try testing.expectEqual(host.detail.?.monster_list.items[0].common_info.?.id, guest.detail.?.monster_list.items[0].common_info.?.id);
    try room.removeMember(2);
    try testing.expect(room.entities.contains(first) and room.entities.contains(monster));
    try testing.expect(!room.entities.contains(second));
}

test "map refusal stays in the host server's old scene; stale consent cannot follow a newer map" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    try room.acknowledgeLoaded(1, 7001);
    try room.acknowledgeLoaded(2, 7001);
    const host_actor = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const guest_actor = try room.spawnCharacter(2, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    const old_monster = try room.spawnMonster(.{ .common_info = .{ .hp = 500 }, .battle_info = .{} });
    const new_scene = try room.beginHostScene(1, 104);
    try room.chooseFollow(2, 9001, new_scene, false);
    try testing.expectEqual(@as(u64, 7001), room.members.get(2).?.scene_id);
    try testing.expectEqual(@as(u64, 7001), room.entities.get(guest_actor).?.scene_id);
    try testing.expectEqual(@as(u64, 7001), room.entities.get(old_monster).?.scene_id);
    try testing.expectEqual(new_scene, room.entities.get(host_actor).?.scene_id);
    const latest = try room.beginHostScene(1, 105);
    try testing.expectError(error.StaleChoice, room.chooseFollow(2, 9001, new_scene, true));
    try testing.expectError(error.StaleChoice, room.chooseFollow(2, 9002, latest, true));
    try room.chooseFollow(2, 9001, latest, true);
    try testing.expectEqual(latest, room.entities.get(guest_actor).?.scene_id);
    try testing.expectEqual(@as(u64, 7001), room.entities.get(old_monster).?.scene_id);
    try testing.expectEqual(@as(u32, 2), room.entities.get(guest_actor).?.generation);
    try testing.expectError(error.WrongScene, room.acknowledgeLoaded(2, new_scene));
    try room.acknowledgeLoaded(2, latest);
}

test "guest cannot move another member, enemies or stale scene objects" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    const own = try room.spawnCharacter(2, .{ .common_info = .{ .templateid = "char_test", .hp = 300 }, .battle_info = .{} });
    const other = try room.spawnCharacter(1, .{ .common_info = .{ .templateid = "char_test", .hp = 300 }, .battle_info = .{} });
    var moves = [_]pb.MOVE_OBJECT_MOVE_INFO{.{ .objid = own, .scene_num_id = 103, .motion_info = .{ .position = .{ .x = 20, .y = 0, .z = 5 } } }};
    const packet: pb.CS_MOVE_OBJECT_MOVE = .{ .move_info = .{ .items = &moves, .capacity = moves.len, .pointer_stability = .{} } };
    try testing.expectError(error.NotAdmitted, room.acceptMovement(2, 7001, packet));
    try room.acknowledgeLoaded(2, 7001);
    try testing.expectError(error.WrongScene, room.acceptMovement(2, 7000, packet));
    moves[0].objid = other;
    try testing.expectError(error.NotOwned, room.acceptMovement(2, 7001, packet));
    moves[0].objid = own;
    _ = try room.acceptMovement(2, 7001, packet);
    try testing.expectEqual(@as(f32, 20), room.entities.getPtr(own).?.common().position.?.x);
}

test "guest HP and death claims are refused; skill requests enforce ownership, generation and sequence" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(2);
    try room.acknowledgeLoaded(2, 7001);
    var skills = [_]pb.SERVER_SKILL{.{ .inst_id = 123, .is_enable = true }};
    const own = try room.spawnCharacter(2, .{ .common_info = .{ .templateid = "char_test", .hp = 300 }, .battle_info = .{ .skill_list = .{ .items = &skills, .capacity = skills.len, .pointer_stability = .{} } } });
    var op: pb.BATTLE_CLIENT_OP_DATA = .{ .owner_id = own, .generation = 1, .chapter_map_id = 103, .seq_id = 7, .op_type = .BATTLE_OP_ENTITY_VALUE_MODIFY, .op_data_impl = .{ .entity_value_modify_data = .{ .value = .{ .hp = 0 } } } };
    try testing.expectError(error.ClientSettlementRejected, room.validateBattleIntent(2, 7001, op));
    op.op_type = .BATTLE_OP_SKILL_START_CAST;
    op.op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } };
    op.generation = 0;
    try testing.expectError(error.StaleGeneration, room.validateBattleIntent(2, 7001, op));
    op.generation = 1;
    op.op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 666 } };
    try testing.expectError(error.UnknownSkill, room.validateBattleIntent(2, 7001, op));
    op.op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } };
    try room.validateBattleIntent(2, 7001, op);
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, 7001, op));
    try testing.expectEqual(@as(f64, 300), room.entities.getPtr(own).?.common().hp);
}

test "battle sequence belongs to a member across character and scene changes" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    var skills = [_]pb.SERVER_SKILL{.{ .inst_id = 123, .is_enable = true }};
    const template: pb.SCENE_CHARACTER = .{ .common_info = .{ .hp = 300 }, .battle_info = .{
        .skill_list = .{ .items = &skills, .capacity = skills.len, .pointer_stability = .{} },
    } };
    const host = try room.spawnCharacter(1, template);
    const first = try room.spawnCharacter(2, template);
    const second = try room.spawnCharacter(2, template);
    try room.acknowledgeLoaded(1, 7001);
    try room.acknowledgeLoaded(2, 7001);
    var op: pb.BATTLE_CLIENT_OP_DATA = .{
        .owner_id = first,
        .generation = 1,
        .chapter_map_id = 103,
        .seq_id = 7,
        .op_type = .BATTLE_OP_SKILL_START_CAST,
        .op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } },
    };
    try room.validateBattleIntent(2, 7001, op);
    op.owner_id = second;
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, 7001, op));
    op.seq_id = 6;
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, 7001, op));
    // A different connection has its own sequence space.
    op.owner_id = host;
    op.seq_id = 7;
    try room.validateBattleIntent(1, 7001, op);
    op.owner_id = second;
    op.seq_id = 8;
    try room.validateBattleIntent(2, 7001, op);
    const next = try room.beginHostScene(1, 104);
    try room.chooseFollow(2, 9001, next, true);
    try room.acknowledgeLoaded(2, next);
    op.chapter_map_id = 104;
    op.generation = 2;
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, next, op));
    // An invalid request must not advance the accepted sequence watermark.
    op.seq_id = 100;
    op.op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 999 } };
    try testing.expectError(error.UnknownSkill, room.validateBattleIntent(2, next, op));
    op.seq_id = 9;
    op.op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } };
    try room.validateBattleIntent(2, next, op);
}

test "team leaders cannot be stolen and character switching retains the battle sequence" {
    var room = try Room.init(testing.allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    try room.acknowledgeLoaded(2, 7001);
    const foreign = try room.spawnCharacter(1, .{ .common_info = .{ .hp = 300 }, .battle_info = .{} });
    var skills = [_]pb.SERVER_SKILL{.{ .inst_id = 123, .is_enable = true }};
    const template: pb.SCENE_CHARACTER = .{ .common_info = .{ .hp = 300 }, .battle_info = .{ .skill_list = .{ .items = &skills, .capacity = skills.len, .pointer_stability = .{} } } };
    const first = try room.spawnCharacter(2, template);
    const second = try room.spawnCharacter(2, template);
    try testing.expectError(error.NotOwned, room.setLeader(2, 7001, foreign));
    try room.setLeader(2, 7001, second);
    var op: pb.BATTLE_CLIENT_OP_DATA = .{ .owner_id = first, .generation = 1, .chapter_map_id = 103, .seq_id = 7, .op_type = .BATTLE_OP_SKILL_START_CAST, .op_data_impl = .{ .skill_start_cast_op_data = .{ .inst_id = 123 } } };
    try room.validateBattleIntent(2, 7001, op);
    op.owner_id = second;
    op.seq_id = 1;
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, 7001, op));
    op.seq_id = 8;
    try room.validateBattleIntent(2, 7001, op);
    try testing.expectError(error.DuplicateSequence, room.validateBattleIntent(2, 7001, op));
    _ = try room.spawnCharacter(2, template);
    _ = try room.spawnCharacter(2, template);
    try testing.expectError(error.TeamFull, room.spawnCharacter(2, template));
}
