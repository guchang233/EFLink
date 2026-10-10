const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const generator = b.addExecutable(.{
        .name = "eflink-proto-gen",
        .root_module = b.createModule(.{
            .root_source_file = b.path("vendor/lr-proto/gen/main.zig"),
            .target = b.graph.host,
            .optimize = optimize,
        }),
    });
    const generate = b.addRunArtifact(generator);
    generate.expectExitCode(0);
    generate.addFileArg(b.path("vendor/lr-proto/endfield.proto"));
    const generated = generate.captureStdOut(.{ .basename = "native_proto.zig" });
    const proto = b.createModule(.{
        .root_source_file = b.path("vendor/lr-proto/root.zig"),
        .target = target,
        .optimize = optimize,
    });
    proto.addAnonymousImport("beyond_generated", .{ .root_source_file = generated });
    const room = b.createModule(.{
        .root_source_file = b.path("src/Room.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{.{ .name = "proto", .module = proto }},
    });
    const frame = b.createModule(.{
        .root_source_file = b.path("src/Frame.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{.{ .name = "proto", .module = proto }},
    });
    const scene_session = b.createModule(.{
        .root_source_file = b.path("src/SceneSession.zig"),
        .target = target,
        .optimize = optimize,
        .imports = &.{ .{ .name = "proto", .module = proto }, .{ .name = "room", .module = room } },
    });
    const tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("tests/native_world_test.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "proto", .module = proto },
                .{ .name = "room", .module = room },
                .{ .name = "frame", .module = frame },
                .{ .name = "scene_session", .module = scene_session },
            },
        }),
    });
    b.step("test", "Validate native wire format and shared server world").dependOn(&b.addRunArtifact(tests).step);
    const authority = b.addLibrary(.{
        .name = "eflink-native-authority",
        .linkage = .dynamic,
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/NativeAuthority.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{ .{ .name = "proto", .module = proto }, .{ .name = "room", .module = room }, .{ .name = "scene_session", .module = scene_session } },
        }),
    });
    b.step("authority", "Build the experimental callable shared world kernel").dependOn(&b.addInstallArtifact(authority, .{}).step);
    const fixtures = b.addExecutable(.{
        .name = "eflink-native-fixtures",
        .root_module = b.createModule(.{
            .root_source_file = b.path("tests/native_fixtures.zig"),
            .target = b.graph.host,
            .optimize = optimize,
            .imports = &.{ .{ .name = "proto", .module = proto }, .{ .name = "room", .module = room } },
        }),
    });
    const emit = b.addRunArtifact(fixtures);
    emit.expectExitCode(0);
    const fixture_file = emit.captureStdErr(.{ .basename = "native-fixtures.json" });
    b.step("fixtures", "Generate synthetic server messages for native parser verification").dependOn(&b.addInstallFile(fixture_file, "share/native-fixtures.json").step);
}
