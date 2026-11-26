// 仅用于 build.zig 的便携构建：给 mingw CRT 补一个窄字符 WinMain 入口，
// 转调程序真正的 wWinMain（等价于 MSVC 构建里 /SUBSYSTEM:WINDOWS + wWinMain）。
// 不参与 CMake/MSVC 构建，也不影响 src/ 下的任何代码。
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR cmdLine, int nCmdShow);

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR, int nCmdShow) {
    return wWinMain(hInstance, hPrev, GetCommandLineW(), nCmdShow);
}
