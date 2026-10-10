pub const pb = @import("beyond_generated");
const std = @import("std");

const enums = std.enums;
const Io = std.Io;
const Allocator = std.mem.Allocator;

const WireType = enum(u32) {
    var_int = 0,
    int64 = 1,
    length_prefixed = 2,
    int32 = 5,

    pub fn of(comptime T: type) WireType {
        if (T == []const u8) return .length_prefixed;

        return switch (@typeInfo(T)) {
            .int, .bool, .@"enum" => .var_int,
            .float => |float| return switch (float.bits) {
                32 => .int32,
                64 => .int64,
                else => @compileError("only f32 and f64 are supported"),
            },
            .optional => |container| of(container.child),
            .pointer => |container| of(container.child),
            .@"struct" => .length_prefixed,
            else => @compileError("unsupported type: " ++ @typeName(T)),
        };
    }
};

fn MsgIDEnum(comptime Message: type) type {
    if (comptime std.mem.startsWith(u8, Message.message_name, "SC_"))
        return pb.SCMessageID;

    if (comptime std.mem.startsWith(u8, Message.message_name, "CS_"))
        return pb.CSMessageID;

    comptime unreachable;
}

pub fn messageId(comptime Message: type) MsgIDEnum(Message) {
    return @field(MsgIDEnum(Message), Message.message_name);
}

pub fn encodeMessage(w: *Io.Writer, message: anytype) !void {
    const Message = @TypeOf(message);

    inline for (@typeInfo(Message).@"struct".field_names, @typeInfo(Message).@"struct".field_types) |field_name, field_type| {
        if (comptime oneofUnion(field_type)) |_| {
            if (@field(message, field_name)) |oneof| {
                switch (oneof) {
                    inline else => |value, tag| {
                        try encodeField(w, value, @field(Message, @tagName(tag) ++ "_field_desc").number);
                    },
                }
            }
        } else {
            if (shouldEncodeField(@field(message, field_name)))
                try encodeField(w, @field(message, field_name), @field(Message, field_name ++ "_field_desc").number);
        }
    }
}

fn oneofUnion(comptime T: type) ?std.builtin.Type.Union {
    return switch (@typeInfo(T)) {
        .optional => |optional| switch (@typeInfo(optional.child)) {
            .@"union" => |u| u,
            else => null,
        },
        else => null,
    };
}

pub fn encodingLength(message: anytype) usize {
    var prober = Io.Writer.Discarding.init("");
    encodeMessage(&prober.writer, message) catch unreachable;
    return prober.fullCount();
}

fn shouldEncodeField(value: anytype) bool {
    const Value = @TypeOf(value);
    if (Repeated(Value)) |_| {
        return value.items.len != 0;
    } else if (Optional(Value)) |Child| {
        // The generator represents ordinary proto3 enum fields as optional Zig
        // values. This is not protobuf explicit presence: a zero enum is omitted.
        // Real oneof values bypass shouldEncodeField and retain zero presence.
        if (@typeInfo(Child) == .@"enum") return if (value) |item| @backingInt(item) != 0 else false;
        return value != null;
    } else {
        if (Value == []const u8) return value.len != 0 else switch (@typeInfo(Value)) {
            .int => return value != 0,
            .bool => return value,
            .float => return value != 0,
            .@"enum" => return @as(i32, @backingInt(value)) != 0,
            .@"struct" => return true,
            else => @compileError("unsupported type: " ++ @typeName(Value)),
        }
    }
}

fn encodeField(w: *Io.Writer, value: anytype, comptime number: u32) !void {
    const Value = @TypeOf(value);
    if (Repeated(Value)) |_| {
        for (value.items) |item| try encodeField(w, item, number);
    } else if (Optional(Value)) |_| {
        if (value) |item| try encodeField(w, item, number);
    } else {
        try writeVarInt(w, comptime wireTag(number, .of(Value)));
        if (Value == []const u8) try writeBytes(w, value) else switch (@typeInfo(Value)) {
            .int => try writeVarInt(w, value),
            .bool => try writeVarInt(w, @as(u8, if (value) 1 else 0)),
            .float => |float| {
                const BackingInt = if (float.bits == 32) u32 else if (float.bits == 64) u64 else @compileError("encountered invalid float type: " ++ @typeName(Value));
                try w.writeInt(BackingInt, @bitCast(value), .little);
            },
            .@"enum" => try writeVarInt(w, @backingInt(value)),
            .@"struct" => {
                try writeVarInt(w, encodingLength(value));
                try encodeMessage(w, value);
            },
            else => @compileError("unsupported type: " ++ @typeName(Value)),
        }
    }
}

fn writeBytes(w: *Io.Writer, bytes: []const u8) !void {
    try writeVarInt(w, bytes.len);
    try w.writeAll(bytes);
}

fn Repeated(comptime T: type) ?type {
    return switch (@typeInfo(T)) {
        .@"struct" => if (@hasField(T, "items")) switch (@typeInfo(@FieldType(T, "items"))) {
            .pointer => |pointer| if (T == std.ArrayList(pointer.child)) return pointer.child else null,
            else => null,
        } else null,
        else => null,
    };
}

fn Optional(comptime T: type) ?type {
    return switch (@typeInfo(T)) {
        .optional => |optional| optional.child,
        else => null,
    };
}

inline fn wireTag(comptime field_number: u32, comptime wire_type: WireType) u32 {
    return (field_number << 3) | @backingInt(wire_type);
}

fn writeVarInt(w: *Io.Writer, value: anytype) !void {
    const int = @typeInfo(@TypeOf(value)).int;
    // Negative int32/int64 protobuf values use the sign-extended 64-bit varint.
    var v: u64 = if (int.signedness == .signed) @bitCast(@as(i64, value)) else @intCast(value);
    while (v >= 0x80) : (v >>= 7) {
        try w.writeByte(@intCast(0x80 | (v & 0x7F)));
    } else try w.writeByte(@intCast(v & 0x7F));
}

pub fn decodeMessage(r: *Io.Reader, allocator: Allocator, comptime Message: type) !Message {
    return decodeMessageBounded(r, allocator, Message, 0);
}

fn decodeMessageBounded(r: *Io.Reader, allocator: Allocator, comptime Message: type, depth: usize) anyerror!Message {
    if (depth > 64) return error.NestingLimitExceeded;
    @setEvalBranchQuota(100_000);

    const fields = @typeInfo(Message).@"struct".field_names;
    const field_types = @typeInfo(Message).@"struct".field_types;
    comptime var field_names: []const []const u8 = &.{};
    comptime var oneof_names: []const []const u8 = &.{};
    comptime var oneof_types: []const type = &.{};

    inline for (fields, field_types) |field_name, field_type| {
        if (@hasDecl(Message, field_name ++ "_field_desc")) {
            field_names = field_names ++ .{field_name};
        } else if (comptime oneofUnion(field_type)) |oneof| {
            oneof_names = oneof_names ++ .{field_name};
            oneof_types = oneof_types ++ .{field_type};
            inline for (oneof.field_names) |oneof_field| {
                field_names = field_names ++ .{oneof_field};
            }
        }
    }

    const MessageField = std.meta.FieldEnum(Message);

    const FieldNumber = comptime blk: {
        var field_numbers: [field_names.len]u32 = @splat(0);
        for (field_names, 0..) |name, i| {
            field_numbers[i] = @field(Message, name ++ "_field_desc").number;
        }

        break :blk @Enum(u32, .exhaustive, field_names, &field_numbers);
    };

    var message: Message = .init;

    while (true) {
        _ = r.peekByte() catch |err| switch (err) {
            error.EndOfStream => break,
            else => return err,
        };
        const wire_tag = try readVarInt(r, u32);
        if (wire_tag >> 3 == 0) return error.MalformedProtobuf;
        const wire_type = enums.fromInt(WireType, wire_tag & 7) orelse return error.MalformedProtobuf;
        if (fields.len == 0) {
            try skipField(r, wire_type);
            continue;
        }

        const field_variant = enums.fromInt(FieldNumber, wire_tag >> 3) orelse {
            try skipField(r, wire_type);
            continue;
        };

        switch (field_variant) {
            inline else => |variant| {
                const field_name = @tagName(variant);
                if (@hasField(Message, field_name) and comptime oneofUnion(@FieldType(Message, field_name)) == null) {
                    const field = fields[@backingInt(comptime std.meta.stringToEnum(MessageField, field_name).?)];
                    const field_type = field_types[@backingInt(comptime std.meta.stringToEnum(MessageField, field_name).?)];

                    if (Repeated(field_type)) |Item| {
                        if ((comptime WireType.of(Item) != .length_prefixed) and wire_type == .length_prefixed) {
                            const length = try readVarInt(r, usize); // packed list of scalar values
                            var reader = Io.Reader.fixed(try r.take(length));
                            while (true) {
                                _ = reader.peekByte() catch |err| switch (err) {
                                    error.EndOfStream => break,
                                    else => return err,
                                };
                                const value = try decodeField(&reader, allocator, Item, .of(Item), depth);
                                try @field(message, field).append(allocator, value);
                            }
                        } else {
                            const item = try decodeField(r, allocator, Item, wire_type, depth);
                            try @field(message, field).append(allocator, item);
                        }
                    } else {
                        @field(message, field) = try decodeField(r, allocator, field_type, wire_type, depth);
                    }
                } else inline for (oneof_names, oneof_types) |oneof_name, Oneof| inline for (@typeInfo(std.meta.Child(Oneof)).@"union".field_names, @typeInfo(std.meta.Child(Oneof)).@"union".field_types) |oneof_field, oneof_field_type| {
                    if (comptime std.mem.eql(u8, oneof_field, field_name)) {
                        @field(message, oneof_name) = @unionInit(std.meta.Child(Oneof), field_name, try decodeField(r, allocator, oneof_field_type, wire_type, depth));
                        break;
                    }
                };
            },
        }
    }

    return message;
}

fn decodeField(r: *Io.Reader, allocator: Allocator, comptime T: type, wire_type: WireType, depth: usize) anyerror!T {
    if (wire_type != WireType.of(T)) return error.MalformedProtobuf;
    if (Optional(T)) |C|
        return try decodeField(r, allocator, C, wire_type, depth)
    else if (T == []const u8)
        return try r.readAlloc(allocator, try readVarInt(r, usize))
    else switch (@typeInfo(T)) {
        .int => return try readVarInt(r, T),
        .bool => return (try readVarInt(r, u8)) != 0,
        .float => |float| {
            const BackingInt = if (float.bits == 32) u32 else if (float.bits == 64) u64 else @compileError("encountered float type of incompatible width: " ++ float.bits);
            return @bitCast(try r.takeInt(BackingInt, .little));
        },
        .@"enum" => return enums.fromInt(T, try readVarInt(r, i32)) orelse return error.UnknownEnumValue,
        .@"struct" => {
            var reader = Io.Reader.fixed(try r.take(try readVarInt(r, usize)));
            return try decodeMessageBounded(&reader, allocator, T, depth + 1);
        },
        else => @compileError("unsupported type: " ++ @typeName(T)),
    }
}

fn readVarInt(r: *Io.Reader, comptime T: type) !T {
    const int = @typeInfo(T).int;
    var shift: std.math.Log2Int(u64) = 0;
    var result: u64 = 0;

    while (true) : (shift += 7) {
        const byte = try r.takeByte();
        if (shift == 63 and byte > 1) return error.MalformedProtobuf;
        result |= @as(u64, byte & 0x7F) << shift;
        if ((byte & 0x80) != 0x80) return switch (int.signedness) {
            .unsigned => @truncate(result),
            .signed => @bitCast(@as(@Int(.unsigned, int.bits), @truncate(result))),
        };

        if (shift >= @bitSizeOf(u64) - 7) return error.MalformedProtobuf;
    }
}

fn skipField(r: *Io.Reader, wire_type: WireType) !void {
    switch (wire_type) {
        .var_int => _ = try readVarInt(r, u64),
        .int32 => try r.discardAll(4),
        .int64 => try r.discardAll(8),
        .length_prefixed => try r.discardAll(try readVarInt(r, usize)),
    }
}
