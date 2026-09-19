#pragma once
#include "util.h"

enum class ThemeMode {
    System = 0,
    Light  = 1,
    Dark   = 2
};

enum class HotkeyAction {
    Capture = 0
};

struct AppSettings {
    // Hotkey: modifiers use MOD_* flags; vk is virtual key code
    UINT hotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
    UINT hotkeyVk        = 'R';
    std::wstring hotkeyText = L"Ctrl+Shift+R";

    ThemeMode theme = ThemeMode::System;
    int mosaicSize      = 10;
    int lineThickness   = 4;
    int brushThickness  = 25;
    COLORREF themeColor = RGB(0x00, 0x78, 0xD4); // Windows accent blue

    // Current drawing color (also persisted)
    COLORREF drawColor  = RGB(0xFF, 0x00, 0x00);
    BYTE     drawAlpha  = 255;

    // API tokens (persisted; never hardcode secrets in source)
    std::wstring mineruToken;  // MinerU API — 提取内容
    std::wstring volcApiKey;   // 火山引擎 AI MediaKit — 魔法消除

    void Load();
    void Save() const;

    bool IsDarkTheme() const;
    COLORREF BgColor() const;
    COLORREF PanelColor() const;
    COLORREF TextColor() const;
    COLORREF BorderColor() const;
    COLORREF CanvasBg() const;
};

AppSettings& Settings();

// Parse / format hotkey text helpers
std::wstring HotkeyToText(UINT mods, UINT vk);
bool ParseHotkeyText(const std::wstring& text, UINT& mods, UINT& vk);
