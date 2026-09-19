#include "textdlg.h"
#include "annotation.h"
#include "colorpicker.h"
#include "settings.h"

using namespace Gdiplus;

namespace {

const int kW = 580;
const int kH = 430;

enum TextCtrlId {
    TXC_EDIT = 2001,
    TXC_FONTSIZE = 2002,
    TXC_BOLD = 2003,
    TXC_TRANS = 2004,
    TXC_COLOR = 2005,
    TXC_OK = 2006,
    TXC_CANCEL = 2007
};

struct TextDlgState {
    HWND hwnd = nullptr;
    TextDialogResult result;
    COLORREF color = RGB(255, 0, 0);
    BYTE alpha = 255;
    HWND colorBtn = nullptr;
    bool done = false;
};

const wchar_t* kTextClass = L"ScreenshotToolTextDlg";

HMENU Hm(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

void PaintColorBtn(HWND hwnd, TextDlgState* st) {
    if (!st || !st->colorBtn) return;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;
    RECT rc;
    GetClientRect(st->colorBtn, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bm);
    {
        Graphics g(mem);
        SolidBrush br(ToGpColor(st->color, st->alpha));
        g.FillRectangle(&br, 0, 0, rc.right, rc.bottom);
        Pen pen(Color(255, 80, 80, 80), 1);
        g.DrawRectangle(&pen, 0, 0, rc.right - 1, rc.bottom - 1);
    }
    HDC btnDc = GetDC(st->colorBtn);
    BitBlt(btnDc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    ReleaseDC(st->colorBtn, btnDc);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);
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
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        if (id == TXC_COLOR) {
            auto r = ColorPicker::Show(hwnd, st->color, st->alpha);
            if (r.ok) {
                st->color = r.color;
                st->alpha = r.alpha;
                PaintColorBtn(hwnd, st);
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
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) PaintColorBtn(hwnd, st);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (st && dis && dis->CtlID == TXC_COLOR) {
            Graphics g(dis->hDC);
            SolidBrush br(ToGpColor(st->color, st->alpha));
            g.FillRectangle(&br, 0, 0, dis->rcItem.right - dis->rcItem.left,
                            dis->rcItem.bottom - dis->rcItem.top);
            Pen pen(Color(255, 80, 80, 80), 1);
            g.DrawRectangle(&pen, 0, 0,
                            dis->rcItem.right - dis->rcItem.left - 1,
                            dis->rcItem.bottom - dis->rcItem.top - 1);
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
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TextDlgProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
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

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    const wchar_t* caption = existing ? L"编辑文字" : L"插入文字";

    // 计算含标题栏的外框尺寸
    RECT wr = { 0, 0, kW, kH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_TOPMOST;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int outerW = wr.right - wr.left;
    int outerH = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExW(exStyle,
                                kTextClass, caption,
                                style,
                                (sw - outerW) / 2, (sh - outerH) / 2, outerW, outerH,
                                owner, nullptr, hi, &st);
    if (!hwnd) return st.result;
    SetWindowTextW(hwnd, caption);

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    CreateWindowW(L"STATIC", L"文字内容：", WS_CHILD | WS_VISIBLE,
                  20, 16, 120, 24, hwnd, nullptr, hi, nullptr);
    HWND text = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                existing ? existing->text.c_str() : L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                20, 44, kW - 60, 150, hwnd, Hm(TXC_EDIT), hi, nullptr);

    CreateWindowW(L"STATIC", L"字号", WS_CHILD | WS_VISIBLE,
                  20, 210, 48, 24, hwnd, nullptr, hi, nullptr);
    wchar_t szbuf[16];
    swprintf_s(szbuf, L"%d", existing ? static_cast<int>(existing->fontSize) : 20);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", szbuf,
                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                    70, 206, 80, 30, hwnd, Hm(TXC_FONTSIZE), hi, nullptr);

    HWND bold = CreateWindowW(L"BUTTON", L"加粗 (Ctrl+B)",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              180, 206, 140, 30, hwnd, Hm(TXC_BOLD), hi, nullptr);
    if (existing && existing->bold) SendMessageW(bold, BM_SETCHECK, BST_CHECKED, 0);

    HWND trans = CreateWindowW(L"BUTTON", L"背景透明",
                               WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              340, 206, 120, 30, hwnd, Hm(TXC_TRANS), hi, nullptr);
    bool transDefault = existing ? existing->transparentBg : true;
    SendMessageW(trans, BM_SETCHECK, transDefault ? BST_CHECKED : BST_UNCHECKED, 0);

    CreateWindowW(L"STATIC", L"颜色", WS_CHILD | WS_VISIBLE,
                  20, 256, 48, 24, hwnd, nullptr, hi, nullptr);
    st.colorBtn = CreateWindowW(L"BUTTON", L"",
                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                70, 250, 56, 36, hwnd, Hm(TXC_COLOR), hi, nullptr);

    // 按钮上移，保证完整显示
    HWND ok = CreateWindowW(L"BUTTON", L"确定",
                            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            kW - 240, 320, 100, 36, hwnd, Hm(TXC_OK), hi, nullptr);
    HWND cancel = CreateWindowW(L"BUTTON", L"取消",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                kW - 120, 320, 100, 36, hwnd, Hm(TXC_CANCEL), hi, nullptr);

    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    (void)ok;
    (void)cancel;

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
    return st.result;
}
