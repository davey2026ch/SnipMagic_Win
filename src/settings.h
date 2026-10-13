#pragma once
#include <vector>
#include "util.h"

enum class ThemeMode {
    System = 0,
    Light  = 1,
    Dark   = 2
};

enum class HotkeyAction {
    Capture = 0,
    LongCapture = 1
};

struct AppSettings {
    // 区域截图热键：modifiers use MOD_* flags; vk is virtual key code
    UINT hotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
    UINT hotkeyVk        = 'R';
    std::wstring hotkeyText = L"Ctrl+Shift+R";

    // 长截图热键
    UINT longHotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
    UINT longHotkeyVk        = 'E';
    std::wstring longHotkeyText = L"Ctrl+Shift+E";

    ThemeMode theme = ThemeMode::System;
    int mosaicSize      = 10;
    int lineThickness   = 4;
    int brushThickness  = 25;
    COLORREF themeColor = RGB(0x00, 0x78, 0xD4); // Windows accent blue

    // Current drawing color (also persisted)
    COLORREF drawColor  = RGB(0xFF, 0x00, 0x00);
    BYTE     drawAlpha  = 255;

    // 带边框复制到外部软件：开启时复制到微信/Word 等外部程序自动加 1px 内边框；
    // 程序内粘贴（当前画布/其他页签）始终不加边框。默认开启。
    bool borderCopyToExternal = true;

    // 取色器「最近使用」颜色（最新在前，最多 kRecentColorMax 个；持久化到 ini）
    // 宫格 8 列 × 3 行 = 24 格，其中前 3 格是固定色（红/黑/白，不可替换），
    // 因此「最近使用」最多记 21 个
    static const int kRecentColorMax = 21;
    std::vector<COLORREF> recentColors;

    // API tokens (persisted; never hardcode secrets in source)
    std::wstring mineruToken;  // MinerU API — 提取内容
    std::wstring volcApiKey;   // 火山引擎 AI MediaKit — 魔法消除

    void Load();
    void Save() const;

    // 记录一次「使用过」的颜色：去重、插到最前、截断到 9 个，并立刻写 ini
    void PushRecentColor(COLORREF c);

    bool IsDarkTheme() const;
    COLORREF BgColor() const;
    COLORREF PanelColor() const;
    COLORREF TextColor() const;
    COLORREF BorderColor() const;
    COLORREF CanvasBg() const;
};

// 取色器宫格前 3 格的固定常用色（红 / 黑 / 白）。
// 它们固定占位、不参与「最近使用」：既不会被历史色替换，用它们取色时也不记进历史（避免重复显示）。
inline const COLORREF* PickerFixedColors(int& count) {
    static const COLORREF kFixed[3] = {
        RGB(0xFF, 0x00, 0x00),   // 红
        RGB(0x00, 0x00, 0x00),   // 黑
        RGB(0xFF, 0xFF, 0xFF),   // 白
    };
    count = 3;
    return kFixed;
}

inline bool IsPickerFixedColor(COLORREF c) {
    int n = 0;
    const COLORREF* fixed = PickerFixedColors(n);
    for (int i = 0; i < n; ++i) {
        if (fixed[i] == c) return true;
    }
    return false;
}

AppSettings& Settings();

// Parse / format hotkey text helpers
std::wstring HotkeyToText(UINT mods, UINT vk);
bool ParseHotkeyText(const std::wstring& text, UINT& mods, UINT& vk);
