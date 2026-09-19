#include "settingsdlg.h"
#include "settings.h"
#include "version.h"
#include "colorpicker.h"
#include "app.h"

namespace {

const int kW = 420;
const int kH = 420;

enum {
    IDC_HOTKEY = 3001,
    IDC_THEME = 3002,
    IDC_MOSAIC = 3003,
    IDC_LINE = 3004,
    IDC_BRUSH = 3005,
    IDC_THEMECOLOR = 3006,
    IDC_OK = 3007,
    IDC_CANCEL = 3008,
    IDC_VERSION = 3009
};

struct SetDlgState {
    HWND hwnd = nullptr;
    bool ok = false;
    COLORREF themeColor = RGB(0, 0x78, 0xD4);
    HWND colorBtn = nullptr;
    AppSettings draft;
};

const wchar_t* kClass = L"ScreenshotToolSettingsDlg";

void PaintColorBtn(HWND hwnd, SetDlgState* st) {
    if (!st->colorBtn) return;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;
    RECT rc; GetClientRect(st->colorBtn, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bm);
    Graphics g(mem);
    SolidBrush br(ToGpColor(st->themeColor));
    g.FillRectangle(&br, 0, 0, rc.right, rc.bottom);
    Pen pen(Color(255, 80, 80, 80), 1);
    g.DrawRectangle(&pen, 0, 0, rc.right - 1, rc.bottom - 1);
    HDC btnDc = GetDC(st->colorBtn);
    BitBlt(btnDc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    ReleaseDC(st->colorBtn, btnDc);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SetDlgState* st = reinterpret_cast<SetDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<SetDlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        return TRUE;
    }
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        if (id == IDC_THEMECOLOR) {
            auto r = ColorPicker::Show(hwnd, st->themeColor, 255);
            if (r.ok) {
                st->themeColor = r.color;
                PaintColorBtn(hwnd, st);
            }
            return 0;
        }
        if (id == IDC_OK) {
            wchar_t buf[128] = {};
            GetWindowTextW(GetDlgItem(hwnd, IDC_HOTKEY), buf, 128);
            UINT m = 0, v = 0;
            if (ParseHotkeyText(buf, m, v) && v != 0) {
                st->draft.hotkeyModifiers = m;
                st->draft.hotkeyVk = v;
                st->draft.hotkeyText = HotkeyToText(m, v);
            } else {
                MessageBoxW(hwnd, L"快捷键格式无效，示例：Ctrl+Shift+R", L"提示", MB_ICONWARNING);
                return 0;
            }
            int theme = static_cast<int>(SendMessageW(GetDlgItem(hwnd, IDC_THEME), CB_GETCURSEL, 0, 0));
            if (theme < 0) theme = 0;
            st->draft.theme = static_cast<ThemeMode>(theme);
            st->draft.mosaicSize = GetDlgItemInt(hwnd, IDC_MOSAIC, nullptr, FALSE);
            st->draft.lineThickness = GetDlgItemInt(hwnd, IDC_LINE, nullptr, FALSE);
            st->draft.brushThickness = GetDlgItemInt(hwnd, IDC_BRUSH, nullptr, FALSE);
            if (st->draft.mosaicSize < 1) st->draft.mosaicSize = 1;
            if (st->draft.lineThickness < 1) st->draft.lineThickness = 1;
            if (st->draft.brushThickness < 1) st->draft.brushThickness = 1;
            st->draft.themeColor = st->themeColor;
            st->ok = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_CANCEL) {
            st->ok = false;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }
    case WM_PAINT:
        if (st) PaintColorBtn(hwnd, st);
        return 0;
    case WM_CLOSE:
        if (st) { st->ok = false; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void EnsureClass(HINSTANCE hi) {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

bool SettingsDialog::Show(HWND owner) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);

    SetDlgState st;
    st.draft = Settings();
    st.themeColor = Settings().themeColor;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
                                kClass, L"设置",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                (sw - kW) / 2, (sh - kH) / 2, kW, kH,
                                owner, nullptr, hi, &st);
    if (!hwnd) return false;

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    int y = 20;
    auto label = [&](const wchar_t* text, int x, int yy) {
        HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                               x, yy, 110, 24, hwnd, nullptr, hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    auto edit = [&](int id, const wchar_t* val, int x, int yy, int w) {
        HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val,
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 x, yy, w, 28, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hi, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };

    label(L"截图快捷键", 20, y);
    edit(IDC_HOTKEY, st.draft.hotkeyText.c_str(), 140, y - 4, 220);
    y += 44;

    label(L"主题", 20, y);
    HWND theme = CreateWindowW(L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               140, y - 4, 220, 120, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEME)), hi, nullptr);
    SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"跟随系统"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"明亮"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"暗色"));
    SendMessageW(theme, CB_SETCURSEL, static_cast<int>(st.draft.theme), 0);
    y += 44;

    label(L"马赛克密度", 20, y);
    edit(IDC_MOSAIC, std::to_wstring(st.draft.mosaicSize).c_str(), 140, y - 4, 80);
    y += 44;

    label(L"线条粗细", 20, y);
    edit(IDC_LINE, std::to_wstring(st.draft.lineThickness).c_str(), 140, y - 4, 80);
    y += 44;

    label(L"笔刷粗细", 20, y);
    edit(IDC_BRUSH, std::to_wstring(st.draft.brushThickness).c_str(), 140, y - 4, 80);
    y += 44;

    label(L"主题色", 20, y);
    st.colorBtn = CreateWindowW(L"BUTTON", L"",
                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                140, y - 6, 48, 32, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEMECOLOR)), hi, nullptr);
    y += 50;

    std::wstring ver = std::wstring(L"版本 ") + APP_VERSION + L"  ·  打包时间 " +
                       std::wstring(APP_BUILD_TIME, APP_BUILD_TIME + strlen(APP_BUILD_TIME));
    // APP_BUILD_TIME is char*
    {
        std::string bt = APP_BUILD_TIME;
        std::wstring wbt(bt.begin(), bt.end());
        ver = std::wstring(L"版本 ") + APP_VERSION + L"  ·  打包时间 " + wbt;
    }
    HWND verH = CreateWindowW(L"STATIC", ver.c_str(), WS_CHILD | WS_VISIBLE,
                              20, kH - 100, kW - 40, 22, hwnd, reinterpret_cast<HMENU>(IDC_VERSION), hi, nullptr);
    SendMessageW(verH, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    HWND ok = CreateWindowW(L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            kW - 200, kH - 70, 80, 32, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OK)), hi, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kW - 105, kH - 70, 80, 32, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)), hi, nullptr);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (font) DeleteObject(font);

    if (!st.ok) return false;

    // apply + persist
    AppSettings& s = Settings();
    s.hotkeyModifiers = st.draft.hotkeyModifiers;
    s.hotkeyVk = st.draft.hotkeyVk;
    s.hotkeyText = st.draft.hotkeyText;
    s.theme = st.draft.theme;
    s.mosaicSize = st.draft.mosaicSize;
    s.lineThickness = st.draft.lineThickness;
    s.brushThickness = st.draft.brushThickness;
    s.themeColor = st.themeColor;
    s.Save();
    App::Instance().OnSettingsChanged();
    return true;
}
