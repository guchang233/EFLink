// A single server-owned world. Members never own independent scene/entity stores.
// Native game adapter, full combat engine and live client admission remain pending.
// Callers serialize all access; this registry does not provide its own mutex.
const Room = @This();
const std = @import("std");
const proto = @import("proto");
const pb = proto.pb;
const Allocator = std.mem.Allocator;

pub const MemberId = u16;
pub const Entity = struct {
    pub const Detail = union(enum) {
        character: pb.SCENE_CHARACTER,
        monster: pb.SCENE_MONSTER,
        interactive: pb.SCENE_INTERACTIVE,
        npc: pb.SCENE_NPC,
        summon: pb.SCENE_SUMMON,
    };
    owner: ?MemberId,
    scene_id: u64,
    generation: u32,
    detail: Detail,

    pub fn common(entity: *Entity) *pb.SCENE_OBJECT_COMMON_INFO {
        return switch (entity.detail) {
            inline else => |*detail| &detail.common_info.?,
        };
    }
    pub fn battle(entity: *Entity) ?*pb.BATTLE_MGR_INFO {
        return switch (entity.detail) {
            .npc => null,
            inline else => |*detail| if (detail.battle_info) |*value| value else null,
        };
    }
};
pub const Member = struct {
    state: enum { loading, active, deferred } = .loading,
    scene_id: u64 = 0,
    leader_id: u64 = 0,
    team_change_token: u64 = 1,
    pending_scene_id: u64 = 0,
    follow_deferred: bool = false,
    // Native battle sequence numbers belong to the client's session, not to
    // each character or monster. Switching character/scene cannot reopen them.
    last_battle_sequence: i32 = -1,
};
pub const Scene = struct {
    id: u64,
    num_id: i32,
    // Deep-owned native world context: scripts, drops, areas and scene mode.
    // Per-member team and live entities are rebuilt from the shared registry.
    native_self: ?pb.SC_SELF_SCENE_INFO = null,
};
pub const Error = error{
    InvalidScene,
    InvalidMember,
    WrongScene,
    NotAdmitted,
    NotOwned,
    InvalidEntity,
    StaleGeneration,
    DuplicateSequence,
    ClientSettlementRejected,
    UnsupportedBattleIntent,
    UnknownSkill,
    StaleChoice,
    NotHost,
    TeamFull,
    MissingLeader,
    EntityCollision,
};

gpa: Allocator,
arena: std.heap.ArenaAllocator,
world_id: u64,
scene_id: u64,
scene_num_id: i32,
next_entity_id: u64 = 1,
next_scene_id: u64,
scenes: std.AutoHashMapUnmanaged(u64, Scene) = .empty,
members: std.AutoHashMapUnmanaged(MemberId, Member) = .empty,
entities: std.AutoHashMapUnmanaged(u64, Entity) = .empty,
battle_entities: std.AutoHashMapUnmanaged(u64, u64) = .empty,

pub fn init(gpa: Allocator, world_id: u64, scene_id: u64, scene_num_id: i32) !Room {
    if (world_id == 0 or scene_id == 0 or scene_num_id <= 0) return error.InvalidScene;
    var room: Room = .{ .gpa = gpa, .arena = .init(gpa), .world_id = world_id, .scene_id = scene_id, .scene_num_id = scene_num_id, .next_scene_id = scene_id +% 1 };
    try room.scenes.put(gpa, scene_id, .{ .id = scene_id, .num_id = scene_num_id });
    return room;
}
pub fn deinit(room: *Room) void {
    room.entities.deinit(room.gpa);
    room.battle_entities.deinit(room.gpa);
    room.members.deinit(room.gpa);
    room.scenes.deinit(room.gpa);
    room.arena.deinit();
}
pub fn addMember(room: *Room, id: MemberId) !void {
    if (id == 0 or room.members.contains(id)) return error.InvalidMember;
    try room.members.put(room.gpa, id, .{ .scene_id = room.scene_id });
}
// Called by the future native adapter after its full load acknowledgment. This
// alone does not prove personal transport isolation or satisfy production admission.
pub fn acknowledgeLoaded(room: *Room, id: MemberId, scene_id: u64) Error!void {
    const member = room.members.getPtr(id) orelse return error.InvalidMember;
    if (member.scene_id != scene_id or !room.scenes.contains(scene_id)) return error.WrongScene;
    member.state = .active;
}
fn clone(room: *Room, value: anytype) !@TypeOf(value) {
    var encoded: std.Io.Writer.Allocating = .init(room.gpa);
    defer encoded.deinit();
    // This writer has no I/O failure source; WriteFailed means allocation
    // failed while growing its in-memory buffer.
    proto.encodeMessage(&encoded.writer, value) catch |err|
        return if (err == error.WriteFailed) error.OutOfMemory else err;
    var reader: std.Io.Reader = .fixed(encoded.written());
    return proto.decodeMessage(&reader, room.arena.allocator(), @TypeOf(value));
}
const NativeStage = struct {
    entities: std.AutoHashMapUnmanaged(u64, Entity) = .empty,
    battle_ids: std.AutoHashMapUnmanaged(u64, u64) = .empty,
    fn deinit(stage: *NativeStage, allocator: Allocator) void {
        stage.entities.deinit(allocator);
        stage.battle_ids.deinit(allocator);
    }
    fn clear(stage: *NativeStage) void {
        stage.entities.clearRetainingCapacity();
        stage.battle_ids.clearRetainingCapacity();
    }
};
fn collectNativeObject(room: *Room, scene: Scene, detail: Entity.Detail, staged: *NativeStage) !void {
    const common = switch (detail) {
        inline else => |object| object.common_info orelse return error.InvalidEntity,
    };
    if (common.id == 0 or !std.math.isFinite(common.hp) or common.hp < 0) return error.InvalidEntity;
    switch (detail) {
        .character => if (common.type != 0) return error.InvalidEntity,
        .monster => if (common.type != 3) return error.InvalidEntity,
        else => {}, // Keep native types for families not yet verified in-game.
    }
    if (common.scene_num_id != 0 and common.scene_num_id != scene.num_id) return error.WrongScene;
    for ([_]?pb.VECTOR{ common.position, common.rotation }) |optional| if (optional) |vector| {
        if (!std.math.isFinite(vector.x) or !std.math.isFinite(vector.y) or !std.math.isFinite(vector.z)) return error.InvalidEntity;
    };
    if (staged.entities.contains(common.id)) return error.EntityCollision;
    if (room.entities.get(common.id)) |existing| {
        if (existing.scene_id != scene.id or (existing.owner != null and existing.owner != 1)) return error.EntityCollision;
    }
    var entity: Entity = .{ .owner = if (detail == .character) 1 else null, .scene_id = scene.id, .generation = 0, .detail = detail };
    if (entity.battle()) |battle| {
        entity.generation = battle.msg_generation;
        // No two objects can impersonate a battle reporter in the shared room.
        if (battle.battle_inst_id != 0) {
            if (staged.battle_ids.contains(battle.battle_inst_id)) return error.EntityCollision;
            if (room.battle_entities.get(battle.battle_inst_id)) |id| {
                const other = room.entities.get(id).?;
                if (other.scene_id != scene.id or (other.owner != null and other.owner != 1)) return error.EntityCollision;
            }
            try staged.battle_ids.put(room.gpa, battle.battle_inst_id, common.id);
        }
    }
    try staged.entities.put(room.gpa, common.id, entity);
}
fn collectNative(room: *Room, scene: Scene, detail: pb.SCENE_OBJECT_DETAIL_CONTAINER, staged: *NativeStage) !void {
    for (detail.char_list.items) |object| try room.collectNativeObject(scene, .{ .character = object }, staged);
    for (detail.monster_list.items) |object| try room.collectNativeObject(scene, .{ .monster = object }, staged);
    for (detail.interactive_list.items) |object| try room.collectNativeObject(scene, .{ .interactive = object }, staged);
    for (detail.npc_list.items) |object| try room.collectNativeObject(scene, .{ .npc = object }, staged);
    for (detail.summon_list.items) |object| try room.collectNativeObject(scene, .{ .summon = object }, staged);
}
// Called only by the trusted host adapter. This imports a complete native
// scene, preserving IDs, battle identities and script/mode context. It never
// admits a guest or replaces any guest-owned operator with host account data.
pub fn importHostScene(room: *Room, host_id: MemberId, snapshot: pb.SC_SELF_SCENE_INFO) !void {
    if (host_id != 1 or !room.members.contains(host_id)) return error.NotHost;
    const scene = room.scenes.get(snapshot.scene_id) orelse return error.InvalidScene;
    if (scene.id != room.scene_id or snapshot.scene_num_id != scene.num_id or room.members.get(1).?.scene_id != scene.id) return error.WrongScene;
    const detail = snapshot.detail orelse return error.InvalidEntity;
    const team = snapshot.team_info orelse return error.MissingLeader;
    if (snapshot.scene_impl == null) return error.InvalidScene;
    var staged: NativeStage = .{};
    defer staged.deinit(room.gpa);
    try room.collectNative(scene, detail, &staged);
    const leader = staged.entities.get(team.cur_leader_id) orelse return error.MissingLeader;
    if (leader.owner != 1) return error.NotOwned;
    var removed: std.ArrayList(u64) = .empty;
    defer removed.deinit(room.gpa);
    var existing = room.entities.iterator();
    while (existing.next()) |entry| {
        const entity = entry.value_ptr;
        if (entity.scene_id == scene.id and (entity.owner == null or entity.owner == 1)) try removed.append(room.gpa, entry.key_ptr.*);
    }
    // All validation/allocation precedes publication under the caller's room
    // serialization. A rejected/failed import never publishes a partial batch.
    const owned = try room.clone(snapshot);
    staged.clear();
    try room.collectNative(scene, owned.detail.?, &staged);
    try room.entities.ensureUnusedCapacity(room.gpa, staged.entities.count());
    try room.battle_entities.ensureUnusedCapacity(room.gpa, staged.battle_ids.count());
    for (removed.items) |id| {
        if (room.entities.getPtr(id).?.battle()) |battle| _ = room.battle_entities.remove(battle.battle_inst_id);
        _ = room.entities.remove(id);
    }
    var incoming = staged.entities.iterator();
    while (incoming.next()) |entry| room.entities.putAssumeCapacity(entry.key_ptr.*, entry.value_ptr.*);
    var identities = staged.battle_ids.iterator();
    while (identities.next()) |entry| room.battle_entities.putAssumeCapacity(entry.key_ptr.*, entry.value_ptr.*);
    room.scenes.getPtr(scene.id).?.native_self = owned;
    const member = room.members.getPtr(1).?;
    member.leader_id = team.cur_leader_id;
    member.team_change_token = team.team_change_token;
}
// Templates come from server configuration, never from a guest spawn/HP report.
pub fn spawnCharacter(room: *Room, owner: MemberId, template: pb.SCENE_CHARACTER) !u64 {
    if (!room.members.contains(owner)) return error.InvalidMember;
    if (template.common_info == null or template.battle_info == null) return error.InvalidEntity;
    var count: usize = 0;
    var existing = room.entities.valueIterator();
    while (existing.next()) |entity| if (entity.owner == owner) {
        count += 1;
    };
    if (count >= 4) return error.TeamFull;
    var entity: Entity = .{ .owner = owner, .scene_id = room.members.get(owner).?.scene_id, .generation = 1, .detail = .{ .character = try room.clone(template) } };
    const id = try room.insert(&entity);
    const member = room.members.getPtr(owner).?;
    if (member.leader_id == 0) member.leader_id = id;
    return id;
}
pub fn spawnMonster(room: *Room, template: pb.SCENE_MONSTER) !u64 {
    if (template.common_info == null or template.battle_info == null) return error.InvalidEntity;
    var entity: Entity = .{ .owner = null, .scene_id = room.scene_id, .generation = 1, .detail = .{ .monster = try room.clone(template) } };
    return room.insert(&entity);
}
fn insert(room: *Room, entity: *Entity) !u64 {
    var id = room.next_entity_id;
    // Imported native IDs remain reserved, including IDs near integer wrap.
    while (id == 0 or room.entities.contains(id) or room.battle_entities.contains(id)) id +%= 1;
    const common = entity.common();
    if (!std.math.isFinite(common.hp) or common.hp <= 0) return error.InvalidEntity;
    common.id = id;
    // The installed client's native monster pipeline uses object type 3.
    // Type 1 from the initial synthetic model is not a monster discriminator.
    common.type = switch (entity.detail) {
        .character => 0,
        .monster => 3,
        else => return error.InvalidEntity,
    };
    common.scene_num_id = room.scenes.get(entity.scene_id).?.num_id;
    const battle = entity.battle() orelse return error.InvalidEntity;
    battle.battle_inst_id = id;
    battle.msg_generation = entity.generation;
    try room.entities.ensureUnusedCapacity(room.gpa, 1);
    try room.battle_entities.ensureUnusedCapacity(room.gpa, 1);
    room.entities.putAssumeCapacity(id, entity.*);
    room.battle_entities.putAssumeCapacity(id, id);
    room.next_entity_id = id +% 1;
    return id;
}
pub fn bootstrap(room: *Room, id: MemberId, allocator: Allocator) !pb.SC_SELF_SCENE_INFO {
    const member = room.members.get(id) orelse return error.InvalidMember;
    const scene = room.scenes.get(member.scene_id) orelse return error.InvalidScene;
    var info: pb.SC_SELF_SCENE_INFO = scene.native_self orelse .{ .scene_impl = .{ .empty = .{} } };
    info.scene_num_id = scene.num_id;
    info.scene_id = scene.id;
    info.self_info_reason = @backingInt(pb.SELF_INFO_REASON_TYPE.SLR_ENTER_SCENE);
    info.team_info = .{ .team_type = .MAIN, .cur_leader_id = member.leader_id, .team_change_token = member.team_change_token };
    info.detail = .{};
    var entities = room.entities.valueIterator();
    while (entities.next()) |entity| {
        if (entity.scene_id != scene.id) continue;
        switch (entity.detail) {
            .character => |c| try info.detail.?.char_list.append(allocator, c),
            .monster => |m| try info.detail.?.monster_list.append(allocator, m),
            .interactive => |object| try info.detail.?.interactive_list.append(allocator, object),
            .npc => |object| try info.detail.?.npc_list.append(allocator, object),
            .summon => |object| try info.detail.?.summon_list.append(allocator, object),
        }
    }
    return info;
}
pub fn acceptMovement(room: *Room, member_id: MemberId, scene_id: u64, packet: pb.CS_MOVE_OBJECT_MOVE) !pb.SC_MOVE_OBJECT_MOVE {
    const member = room.members.getPtr(member_id) orelse return error.InvalidMember;
    if (member.state != .active) return error.NotAdmitted;
    if (member.scene_id != scene_id) return error.WrongScene;
    const scene = room.scenes.get(scene_id) orelse return error.InvalidScene;
    // Validate the whole batch before changing any entity (no partial foreign move).
    for (packet.move_info.items) |move| {
        const entity = room.entities.get(move.objid) orelse return error.InvalidEntity;
        if (entity.owner == null or entity.owner.? != member_id) return error.NotOwned;
        if (move.scene_num_id != scene.num_id or entity.scene_id != scene_id) return error.WrongScene;
        const motion = move.motion_info orelse return error.InvalidEntity;
        const position = motion.position orelse return error.InvalidEntity;
        if (!std.math.isFinite(position.x) or !std.math.isFinite(position.y) or !std.math.isFinite(position.z)) return error.InvalidEntity;
    }
    for (packet.move_info.items) |move| {
        const common = room.entities.getPtr(move.objid).?.common();
        common.position = move.motion_info.?.position;
        if (move.motion_info.?.rotation) |rotation| common.rotation = rotation;
    }
    return .{ .move_info = packet.move_info, .server_notify = true };
}
// Validate native skill requests. HP, death, reset and buff claims from guests are
// never treated as server settlement. Resolving accepted casts requires a server
// battle engine; this core intentionally does not claim that engine is implemented.
pub fn validateBattleIntent(room: *Room, member_id: MemberId, scene_id: u64, op: pb.BATTLE_CLIENT_OP_DATA) Error!void {
    const member = room.members.getPtr(member_id) orelse return error.InvalidMember;
    if (member.state != .active) return error.NotAdmitted;
    if (member.scene_id != scene_id) return error.WrongScene;
    const scene = room.scenes.get(scene_id) orelse return error.InvalidScene;
    if (op.chapter_map_id != scene.num_id) return error.WrongScene;
    // Native owner_id is a battle instance, not necessarily the scene object ID.
    const entity = room.battleOwner(op.owner_id) orelse return error.InvalidEntity;
    if (entity.owner == null or entity.owner.? != member_id) return error.NotOwned;
    if (entity.scene_id != scene_id) return error.WrongScene;
    if (op.generation != entity.generation) return error.StaleGeneration;
    if (op.seq_id < 0 or op.seq_id <= member.last_battle_sequence) return error.DuplicateSequence;
    const payload = op.op_data_impl orelse return error.UnsupportedBattleIntent;
    switch (payload) {
        .entity_value_modify_data, .entity_die_op_data, .reset_battle_op_data => return error.ClientSettlementRejected,
        .skill_start_cast_op_data => |optional_cast| {
            const cast = optional_cast orelse return error.UnsupportedBattleIntent;
            if (op.op_type != .BATTLE_OP_SKILL_START_CAST) return error.UnsupportedBattleIntent;
            if (!installedSkill(entity.*, cast.inst_id)) return error.UnknownSkill;
            if (cast.target) |target| for (target.targets.items) |item| {
                if (item.target_id != 0) {
                    const target_entity = room.entities.get(item.target_id) orelse return error.InvalidEntity;
                    if (target_entity.scene_id != scene_id) return error.WrongScene;
                }
            };
        },
        .skill_end_cast_op_data => |optional_cast| {
            const cast = optional_cast orelse return error.UnsupportedBattleIntent;
            if (op.op_type != .BATTLE_OP_SKILL_END_CAST) return error.UnsupportedBattleIntent;
            if (!installedSkill(entity.*, cast.inst_id)) return error.UnknownSkill;
        },
        else => return error.UnsupportedBattleIntent,
    }
    member.last_battle_sequence = op.seq_id;
}

pub fn setLeader(room: *Room, member_id: MemberId, scene_id: u64, leader_id: u64) Error!void {
    const member = room.members.getPtr(member_id) orelse return error.InvalidMember;
    const entity = room.entities.get(leader_id) orelse return error.InvalidEntity;
    if (entity.owner != member_id) return error.NotOwned;
    if (member.scene_id != scene_id or entity.scene_id != scene_id) return error.WrongScene;
    member.leader_id = leader_id;
    member.team_change_token += 1;
}

// Guests who decline stay in the previous scene INSIDE the same server world.
pub fn beginHostScene(room: *Room, host_id: MemberId, scene_num_id: i32) !u64 {
    var id = room.next_scene_id;
    while (id == 0 or room.scenes.contains(id)) id +%= 1;
    return room.beginNativeHostScene(host_id, id, scene_num_id);
}
// The native adapter supplies the actual host scene instance, never a map-name
// alias or a second client-owned scene. Consent still belongs to each member.
pub fn beginNativeHostScene(room: *Room, host_id: MemberId, id: u64, scene_num_id: i32) !u64 {
    if (host_id != 1 or !room.members.contains(host_id)) return error.NotHost;
    if (id == 0 or scene_num_id <= 0 or room.scenes.contains(id)) return error.InvalidScene;
    try room.scenes.put(room.gpa, id, .{ .id = id, .num_id = scene_num_id });
    room.next_scene_id = id +% 1;
    room.scene_id = id;
    room.scene_num_id = scene_num_id;
    room.moveMemberToScene(host_id, id);
    var members = room.members.iterator();
    while (members.next()) |entry| {
        if (entry.key_ptr.* == host_id) continue;
        entry.value_ptr.pending_scene_id = id;
        entry.value_ptr.follow_deferred = false;
    }
    return id;
}
pub fn chooseFollow(room: *Room, id: MemberId, world_id: u64, scene_id: u64, follow: bool) Error!void {
    const member = room.members.getPtr(id) orelse return error.InvalidMember;
    if (world_id != room.world_id or member.pending_scene_id != scene_id or scene_id != room.scene_id)
        return error.StaleChoice;
    if (!follow) {
        member.follow_deferred = true;
        return;
    }
    member.pending_scene_id = 0;
    member.follow_deferred = false;
    room.moveMemberToScene(id, scene_id);
}
fn moveMemberToScene(room: *Room, id: MemberId, scene_id: u64) void {
    const member = room.members.getPtr(id).?;
    member.scene_id = scene_id;
    member.state = .loading;
    member.team_change_token += 1;
    var entities = room.entities.valueIterator();
    while (entities.next()) |entity| {
        if (entity.owner != id) continue;
        entity.scene_id = scene_id;
        entity.generation += 1;
        entity.common().scene_num_id = room.scenes.get(scene_id).?.num_id;
        if (entity.battle()) |battle| battle.msg_generation = entity.generation;
    }
}
pub fn removeMember(room: *Room, id: MemberId) !void {
    if (!room.members.contains(id)) return error.InvalidMember;
    var removed: std.ArrayList(u64) = .empty;
    defer removed.deinit(room.gpa);
    var iterator = room.entities.iterator();
    while (iterator.next()) |entry| if (entry.value_ptr.owner == id) {
        try removed.append(room.gpa, entry.key_ptr.*);
    };
    _ = room.members.remove(id);
    for (removed.items) |entity_id| {
        if (room.entities.getPtr(entity_id).?.battle()) |battle| _ = room.battle_entities.remove(battle.battle_inst_id);
        _ = room.entities.remove(entity_id);
    }
}

fn battleOwner(room: *Room, battle_id: u64) ?*Entity {
    const id = room.battle_entities.get(battle_id) orelse return null;
    return room.entities.getPtr(id);
}
fn installedSkill(entity_value: Entity, id: u64) bool {
    var entity = entity_value;
    const battle = entity.battle() orelse return false;
    for (battle.skill_list.items) |skill| if (skill.inst_id == id and skill.is_enable and !skill.is_passive) return true;
    return false;
}
