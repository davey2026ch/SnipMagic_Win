#include "app.h"
#include "settings.h"
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
    if (App::Instance().Init(hInstance, nCmdShow)) {
        code = App::Instance().Run();
    } else {
        MessageBoxW(nullptr, L"初始化失败", APP_NAME, MB_ICONERROR);
        code = 1;
    }

    if (mutex) CloseHandle(mutex);
    GdiplusShutdown(gdiplusToken);
    CoUninitialize();
    return code;
}
