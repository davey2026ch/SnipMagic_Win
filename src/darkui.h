#pragma once
// 弹窗暗色主题共享辅助（设置/文字/颜色弹窗、序号菜单共用）。
// 仅在暗色主题下生效；亮色保持原有浅色外观。
#include <windows.h>
#include <uxtheme.h>
#include "settings.h"

namespace darkui {

inline bool Dark() { return Settings().IsDarkTheme(); }

// 弹窗配色：与主界面暗色保持一致（面板 45、输入框 32、文字 230）
inline COLORREF DlgBg()   { return Dark() ? RGB(45, 45, 45) : RGB(250, 250, 250); }
inline COLORREF EditBg()  { return Dark() ? RGB(32, 32, 32) : RGB(255, 255, 255); }
inline COLORREF TextCol() { return Dark() ? RGB(230, 230, 230) : RGB(30, 30, 30); }

inline HBRUSH BgBrush() {
    static HBRUSH dark = CreateSolidBrush(RGB(45, 45, 45));
    static HBRUSH light = CreateSolidBrush(RGB(250, 250, 250));
    return Dark() ? dark : light;
}

inline HBRUSH EditBrush() {
    static HBRUSH dark = CreateSolidBrush(RGB(32, 32, 32));
    static HBRUSH light = CreateSolidBrush(RGB(255, 255, 255));
    return Dark() ? dark : light;
}

// 标题栏明暗随主题切换：暗色=开，明亮=显式关闭（否则从暗色切回明亮仍是黑条）
// DWMWA_USE_IMMERSIVE_DARK_MODE；旧系统自动回退属性号 19
inline void DarkTitleBar(HWND hwnd) {
    if (!hwnd) return;
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI* Fn)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (!fn) return;
    BOOL on = Dark() ? TRUE : FALSE;
    if (FAILED(fn(hwnd, 20, &on, sizeof(on)))) {
        fn(hwnd, 19, &on, sizeof(on));
    }
}

// 子控件配色：static 用面板底色，edit/listbox 用输入框底色
inline LRESULT CtlColor(UINT msg, HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, TextCol());
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        return reinterpret_cast<LRESULT>(EditBrush());
    }
    return reinterpret_cast<LRESULT>(BgBrush());
}

// 窗口类 hbrBackground 置空后，在 WM_ERASEBKGND 里调用
inline BOOL EraseBg(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, BgBrush());
    return TRUE;
}

// 把直接子控件切到系统暗色视觉主题（Win10 1809+ 的 uxtheme 提供；
// 旧系统上调用无效、控件自动回退默认外观，不会报错）。
// ComboBox 需要 DarkMode_CF 变体（DarkMode_Explorer 对下拉框外观不生效）
inline void ThemeChildren(HWND parent) {
    if (!parent || !Dark()) return;
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[64] = {};
        GetClassNameW(c, cls, 64);
        if (!wcscmp(cls, L"ComboBox")) {
            SetWindowTheme(c, L"DarkMode_CF", nullptr);
        } else if (!wcscmp(cls, L"Edit") || !wcscmp(cls, L"Button") ||
                   !wcscmp(cls, L"Static")) {
            SetWindowTheme(c, L"DarkMode_Explorer", nullptr);
        }
    }
}

} // namespace darkui
