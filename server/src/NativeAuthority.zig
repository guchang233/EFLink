// C ABI for the shared kernel. No game calls, sockets or login interception.
// The trusted caller serializes context access and supplies native lifecycle
// proofs; this DLL alone cannot establish real client/world admission.
const std = @import("std");
const proto = @import("proto");
const pb = proto.pb;
const Room = @import("room");
const SceneSession = @import("scene_session");
const allocator = std.heap.page_allocator;
const max_body = 1024 * 1024;
const max_queue = 64;
const ok: i32 = 0;
const invalid: i32 = 1;
const rejected: i32 = 2;
const oom: i32 = 3;
const short_buffer: i32 = 4;
const empty: i32 = 5;

const Message = extern struct {
    world_id: u64,
    transition_id: u64,
    message_id: u32,
    size: u32,
    recipient: u16,
    reserved: u16 = 0,
    reserved2: u32 = 0,
};
const Proof = extern struct {
    transition_id: u64,
    world_id: u64,
    scene_id: u64,
    leader_id: u64,
    private_feed_generation: u64,
};
const Queued = struct { head: Message, bytes: []u8 };
const Context = struct {
    room: Room,
    sessions: std.AutoHashMapUnmanaged(u16, SceneSession) = .empty,
    queue: std.ArrayList(Queued) = .empty,
    last_transition_id: u64 = 0,
};
fn failure(err: anyerror) i32 {
    return if (err == error.OutOfMemory or err == error.WriteFailed) oom else rejected;
}
fn decode(comptime T: type, data: ?[*]const u8, size: u32, arena: *std.heap.ArenaAllocator) !T {
    if (data == null or size == 0 or size > max_body) return error.InvalidInput;
    var reader: std.Io.Reader = .fixed(data.?[0..size]);
    return proto.decodeMessage(&reader, arena.allocator(), T);
}
fn encode(ctx: *Context, recipient: u16, transition: u64, value: anytype) !Queued {
    var writer: std.Io.Writer.Allocating = .init(allocator);
    defer writer.deinit();
    try proto.encodeMessage(&writer.writer, value);
    if (writer.written().len > max_body) return error.TooLarge;
    const bytes = try allocator.dupe(u8, writer.written());
    return .{ .bytes = bytes, .head = .{ .world_id = ctx.room.world_id, .transition_id = transition, .recipient = recipient, .message_id = @intCast(@backingInt(proto.messageId(@TypeOf(value)))), .size = @intCast(bytes.len) } };
}
fn create(world: u64, scene: u64, num: i32, out: ?*?*Context) callconv(.c) i32 {
    const target = out orelse return invalid;
    target.* = null;
    if (world == 0 or scene == 0 or num <= 0) return invalid;
    const ctx = allocator.create(Context) catch return oom;
    ctx.* = .{ .room = Room.init(allocator, world, scene, num) catch |err| {
        allocator.destroy(ctx);
        return failure(err);
    } };
    target.* = ctx;
    return ok;
}
fn destroy(value: ?*Context) callconv(.c) void {
    const ctx = value orelse return;
    for (ctx.queue.items) |item| allocator.free(item.bytes);
    ctx.queue.deinit(allocator);
    ctx.sessions.deinit(allocator);
    ctx.room.deinit();
    allocator.destroy(ctx);
}
fn addMember(value: ?*Context, member: u16) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    if (member == 0 or ctx.room.members.count() >= 8) return invalid;
    ctx.sessions.ensureUnusedCapacity(allocator, 1) catch return oom;
    ctx.room.addMember(member) catch |err| return failure(err);
    ctx.sessions.putAssumeCapacity(member, SceneSession.init(&ctx.room, member) catch unreachable);
    return ok;
}
fn importHost(value: ?*Context, member: u16, data: ?[*]const u8, size: u32) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    if (member != 1) return rejected;
    // A bootstrap already queued/in flight cannot be silently replaced.
    var sessions = ctx.sessions.valueIterator();
    while (sessions.next()) |session| if (session.phase != .idle) return rejected;
    var arena: std.heap.ArenaAllocator = .init(allocator);
    defer arena.deinit();
    const snapshot = decode(pb.SC_SELF_SCENE_INFO, data, size, &arena) catch |err| return failure(err);
    ctx.room.importHostScene(member, snapshot) catch |err| return failure(err);
    return ok;
}
fn allocateOperator(value: ?*Context, member: u16, data: ?[*]const u8, size: u32, out: ?*u64) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    const target = out orelse return invalid;
    target.* = 0;
    const session = ctx.sessions.get(member) orelse return rejected;
    if (session.phase != .idle) return rejected;
    var arena: std.heap.ArenaAllocator = .init(allocator);
    defer arena.deinit();
    const character = decode(pb.SCENE_CHARACTER, data, size, &arena) catch |err| return failure(err);
    target.* = ctx.room.spawnCharacter(member, character) catch |err| return failure(err);
    return ok;
}
fn beginScene(value: ?*Context, member: u16, role_id: u64, generation: u64, out: ?*u64) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    const target = out orelse return invalid;
    target.* = 0;
    const session = ctx.sessions.getPtr(member) orelse return rejected;
    if (ctx.last_transition_id == std.math.maxInt(u64)) return rejected;
    if (ctx.queue.items.len + 2 > max_queue) return rejected;
    ctx.queue.ensureUnusedCapacity(allocator, 2) catch return oom;
    var next = session.*;
    // Tokens belong to the room context, not the reusable member ID. A late
    // reply from a disconnected connection cannot match the next connection.
    next.transition_id = ctx.last_transition_id;
    const state = ctx.room.members.get(member).?.state;
    const result = next.begin(&ctx.room, role_id, generation) catch |err| return failure(err);
    // Native begin changes the member's loading state. Roll it back unless both
    // protobuf messages have been encoded before publishing this transition.
    const change = encode(ctx, member, result.transition_id, result.change) catch |err| {
        ctx.room.members.getPtr(member).?.state = state;
        return failure(err);
    };
    const enter = encode(ctx, member, result.transition_id, result.enter) catch |err| {
        allocator.free(change.bytes);
        ctx.room.members.getPtr(member).?.state = state;
        return failure(err);
    };
    ctx.queue.appendAssumeCapacity(change);
    ctx.queue.appendAssumeCapacity(enter);
    session.* = next;
    ctx.last_transition_id = result.transition_id;
    target.* = result.transition_id;
    return ok;
}
fn sceneLoaded(value: ?*Context, member: u16, world_id: u64, transition: u64, data: ?[*]const u8, size: u32) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    const session = ctx.sessions.getPtr(member) orelse return rejected;
    if (ctx.queue.items.len >= max_queue) return rejected;
    ctx.queue.ensureUnusedCapacity(allocator, 1) catch return oom;
    var arena: std.heap.ArenaAllocator = .init(allocator);
    defer arena.deinit();
    const request = decode(pb.CS_SCENE_LOAD_FINISH, data, size, &arena) catch |err| return failure(err);
    var next = session.*;
    const result = next.sceneLoaded(&ctx.room, world_id, transition, request, arena.allocator()) catch |err| return failure(err);
    const bootstrap = encode(ctx, member, transition, result.self) catch |err| return failure(err);
    ctx.queue.appendAssumeCapacity(bootstrap);
    session.* = next;
    return ok;
}
fn nativeApplied(value: ?*Context, member: u16, input: ?*const Proof) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    const proof = input orelse return invalid;
    const session = ctx.sessions.getPtr(member) orelse return rejected;
    session.nativeApplied(&ctx.room, .{ .transition_id = proof.transition_id, .world_id = proof.world_id, .scene_id = proof.scene_id, .leader_id = proof.leader_id, .private_feed_generation = proof.private_feed_generation }) catch |err| return failure(err);
    return ok;
}
fn poll(value: ?*Context, output: ?*Message, destination: ?[*]u8, capacity: u32) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    const head = output orelse return invalid;
    if (ctx.queue.items.len == 0) {
        head.* = std.mem.zeroes(Message);
        return empty;
    }
    const front = ctx.queue.items[0];
    head.* = front.head;
    if (destination == null or capacity < front.bytes.len) return short_buffer;
    @memcpy(destination.?[0..front.bytes.len], front.bytes);
    _ = ctx.queue.orderedRemove(0);
    allocator.free(front.bytes);
    return ok;
}
fn removeMember(value: ?*Context, member: u16) callconv(.c) i32 {
    const ctx = value orelse return invalid;
    if (member == 0 or member == 1) return rejected;
    const session = ctx.sessions.getPtr(member) orelse return rejected;
    // Room removal reserves its complete list first; allocation failure leaves
    // the old membership and pending transition intact for a retry.
    session.close(&ctx.room) catch |err| return failure(err);
    _ = ctx.sessions.remove(member);
    var i: usize = 0;
    while (i < ctx.queue.items.len) {
        if (ctx.queue.items[i].head.recipient == member) {
            allocator.free(ctx.queue.orderedRemove(i).bytes);
        } else i += 1;
    }
    return ok;
}
const Api = extern struct {
    struct_size: u32,
    version: u32,
    create: *const @TypeOf(create),
    destroy: *const @TypeOf(destroy),
    add_member: *const @TypeOf(addMember),
    import_host_scene: *const @TypeOf(importHost),
    allocate_approved_operator: *const @TypeOf(allocateOperator),
    begin_scene: *const @TypeOf(beginScene),
    scene_loaded: *const @TypeOf(sceneLoaded),
    native_applied: *const @TypeOf(nativeApplied),
    poll: *const @TypeOf(poll),
    remove_member: *const @TypeOf(removeMember),
};
const api: Api = .{ .struct_size = @sizeOf(Api), .version = 1, .create = create, .destroy = destroy, .add_member = addMember, .import_host_scene = importHost, .allocate_approved_operator = allocateOperator, .begin_scene = beginScene, .scene_loaded = sceneLoaded, .native_applied = nativeApplied, .poll = poll, .remove_member = removeMember };
export fn EFLinkNative_GetApiV1() callconv(.c) *const Api {
    return &api;
}
