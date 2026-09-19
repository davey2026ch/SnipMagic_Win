#include "textdlg.h"
#include "annotation.h"
#include "colorpicker.h"
#include "settings.h"

using namespace Gdiplus;

namespace {

const int kW = 560;
const int kH = 360;

enum TextCtrlId {
    TXC_EDIT = 2001,
    TXC_FONTSIZE = 2002,
    TXC_BOLD = 2003,
    TXC_TRANS = 2004,
    TXC_COLOR = 2005,
    TXC_OK = 2006,
    TXC_CANCEL = 2007,
    TXC_BGCOLOR = 2008
};

struct TextDlgState {
    HWND hwnd = nullptr;
    TextDialogResult result;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
    COLORREF bgColor = RGB(255, 255, 255);
    HWND colorBtn = nullptr;
    HWND bgColorBtn = nullptr;
    HWND bgColorLabel = nullptr;
    HBRUSH staticBrush = nullptr;
    bool done = false;
};

const wchar_t* kTextClass = L"ScreenshotToolTextDlg";
HICON g_blankIcon = nullptr;

HMENU Hm(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

HICON MakeBlankIcon(int size) {
    Bitmap bmp(size, size, PixelFormat32bppPARGB);
    HBITMAP hbm = nullptr;
    bmp.GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    if (!hbm) return nullptr;
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    DeleteObject(mask);
    return icon;
}

void PaintSwatch(HWND btn, COLORREF c, BYTE alpha) {
    if (!btn) return;
    HDC hdc = GetDC(btn);
    if (!hdc) return;
    RECT rc;
    GetClientRect(btn, &rc);
    Graphics g(hdc);
    g.Clear(Color(255, 250, 250, 250));
    SolidBrush br(ToGpColor(c, alpha));
    g.FillRectangle(&br, 0, 0, rc.right, rc.bottom);
    ReleaseDC(btn, hdc);
}

void RefreshSwatches(TextDlgState* st) {
    if (!st) return;
    PaintSwatch(st->colorBtn, st->color, st->alpha);
    bool showBg = st->bgColorBtn && IsWindowVisible(st->bgColorBtn);
    if (showBg) PaintSwatch(st->bgColorBtn, st->bgColor, 255);
}

void UpdateBgColorVisibility(TextDlgState* st) {
    if (!st || !st->hwnd) return;
    BOOL transparent = SendMessageW(GetDlgItem(st->hwnd, TXC_TRANS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    // 未勾选「背景透明」时显示背景色
    int show = transparent ? SW_HIDE : SW_SHOW;
    if (st->bgColorBtn) ShowWindow(st->bgColorBtn, show);
    if (st->bgColorLabel) ShowWindow(st->bgColorLabel, show);
    if (!transparent) RefreshSwatches(st);
}

LRESULT CALLBACK TextDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TextDlgState* st = reinterpret_cast<TextDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        st = static_cast<TextDlgState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hwnd = hwnd;
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(40, 40, 40));
        if (!st || !st->staticBrush) {
            static HBRUSH br = CreateSolidBrush(RGB(250, 250, 250));
            return reinterpret_cast<LRESULT>(br);
        }
        return reinterpret_cast<LRESULT>(st->staticBrush);
    }
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == TXC_TRANS && code == BN_CLICKED) {
            UpdateBgColorVisibility(st);
            return 0;
        }
        if (id == TXC_COLOR) {
            auto r = ColorPicker::Show(hwnd, st->color, st->alpha);
            if (r.ok) {
                st->color = r.color;
                st->alpha = r.alpha;
                RefreshSwatches(st);
            }
            return 0;
        }
        if (id == TXC_BGCOLOR) {
            auto r = ColorPicker::Show(hwnd, st->bgColor, 255);
            if (r.ok) {
                st->bgColor = r.color;
                RefreshSwatches(st);
            }
            return 0;
        }
        if (id == TXC_OK) {
            wchar_t buf[4096] = {};
            GetWindowTextW(GetDlgItem(hwnd, TXC_EDIT), buf, 4096);
            st->result.ok = true;
            st->result.text = buf;
            wchar_t sz[16] = {};
            GetWindowTextW(GetDlgItem(hwnd, TXC_FONTSIZE), sz, 16);
            float fs = static_cast<float>(_wtof(sz));
            if (fs < 8) fs = 8;
            if (fs > 200) fs = 200;
            st->result.fontSize = fs;
            st->result.bold = SendMessageW(GetDlgItem(hwnd, TXC_BOLD), BM_GETCHECK, 0, 0) == BST_CHECKED;
            st->result.transparentBg = SendMessageW(GetDlgItem(hwnd, TXC_TRANS), BM_GETCHECK, 0, 0) == BST_CHECKED;
            st->result.color = st->color;
            st->result.alpha = st->alpha;
            st->result.bgColor = st->bgColor;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == TXC_CANCEL) {
            st->result.ok = false;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!st || !dis) return 0;
        if (dis->CtlID == TXC_COLOR) {
            Graphics g(dis->hDC);
            SolidBrush br(ToGpColor(st->color, st->alpha));
            g.FillRectangle(&br, 0, 0,
                            dis->rcItem.right - dis->rcItem.left,
                            dis->rcItem.bottom - dis->rcItem.top);
            return TRUE;
        }
        if (dis->CtlID == TXC_BGCOLOR) {
            Graphics g(dis->hDC);
            SolidBrush br(ToGpColor(st->bgColor, 255));
            g.FillRectangle(&br, 0, 0,
                            dis->rcItem.right - dis->rcItem.left,
                            dis->rcItem.bottom - dis->rcItem.top);
            return TRUE;
        }
        return 0;
    }
    case WM_CLOSE:
        if (st) { st->result.ok = false; st->done = true; DestroyWindow(hwnd); }
        return 0;
    case WM_DESTROY:
        if (st) st->done = true;
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void EnsureClass(HINSTANCE hi) {
    static bool done = false;
    if (done) return;
    if (!g_blankIcon) g_blankIcon = MakeBlankIcon(16);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TextDlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.hIcon = g_blankIcon;
    wc.hIconSm = g_blankIcon;
    wc.lpszClassName = kTextClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

TextDialogResult TextDialog::Show(HWND owner, COLORREF initialColor, TextAnn* existing) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);

    TextDlgState st;
    st.color = existing ? existing->style.color : initialColor;
    st.alpha = existing ? existing->style.alpha : Settings().drawAlpha;
    st.bgColor = existing ? existing->bgColor : RGB(255, 255, 255);
    st.staticBrush = CreateSolidBrush(RGB(250, 250, 250));

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    const wchar_t* caption = existing ? L"编辑文字" : L"插入文字";

    RECT wr = { 0, 0, kW, kH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_TOPMOST;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int outerW = wr.right - wr.left;
    int outerH = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExW(exStyle, kTextClass, caption, style,
                                (sw - outerW) / 2, (sh - outerH) / 2, outerW, outerH,
                                owner, nullptr, hi, &st);
    if (!hwnd) {
        if (st.staticBrush) DeleteObject(st.staticBrush);
        return st.result;
    }
    SetWindowTextW(hwnd, caption);
    if (g_blankIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_blankIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_blankIcon));
    }

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    // 标签：无边框、无特殊底色
    auto mkLabel = [&](const wchar_t* text, int x, int y, int w, int hh, int id = 0) -> HWND {
        HWND hwndCtl = CreateWindowW(L"STATIC", text,
                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     x, y, w, hh, hwnd,
                                     id ? Hm(id) : nullptr, hi, nullptr);
        if (font) SendMessageW(hwndCtl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return hwndCtl;
    };

    mkLabel(L"文字内容", 20, 14, 100, 22);
    // 默认不显示纵向滚动条；内容过多时 ES_AUTOVSCROLL 仍可滚动编辑
    HWND text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                existing ? existing->text.c_str() : L"",
                                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                20, 38, kW - 56, 120, hwnd, Hm(TXC_EDIT), hi, nullptr);

    mkLabel(L"字号", 20, 174, 48, 22);
    wchar_t szbuf[16];
    swprintf_s(szbuf, L"%d", existing ? static_cast<int>(existing->fontSize) : 20);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", szbuf,
                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    70, 170, 70, 28, hwnd, Hm(TXC_FONTSIZE), hi, nullptr);

    HWND bold = CreateWindowW(L"BUTTON", L"加粗 (Ctrl+B)",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              160, 170, 130, 28, hwnd, Hm(TXC_BOLD), hi, nullptr);
    if (existing && existing->bold) SendMessageW(bold, BM_SETCHECK, BST_CHECKED, 0);

    HWND trans = CreateWindowW(L"BUTTON", L"背景透明",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              310, 170, 110, 28, hwnd, Hm(TXC_TRANS), hi, nullptr);
    bool transDefault = existing ? existing->transparentBg : true;
    SendMessageW(trans, BM_SETCHECK, transDefault ? BST_CHECKED : BST_UNCHECKED, 0);

    mkLabel(L"文字颜色", 20, 216, 80, 22);
    st.colorBtn = CreateWindowW(L"BUTTON", L"",
                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                                100, 210, 48, 28, hwnd, Hm(TXC_COLOR), hi, nullptr);

    st.bgColorLabel = mkLabel(L"背景颜色", 180, 216, 80, 22, TXC_BGCOLOR);
    // label shouldn't be clickable for color - use static id 0
    SetWindowLongPtrW(st.bgColorLabel, GWLP_ID, 0);
    st.bgColorBtn = CreateWindowW(L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  260, 210, 48, 28, hwnd, Hm(TXC_BGCOLOR), hi, nullptr);

    // 紧凑：按钮紧挨颜色行下方
    HWND ok = CreateWindowW(L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            kW - 240, 260, 100, 36, hwnd, Hm(TXC_OK), hi, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kW - 120, 260, 100, 36, hwnd, Hm(TXC_CANCEL), hi, nullptr);

    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    (void)ok;
    (void)cancel;

    UpdateBgColorVisibility(&st);
    RefreshSwatches(&st);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(text);

    MSG msg;
    while (!st.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0 || r == -1) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (font) DeleteObject(font);
    if (st.staticBrush) DeleteObject(st.staticBrush);
    return st.result;
}
