// Native unencrypted EF framing, reused from LR-S's Session/network contract.
// This does not redirect game sockets or establish an authenticated game session.
const std = @import("std");
const proto = @import("proto");
const pb = proto.pb;

pub const Packet = struct { head: pb.CSHead, body: []const u8 };

pub fn encode(writer: *std.Io.Writer, message: anytype, client_sequence: u64, server_sequence: u64) !void {
    var body: std.Io.Writer.Allocating = .init(std.heap.page_allocator);
    defer body.deinit();
    try proto.encodeMessage(&body.writer, message);
    if (body.written().len > std.math.maxInt(u16)) return error.FragmentationRequired;
    const head: pb.CSHead = .{
        .msgid = @backingInt(proto.messageId(@TypeOf(message))),
        .up_seqid = client_sequence,
        .down_seqid = server_sequence,
        .checksum = std.hash.Crc32.hash(body.written()),
    };
    var header: std.Io.Writer.Allocating = .init(std.heap.page_allocator);
    defer header.deinit();
    try proto.encodeMessage(&header.writer, head);
    if (header.written().len > std.math.maxInt(u8)) return error.InvalidHeader;
    try writer.writeInt(u8, @intCast(header.written().len), .little);
    try writer.writeInt(u16, @intCast(body.written().len), .little);
    try writer.writeAll(header.written());
    try writer.writeAll(body.written());
}

pub fn read(reader: *std.Io.Reader, allocator: std.mem.Allocator) !Packet {
    const header_size = try reader.takeInt(u8, .little);
    const body_size = try reader.takeInt(u16, .little);
    if (header_size == 0) return error.InvalidHeader;
    const payload = try reader.take(@as(usize, header_size) + body_size);
    var header: std.Io.Reader = .fixed(payload[0..header_size]);
    const head = try proto.decodeMessage(&header, allocator, pb.CSHead);
    if (head.is_compress) return error.CompressionNotImplemented;
    if (head.total_pack_count > 1 or head.current_pack_index != 0) return error.FragmentationNotImplemented;
    if (head.msgid == 0) return error.InvalidMessageId;
    const body = payload[header_size..];
    if (std.hash.Crc32.hash(body) != head.checksum) return error.ChecksumMismatch;
    return .{ .head = head, .body = body };
}
