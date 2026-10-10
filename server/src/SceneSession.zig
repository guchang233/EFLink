// Native scene handshake for a member of a shared Room. A session does not own
// a separate World. The native client bridge and its isolation proof are pending.
const SceneSession = @This();
const std = @import("std");
const Room = @import("room");
const pb = @import("proto").pb;

pub const Begin = struct {
    transition_id: u64,
    change: pb.SC_CHANGE_SCENE_BEGIN_NOTIFY,
    enter: pb.SC_ENTER_SCENE_NOTIFY,
};
pub const Bootstrap = struct {
    transition_id: u64,
    self: pb.SC_SELF_SCENE_INFO,
};
// These values must come from the future trusted native adapter after complete
// unload/load, not from a GUI checkbox or the arrival of a replica snapshot.
pub const NativeApplied = struct {
    transition_id: u64,
    world_id: u64,
    scene_id: u64,
    leader_id: u64,
    private_feed_generation: u64,
};

member_id: Room.MemberId,
world_id: u64,
scene_id: u64 = 0,
transition_id: u64 = 0,
private_feed_generation: u64 = 0,
phase: enum { idle, awaiting_map_load, awaiting_native_bootstrap, active, closed } = .idle,

pub fn init(room: *const Room, member_id: Room.MemberId) !SceneSession {
    if (!room.members.contains(member_id)) return error.InvalidMember;
    return .{ .member_id = member_id, .world_id = room.world_id };
}

pub fn begin(session: *SceneSession, room: *Room, native_role_id: u64, private_feed_generation: u64) !Begin {
    if (session.phase == .closed or session.phase == .awaiting_map_load or session.phase == .awaiting_native_bootstrap)
        return error.TransitionInProgress;
    if (session.world_id != room.world_id or native_role_id == 0 or private_feed_generation == 0)
        return error.WrongWorld;
    const member = room.members.getPtr(session.member_id) orelse return error.InvalidMember;
    const leader = room.entities.getPtr(member.leader_id) orelse return error.MissingLeader;
    if (leader.owner != session.member_id or leader.scene_id != member.scene_id) return error.NotOwned;
    const scene = room.scenes.get(member.scene_id) orelse return error.InvalidScene;
    const position = leader.common().position orelse pb.VECTOR{};
    if (!std.math.isFinite(position.x) or !std.math.isFinite(position.y) or !std.math.isFinite(position.z)) return error.InvalidPosition;
    session.scene_id = scene.id;
    session.transition_id += 1;
    session.private_feed_generation = private_feed_generation;
    session.phase = .awaiting_map_load;
    member.state = .loading;
    return .{
        .transition_id = session.transition_id,
        .change = .{ .scene_num_id = scene.num_id, .position = position, .pass_through_data = .{}, .skip_reenter_map = false },
        .enter = .{ .role_id = native_role_id, .scene_num_id = scene.num_id, .scene_id = scene.id, .position = position, .pass_through_data = .{}, .skip_reenter_map = false },
    };
}

pub fn sceneLoaded(session: *SceneSession, room: *Room, world_id: u64, transition_id: u64, request: pb.CS_SCENE_LOAD_FINISH, allocator: std.mem.Allocator) !Bootstrap {
    if (session.phase != .awaiting_map_load) return error.UnexpectedAcknowledgment;
    try session.checkRoom(room, world_id, transition_id);
    const scene = room.scenes.get(session.scene_id) orelse return error.InvalidScene;
    if (request.scene_num_id != scene.num_id) return error.WrongScene;
    // Loading a map does not mean the native entities, team, scripts and private
    // transport isolation have been installed. No movement is admitted yet.
    const snapshot = try room.bootstrap(session.member_id, allocator);
    session.phase = .awaiting_native_bootstrap;
    return .{ .transition_id = session.transition_id, .self = snapshot };
}

pub fn nativeApplied(session: *SceneSession, room: *Room, proof: NativeApplied) !void {
    if (session.phase != .awaiting_native_bootstrap) return error.UnexpectedAcknowledgment;
    try session.checkRoom(room, proof.world_id, proof.transition_id);
    const member = room.members.get(session.member_id) orelse return error.InvalidMember;
    if (proof.scene_id != session.scene_id or proof.leader_id != member.leader_id or
        proof.private_feed_generation != session.private_feed_generation) return error.NativeStateMismatch;
    try room.acknowledgeLoaded(session.member_id, session.scene_id);
    session.phase = .active;
}

fn checkRoom(session: *const SceneSession, room: *const Room, world_id: u64, transition_id: u64) !void {
    if (world_id != session.world_id or room.world_id != world_id) return error.WrongWorld;
    if (transition_id != session.transition_id) return error.StaleTransition;
    const member = room.members.get(session.member_id) orelse return error.InvalidMember;
    if (member.scene_id != session.scene_id) return error.WrongScene;
}

pub fn close(session: *SceneSession, room: *Room) !void {
    if (session.phase == .closed) return;
    if (room.world_id != session.world_id) return error.WrongWorld;
    try room.removeMember(session.member_id);
    session.phase = .closed;
    // The adapter must separately complete the native home-world lifecycle.
}
