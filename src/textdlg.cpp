#include "textdlg.h"
#include "annotation.h"
#include "colorpicker.h"
#include "darkui.h"

using namespace Gdiplus;

namespace {

// 初始客户区 = 原尺寸(560x340)的 1.5 倍，便于输入长文本；窗口可拖动边框缩放
const int kW = 840;
const int kH = 510;
// 窗口最小客户区（拖动缩放的下限）：宽度要放得下底部那一整行控件
const int kMinW = 700;
const int kMinH = 400;

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
    HWND contentLabel = nullptr;
    HWND edit = nullptr;
    HWND fontLabel = nullptr;
    HWND fontEdit = nullptr;
    HWND bold = nullptr;
    HWND trans = nullptr;
    HWND colorLabel = nullptr;
    HWND colorBtn = nullptr;
    HWND bgColorBtn = nullptr;
    HWND bgColorLabel = nullptr;
    HWND ok = nullptr;
    HWND cancel = nullptr;
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
    // 颜色入口统一为「设置」按钮，不再绘制色块（白色时色块不可见）
    (void)btn; (void)c; (void)alpha;
}

void RefreshSwatches(TextDlgState* st) {
    (void)st;
}

// 按当前客户区尺寸重排所有子控件（窗口可拖动缩放）
void LayoutTextDlg(TextDlgState* st) {
    if (!st || !st->hwnd) return;
    RECT rc;
    GetClientRect(st->hwnd, &rc);
    const int W = rc.right, H = rc.bottom;
    const int m = 20;
    const int btnW = 100, btnH = 34;
    const int btnY = H - m - btnH;
    // 底部只有一行：字号 / 加粗 / 背景透明 / 文字颜色 / 背景颜色
    const int rowY = btnY - 44;
    const int editTop = 40;
    int editH = rowY - 12 - editTop;
    if (editH < 60) editH = 60;

    auto move = [](HWND h, int x, int y, int w, int hh) {
        if (h) SetWindowPos(h, nullptr, x, y, w, hh, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    move(st->contentLabel, m, 12, 120, 22);
    move(st->edit, m, editTop, W - 2 * m, editH);

    // 一行排开：字号 [输入] 加粗 背景透明 文字颜色[设置] 背景颜色[设置]
    int x = m;
    move(st->fontLabel, x, rowY + 2, 44, 22);      x += 44 + 2;
    move(st->fontEdit, x, rowY, 66, 28);           x += 66 + 6;
    move(st->bold, x, rowY, 126, 28);              x += 126 + 6;
    move(st->trans, x, rowY, 118, 28);             x += 118 + 8;
    move(st->colorLabel, x, rowY + 2, 74, 22);     x += 74 + 2;
    move(st->colorBtn, x, rowY - 1, 62, 30);       x += 62 + 8;
    move(st->bgColorLabel, x, rowY + 2, 74, 22);   x += 74 + 2;
    move(st->bgColorBtn, x, rowY - 1, 62, 30);

    move(st->cancel, W - m - btnW, btnY, btnW, btnH);
    move(st->ok, W - m - 2 * btnW - 12, btnY, btnW, btnH);
}

void UpdateBgColorVisibility(TextDlgState* st) {
    if (!st || !st->hwnd) return;
    BOOL transparent = SendMessageW(GetDlgItem(st->hwnd, TXC_TRANS), BM_GETCHECK, 0, 0) == BST_CHECKED;
    int show = transparent ? SW_HIDE : SW_SHOW;
    if (st->bgColorBtn) ShowWindow(st->bgColorBtn, show);
    if (st->bgColorLabel) ShowWindow(st->bgColorLabel, show);
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
    case WM_ERASEBKGND:
        return darkui::EraseBg(hwnd, reinterpret_cast<HDC>(wParam));
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return darkui::CtlColor(msg, reinterpret_cast<HDC>(wParam));
    case WM_SIZE:
        LayoutTextDlg(st);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_GETMINMAXINFO: {
        // 拖动缩放的下限：避免把弹窗缩到控件重叠、无法使用
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT r = { 0, 0, kMinW, kMinH };
        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        DWORD ex = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectEx(&r, style, FALSE, ex);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_COMMAND: {
        if (!st) return 0;
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        if (id == TXC_TRANS && code == BN_CLICKED) {
            UpdateBgColorVisibility(st);
            return 0;
        }
        if (id == TXC_COLOR || id == TXC_BGCOLOR) {
            if (code != BN_CLICKED && code != 0) return 0;
            static bool s_inColor = false;
            if (s_inColor) return 0;
            s_inColor = true;
            if (id == TXC_COLOR) {
                auto r = ColorPicker::Show(hwnd, st->color, st->alpha);
                s_inColor = false;
                if (r.ok) {
                    st->color = r.color;
                    st->alpha = r.alpha;
                    RefreshSwatches(st);
                }
            } else {
                // 背景颜色取色器按当前背景色初始化（背景不涉及透明度）
                auto r = ColorPicker::Show(hwnd, st->bgColor, 255);
                s_inColor = false;
                if (r.ok) {
                    st->bgColor = r.color;
                    RefreshSwatches(st);
                }
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
    case WM_DRAWITEM:
        // 颜色入口均为「设置」普通按钮，无需 owner-draw 色块
        return 0;
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
    wc.hbrBackground = nullptr; // 背景由 WM_ERASEBKGND 按主题绘制
    wc.hIcon = g_blankIcon;
    wc.hIconSm = g_blankIcon;
    wc.lpszClassName = kTextClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace

TextDialogResult TextDialog::Show(HWND owner, TextAnn* existing) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);

    TextDlgState st;
    if (existing) {
        // 编辑已有文字：使用该文字自身的颜色
        st.color = existing->style.color;
        st.alpha = existing->style.alpha;
        st.bgColor = existing->bgColor;
    } else {
        // 插入新文字：使用弹窗固定的默认色（红字 / 白底 / 不透明），
        // 与左侧工具栏的颜色设置互不联动
        st.color = RGB(255, 0, 0);
        st.alpha = 255;
        st.bgColor = RGB(255, 255, 255);
    }

    // 弹窗显示在主窗口所在的显示器（多屏时不再固定弹到主屏）
    const wchar_t* caption = existing ? L"编辑文字" : L"插入文字";

    // WS_THICKFRAME + WS_MAXIMIZEBOX：整块边框可拖动，用户能自行调整大小
    RECT wr = { 0, 0, kW, kH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX;
    DWORD exStyle = WS_EX_TOPMOST;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    int outerW = wr.right - wr.left;
    int outerH = wr.bottom - wr.top;

    // 初始尺寸不小于最小可用尺寸；屏幕装不下时按工作区收边（布局会自动重排）
    RECT minClient = { 0, 0, kMinW, kMinH };
    AdjustWindowRectEx(&minClient, style, FALSE, exStyle);
    int minOuterW = minClient.right - minClient.left;
    int minOuterH = minClient.bottom - minClient.top;
    if (outerW < minOuterW) outerW = minOuterW;
    if (outerH < minOuterH) outerH = minOuterH;

    RECT wa = {};
    HMONITOR mon = owner ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST) : nullptr;
    MONITORINFO mi = { sizeof(mi) };
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    if (outerW > (wa.right - wa.left) - 24) outerW = (wa.right - wa.left) - 24;
    if (outerH > (wa.bottom - wa.top) - 24) outerH = (wa.bottom - wa.top) - 24;
    if (outerW < minOuterW) outerW = minOuterW;
    if (outerH < minOuterH) outerH = minOuterH;

    POINT pos = util::CenterOnMonitorOf(owner, outerW, outerH);

    HWND hwnd = CreateWindowExW(exStyle, kTextClass, caption, style,
                                pos.x, pos.y, outerW, outerH,
                                owner, nullptr, hi, &st);
    if (!hwnd) {
        return st.result;
    }
    darkui::DarkTitleBar(hwnd);
    SetWindowTextW(hwnd, caption);
    // 标题栏不显示图标，只显示文字
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, 0);
    if (g_blankIcon) {
        SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(nullptr));
        SetClassLongPtrW(hwnd, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(nullptr));
    }

    HFONT font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    auto mkLabel = [&](const wchar_t* text, int x, int y, int w, int hh, int id = 0) -> HWND {
        HWND hwndCtl = CreateWindowW(L"STATIC", text,
                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     x, y, w, hh, hwnd,
                                     id ? Hm(id) : nullptr, hi, nullptr);
        if (font) SendMessageW(hwndCtl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return hwndCtl;
    };

    // 所有控件先按零尺寸创建，随后统一由 LayoutTextDlg 按客户区尺寸摆位
    st.contentLabel = mkLabel(L"文字内容", 0, 0, 100, 22);
    // 内容过多时显示纵向滚动条（WS_VSCROLL + ES_AUTOVSCROLL）
    st.edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                              existing ? existing->text.c_str() : L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                  ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                              0, 0, 100, 100, hwnd, Hm(TXC_EDIT), hi, nullptr);

    st.fontLabel = mkLabel(L"字号", 0, 0, 48, 22);
    wchar_t szbuf[16];
    swprintf_s(szbuf, L"%d", existing ? static_cast<int>(existing->fontSize) : 20);
    st.fontEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", szbuf,
                                  WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                                  0, 0, 70, 28, hwnd, Hm(TXC_FONTSIZE), hi, nullptr);

    st.bold = CreateWindowW(L"BUTTON", L"加粗 (Ctrl+B)",
                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                            0, 0, 130, 28, hwnd, Hm(TXC_BOLD), hi, nullptr);
    if (existing && existing->bold) SendMessageW(st.bold, BM_SETCHECK, BST_CHECKED, 0);

    st.trans = CreateWindowW(L"BUTTON", L"背景透明",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                             0, 0, 130, 28, hwnd, Hm(TXC_TRANS), hi, nullptr);
    bool transDefault = existing ? existing->transparentBg : true;
    SendMessageW(st.trans, BM_SETCHECK, transDefault ? BST_CHECKED : BST_UNCHECKED, 0);

    // 文字颜色 / 背景颜色：同一套「设置」按钮 + ColorPicker，逻辑一致
    // （不用色块，避免白色时用户找不到入口）；按钮与标签文字垂直居中
    st.colorLabel = mkLabel(L"文字颜色", 0, 0, 80, 22);
    st.colorBtn = CreateWindowW(L"BUTTON", L"设置",
                                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                0, 0, 72, 30, hwnd, Hm(TXC_COLOR), hi, nullptr);

    st.bgColorLabel = mkLabel(L"背景颜色", 0, 0, 80, 22);
    SetWindowLongPtrW(st.bgColorLabel, GWLP_ID, 0);
    st.bgColorBtn = CreateWindowW(L"BUTTON", L"设置",
                                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                  0, 0, 72, 30, hwnd, Hm(TXC_BGCOLOR), hi, nullptr);

    st.ok = CreateWindowW(L"BUTTON", L"确定",
                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                          0, 0, 100, 34, hwnd, Hm(TXC_OK), hi, nullptr);
    st.cancel = CreateWindowW(L"BUTTON", L"取消",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              0, 0, 100, 34, hwnd, Hm(TXC_CANCEL), hi, nullptr);

    for (HWND c = GetWindow(hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    LayoutTextDlg(&st);
    UpdateBgColorVisibility(&st);
    RefreshSwatches(&st);
    darkui::ThemeChildren(hwnd);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(st.edit);

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
