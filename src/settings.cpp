#include "settings.h"
#include "version.h"

namespace {

std::wstring IniValue(const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section, key, def, buf, 1024, util::GetIniPath().c_str());
    return buf;
}

int IniInt(const wchar_t* section, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, def, util::GetIniPath().c_str()));
}

void IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& val) {
    WritePrivateProfileStringW(section, key, val.c_str(), util::GetIniPath().c_str());
}

void IniWriteInt(const wchar_t* section, const wchar_t* key, int val) {
    IniWrite(section, key, std::to_wstring(val));
}

bool SystemPrefersDark() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD value = 1;
    DWORD size = sizeof(value);
    DWORD type = 0;
    RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    return value == 0;
}

} // namespace

AppSettings& Settings() {
    static AppSettings s;
    return s;
}

bool AppSettings::IsDarkTheme() const {
    if (theme == ThemeMode::Dark) return true;
    if (theme == ThemeMode::Light) return false;
    return SystemPrefersDark();
}

COLORREF AppSettings::BgColor() const {
    return IsDarkTheme() ? RGB(32, 32, 32) : RGB(245, 245, 245);
}

COLORREF AppSettings::PanelColor() const {
    return IsDarkTheme() ? RGB(45, 45, 45) : RGB(255, 255, 255);
}

COLORREF AppSettings::TextColor() const {
    return IsDarkTheme() ? RGB(230, 230, 230) : RGB(30, 30, 30);
}

COLORREF AppSettings::BorderColor() const {
    return IsDarkTheme() ? RGB(70, 70, 70) : RGB(200, 200, 200);
}

COLORREF AppSettings::CanvasBg() const {
    return IsDarkTheme() ? RGB(24, 24, 24) : RGB(230, 230, 230);
}

std::wstring HotkeyToText(UINT mods, UINT vk) {
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_SHIFT)   s += L"Shift+";
    if (mods & MOD_ALT)     s += L"Alt+";
    if (mods & MOD_WIN)     s += L"Win+";

    wchar_t name[64] = {};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16;
    if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN)
        scan |= 0x01000000;
    if (GetKeyNameTextW(static_cast<LONG>(scan), name, 64) > 0 && name[0]) {
        s += name;
    } else if (vk >= 'A' && vk <= 'Z') {
        s += static_cast<wchar_t>(vk);
    } else if (vk >= '0' && vk <= '9') {
        s += static_cast<wchar_t>(vk);
    } else {
        s += util::Format(L"VK_%02X", vk);
    }
    return s;
}

bool ParseHotkeyText(const std::wstring& text, UINT& mods, UINT& vk) {
    mods = 0;
    vk = 0;
    std::wstring t = text;
    for (auto& ch : t) ch = static_cast<wchar_t>(towlower(ch));

    auto has = [&](const wchar_t* k) {
        return t.find(k) != std::wstring::npos;
    };
    if (has(L"ctrl") || has(L"control")) mods |= MOD_CONTROL;
    if (has(L"shift")) mods |= MOD_SHIFT;
    if (has(L"alt") || has(L"menu")) mods |= MOD_ALT;
    if (has(L"win")) mods |= MOD_WIN;

    // last token after '+'
    size_t pos = t.find_last_of(L'+');
    std::wstring key = (pos == std::wstring::npos) ? t : t.substr(pos + 1);
    while (!key.empty() && iswspace(key.front())) key.erase(key.begin());
    while (!key.empty() && iswspace(key.back())) key.pop_back();

    if (key.size() == 1 && key[0] >= L'a' && key[0] <= L'z') {
        vk = static_cast<UINT>(key[0] - L'a' + L'A');
        return true;
    }
    if (key.size() == 1 && key[0] >= L'0' && key[0] <= L'9') {
        vk = static_cast<UINT>(key[0]);
        return true;
    }
    if (key == L"f1") vk = VK_F1;
    else if (key == L"f2") vk = VK_F2;
    else if (key == L"f3") vk = VK_F3;
    else if (key == L"f4") vk = VK_F4;
    else if (key == L"f5") vk = VK_F5;
    else if (key == L"f6") vk = VK_F6;
    else if (key == L"f7") vk = VK_F7;
    else if (key == L"f8") vk = VK_F8;
    else if (key == L"f9") vk = VK_F9;
    else if (key == L"f10") vk = VK_F10;
    else if (key == L"f11") vk = VK_F11;
    else if (key == L"f12") vk = VK_F12;
    else if (key == L"prtsc" || key == L"printscreen") vk = VK_SNAPSHOT;
    else if (key == L"esc" || key == L"escape") vk = VK_ESCAPE;
    else return false;
    return mods != 0 || vk != 0;
}

void AppSettings::Load() {
    std::wstring path = util::GetIniPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Save(); // create default
        return;
    }

    std::wstring hk = IniValue(L"Settings", L"Hotkey", L"Ctrl+Shift+R");
    UINT m = 0, v = 0;
    if (ParseHotkeyText(hk, m, v) && v != 0) {
        hotkeyModifiers = m;
        hotkeyVk = v;
        hotkeyText = HotkeyToText(m, v);
    } else {
        hotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
        hotkeyVk = 'R';
        hotkeyText = L"Ctrl+Shift+R";
    }

    std::wstring lhk = IniValue(L"Settings", L"LongHotkey", L"Ctrl+Shift+E");
    UINT lm = 0, lv = 0;
    if (ParseHotkeyText(lhk, lm, lv) && lv != 0) {
        longHotkeyModifiers = lm;
        longHotkeyVk = lv;
        longHotkeyText = HotkeyToText(lm, lv);
    } else {
        longHotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
        longHotkeyVk = 'E';
        longHotkeyText = L"Ctrl+Shift+E";
    }
    // 区域/长截图热键不得相同
    if (longHotkeyModifiers == hotkeyModifiers && longHotkeyVk == hotkeyVk) {
        longHotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
        longHotkeyVk = 'E';
        longHotkeyText = L"Ctrl+Shift+E";
        if (longHotkeyModifiers == hotkeyModifiers && longHotkeyVk == hotkeyVk) {
            longHotkeyVk = (hotkeyVk == 'E') ? 'L' : 'E';
            longHotkeyText = HotkeyToText(longHotkeyModifiers, longHotkeyVk);
        }
    }

    int th = IniInt(L"Settings", L"Theme", 0);
    if (th < 0 || th > 2) th = 0;
    theme = static_cast<ThemeMode>(th);

    mosaicSize     = (std::max)(1, IniInt(L"Settings", L"MosaicSize", 10));
    lineThickness  = (std::max)(1, IniInt(L"Settings", L"LineThickness", 4));
    brushThickness = (std::max)(1, IniInt(L"Settings", L"BrushThickness", 25));
    themeColor     = util::ParseHex(IniValue(L"Settings", L"ThemeColor", L"#0078D4"));
    drawColor      = util::ParseHex(IniValue(L"Settings", L"DrawColor", L"#FF0000"));
    int a = IniInt(L"Settings", L"DrawAlpha", 255);
    drawAlpha = static_cast<BYTE>((std::max)(0, (std::min)(255, a)));

    mineruToken = util::TrimToken(IniValue(L"Settings", L"MinerUToken", L""));
    volcApiKey  = util::TrimToken(IniValue(L"Settings", L"VolcApiKey", L""));
}

void AppSettings::Save() const {
    IniWrite(L"Settings", L"Hotkey", hotkeyText);
    IniWriteInt(L"Settings", L"HotkeyModifiers", static_cast<int>(hotkeyModifiers));
    IniWriteInt(L"Settings", L"HotkeyVk", static_cast<int>(hotkeyVk));
    IniWrite(L"Settings", L"LongHotkey", longHotkeyText);
    IniWriteInt(L"Settings", L"LongHotkeyModifiers", static_cast<int>(longHotkeyModifiers));
    IniWriteInt(L"Settings", L"LongHotkeyVk", static_cast<int>(longHotkeyVk));
    IniWriteInt(L"Settings", L"Theme", static_cast<int>(theme));
    IniWriteInt(L"Settings", L"MosaicSize", mosaicSize);
    IniWriteInt(L"Settings", L"LineThickness", lineThickness);
    IniWriteInt(L"Settings", L"BrushThickness", brushThickness);
    IniWrite(L"Settings", L"ThemeColor", util::ToHex(themeColor));
    IniWrite(L"Settings", L"DrawColor", util::ToHex(drawColor));
    IniWriteInt(L"Settings", L"DrawAlpha", drawAlpha);
    IniWrite(L"Settings", L"MinerUToken", util::TrimToken(mineruToken));
    IniWrite(L"Settings", L"VolcApiKey", util::TrimToken(volcApiKey));

    // Meta
    IniWrite(L"Meta", L"Version", APP_VERSION);
    std::string bt = APP_BUILD_TIME;
    std::wstring wbt(bt.begin(), bt.end());
    IniWrite(L"Meta", L"BuildTime", wbt);
}
