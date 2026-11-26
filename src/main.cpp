#include "app.h"
#include "settings.h"
#include "netutil.h"
#include "version.h"
#include <objidl.h>

using namespace Gdiplus;

// Fix: declare IsDark early for app.cpp... already handled in app.cpp via namespace.

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Per-monitor DPI awareness (fallback if manifest not applied)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef BOOL(WINAPI* SetCtxFn)(HANDLE);
        auto setCtx = reinterpret_cast<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx) {
            setCtx(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))); // PER_MONITOR_AWARE_V2
        } else {
            typedef BOOL(WINAPI* SetAwareFn)(void);
            auto setAware = reinterpret_cast<SetAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
            if (setAware) setAware();
        }
    }

    // Common controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // COM for shell dialogs
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // GDI+
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken = 0;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    // Single instance
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ScreenshotTool_SingleInstance_Mutex");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ScreenshotToolMainWindow", nullptr);
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        GdiplusShutdown(gdiplusToken);
        CoUninitialize();
        return 0;
    }

    int code = 0;
    // 启动先清掉上次异常退出（崩溃/强杀/断电）可能留下的临时文件：
    // 剪贴板 HTML 的临时 PNG、MinerU 提取切片的临时目录。
    // 正常运行时截图/长截图/录制与编辑全程只在内存里，不会落盘。
    util::PurgeClipboardTempFiles();
    netutil::PurgeTempDirs(L"mineru_");
    if (App::Instance().Init(hInstance, nCmdShow)) {
        code = App::Instance().Run();
    } else {
        MessageBoxW(nullptr, L"初始化失败", APP_NAME, MB_ICONERROR);
        code = 1;
    }
    // 退出时再清一次：开了「邮件客户端兼容」时临时 PNG 只活到程序关闭
    util::PurgeClipboardTempFiles();
    // 释放页面里的 GDI+ 位图（必须在 GdiplusShutdown 之前）
    App::Instance().ReleaseGdiplusResources();

    if (mutex) CloseHandle(mutex);
    GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return code;
}
