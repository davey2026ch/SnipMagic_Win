// 便携构建脚本（无 MSVC 时用 zig 出 exe）
// 用法： zig build -Doptimize=ReleaseFast --prefix out
// 产物： out/bin/SnipMagic.exe  —— 图标与清单（comctl32 v6 / DPI）都已作为资源内嵌
const std = @import("std");

const sources = [_][]const u8{
    "src/main.cpp",
    "src/settings.cpp",
    "src/annotation.cpp",
    "src/document.cpp",
    "src/overlay.cpp",
    "src/canvas.cpp",
    "src/canvas_gif.cpp",
    "src/compareview.cpp",
    "src/colorpicker.cpp",
    "src/textdlg.cpp",
    "src/settingsdlg.cpp",
    "src/extract.cpp",
    "src/longstitch.cpp",
    "src/longcapture.cpp",
    "src/gifcodec.cpp",
    "src/gifrecord.cpp",
    "src/app.cpp",
};

const libs = [_][]const u8{
    "user32", "gdi32",  "gdiplus",  "comctl32", "comdlg32", "shell32", "ole32",
    "oleaut32", "shlwapi", "uxtheme", "msimg32", "imm32",  "winhttp", "crypt32",
    "uuid",   "winmm",
};

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "SnipMagic",
        .target = target,
        .optimize = optimize,
    });

    exe.addCSourceFiles(.{
        .files = &sources,
        .flags = &.{
            "-std=c++17",
            "-DUNICODE",
            "-D_UNICODE",
            "-DNOMINMAX",
            "-DWIN32_LEAN_AND_MEAN",
            // 让宽字符 printf 的 %s 语义与 MSVC 一致
            "-D__USE_MINGW_ANSI_STDIO=0",
            "-w",
        },
    });
    exe.addIncludePath(b.path("src"));
    exe.addWin32ResourceFile(.{ .file = b.path("build_mingw.rc") });
    exe.linkLibCpp();
    exe.subsystem = .Windows;
    // 程序入口是 wWinMain：补一个窄字符 WinMain 转调（等价于 mingw 的 -municode）
    exe.addCSourceFile(.{ .file = b.path("build_mingw_entry.c"), .flags = &.{"-w"} });
    for (libs) |l| exe.linkSystemLibrary(l);

    b.installArtifact(exe);
}
