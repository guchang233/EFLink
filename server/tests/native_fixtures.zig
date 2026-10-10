// Synthetic fixtures only: no login, account data or gameplay objects.
const std = @import("std");
const proto = @import("proto");
const pb = proto.pb;
const Room = @import("room");

fn emit(name: []const u8, message: anytype, first: bool) !void {
    var output: std.Io.Writer.Allocating = .init(std.heap.page_allocator);
    defer output.deinit();
    try proto.encodeMessage(&output.writer, message);
    std.debug.print("{s}{{\"type\":\"{s}\",\"hex\":\"", .{ if (first) "" else ",", name });
    for (output.written()) |byte| std.debug.print("{x:0>2}", .{byte});
    std.debug.print("\"}}", .{});
}

pub fn main() !void {
    var room = try Room.init(std.heap.page_allocator, 9001, 7001, 103);
    defer room.deinit();
    try room.addMember(1);
    try room.addMember(2);
    _ = try room.spawnCharacter(1, .{ .common_info = .{ .templateid = "synthetic_host", .hp = 300 }, .battle_info = .{} });
    _ = try room.spawnCharacter(2, .{ .common_info = .{ .templateid = "synthetic_guest", .hp = 300 }, .battle_info = .{} });
    _ = try room.spawnMonster(.{ .common_info = .{ .templateid = "synthetic_enemy", .hp = 500 }, .battle_info = .{} });
    var arena: std.heap.ArenaAllocator = .init(std.heap.page_allocator);
    defer arena.deinit();
    const bootstrap = try room.bootstrap(2, arena.allocator());
    const damage: pb.BATTLE_CLIENT_OP_DATA = .{
        .owner_id = 42, .op_type = .BATTLE_OP_ENTITY_VALUE_MODIFY, .generation = 1, .chapter_map_id = 103, .seq_id = 7,
        .op_data_impl = .{ .entity_value_modify_data = .{ .entity_inst_id = 501, .scene_num_id = 103, .value = .{ .hp = 100 }, .delta_value = -25 } },
    };
    var ops = [_]pb.BATTLE_CLIENT_OP_DATA{damage};
    std.debug.print("[", .{});
    try emit("BATTLE_CLIENT_OP_DATA", damage, true);
    try emit("CS_BATTLE_OP", pb.CS_BATTLE_OP{ .client_data = .{ .op_list = .{ .items = &ops, .capacity = ops.len, .pointer_stability = .{} } } }, false);
    try emit("SC_SELF_SCENE_INFO", bootstrap, false);
    try emit("SC_OBJECT_ENTER_VIEW", pb.SC_OBJECT_ENTER_VIEW{ .detail = bootstrap.detail }, false);
    try emit("SC_CHANGE_SCENE_BEGIN_NOTIFY", pb.SC_CHANGE_SCENE_BEGIN_NOTIFY{ .scene_num_id = 103, .position = .{ .x = 20, .y = 1, .z = 5 } }, false);
    try emit("SC_ENTER_SCENE_NOTIFY", pb.SC_ENTER_SCENE_NOTIFY{ .role_id = 2, .scene_num_id = 103, .scene_id = 7001, .position = .{ .x = 20, .y = 1, .z = 5 } }, false);
    std.debug.print("]", .{});
}
