#include "app.h"
#include "settings.h"
#include "settingsdlg.h"
#include "overlay.h"
#include "colorpicker.h"
#include "version.h"

using namespace Gdiplus;

namespace {
const wchar_t* kMainClass = L"ScreenshotToolMainWindow";
const int kHotkeyId = 1;

bool IsDark() { return Settings().IsDarkTheme(); }

const wchar_t* kTipClass = L"ScreenshotToolTooltip";

void EnsureTipClass(HINSTANCE hi) {
    static bool reg = false;
    if (reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hi;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_INFOBK + 1));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kTipClass;
    RegisterClassExW(&wc);
    reg = true;
}
} // namespace

App& App::Instance() {
    static App app;
    return app;
}

bool App::Init(HINSTANCE hi, int nCmdShow) {
    hi_ = hi;
    dpi_ = 96;
    Settings().Load();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = App::WndProc;
    wc.hInstance = hi_;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return false;

    dpi_ = 96;
    RECT desk;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desk, 0);
    int w = util::Scale(1000, dpi_);
    int h = util::Scale(700, dpi_);
    int x = desk.left + ((desk.right - desk.left) - w) / 2;
    int y = desk.top + ((desk.bottom - desk.top) - h) / 2;

    std::wstring title = std::wstring(APP_NAME) + L" " + APP_VERSION;
    hwnd_ = CreateWindowExW(WS_EX_APPWINDOW,
                            kMainClass, title.c_str(),
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, w, h,
                            nullptr, nullptr, hi_, this);
    if (!hwnd_) return false;

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    UpdateHotkey();
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Global shortcuts when canvas doesn't have focus
        if (msg.message == WM_KEYDOWN) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl && msg.wParam == 'Z') {
                if (GetKeyState(VK_SHIFT) & 0x8000) Canvas::Instance().Redo();
                else Canvas::Instance().Undo();
                continue;
            }
            if (ctrl && msg.wParam == 'Y') {
                Canvas::Instance().Redo();
                continue;
            }
            if (ctrl && msg.wParam == 'C') {
                Canvas::Instance().CopySelection();
                continue;
            }
            if (ctrl && msg.wParam == 'V') {
                Canvas::Instance().PasteFromBuffer();
                continue;
            }
            if (ctrl && msg.wParam == 'S') {
                if (activeIdx_ >= 0) SaveDoc(activeIdx_);
                continue;
            }
            if (ctrl && msg.wParam == 'B') {
                // bold hint for text tool - handled in dialog
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_SIZE:
        OnSize();
        return 0;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wParam);
        return 0;
    case WM_HOTKEY:
        OnHotkey();
        return 0;
    case WM_APP_CAPTURE_DONE:
        OnCaptureFinished();
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        OnRButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        hoverLeft_ = -1;
        hoverTop_ = -1;
        HideTooltip();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int idx = HitLeftButton(x, y);
        if (idx >= 0 && leftBtns_[idx].id == ID_TOOL_NUMBER) {
            ShowNumberMenu(leftBtns_[idx].rc.left, leftBtns_[idx].rc.bottom);
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        // Minimize to taskbar by default; only exit via tray/right-click or Alt+F4 with confirm
        if (MessageBoxW(hwnd,
                        L"确定退出截图工具吗？\n（最小化可继续在后台待命）",
                        APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, kHotkeyId);
        PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        RECT* r = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        BuildToolbars();
        LayoutChildren();
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void App::OnCreate() {
    dpi_ = util::GetDpiForWindowSafe(hwnd_);
    BuildToolbars();
    Canvas::Instance().Create(hwnd_, hi_);
    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                              WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                              0, 0, 0, 0, hwnd_, nullptr, hi_, nullptr);
    SelectTool(Tool::Select);
    ApplyTheme();
    UpdateStatus();
}

void App::BuildToolbars() {
    topBtns_.clear();
    leftBtns_.clear();

    auto addTop = [&](int id, const wchar_t* text) {
        ToolButton b;
        b.id = id;
        b.text = text;
        b.tip = text;
        b.isLeft = false;
        topBtns_.push_back(b);
    };
    addTop(ID_CMD_CAPTURE, L"截图");
    addTop(ID_CMD_MOSAIC, L"马赛克");
    addTop(ID_CMD_SETTINGS, L"设置");
    addTop(ID_CMD_SAVE_ALL, L"全部保存");
    addTop(ID_CMD_UNDO, L"撤销");
    addTop(ID_CMD_REDO, L"重做");

    struct L { int id; const wchar_t* tip; Tool tool; bool toggle; bool num; };
    const L left[] = {
        { ID_TOOL_SELECT,   L"选择（框选区域）", Tool::Select, true, false },
        { ID_TOOL_BRUSH,    L"笔刷",            Tool::Brush, true, false },
        { ID_TOOL_VIEW,     L"查看模式",         Tool::View, true, false },
        { ID_TOOL_TEXT,     L"插入文字",         Tool::Text, true, false },
        { ID_TOOL_ARROW,    L"箭头",            Tool::Arrow, true, false },
        { ID_TOOL_LINE,     L"直线",            Tool::Line, true, false },
        { ID_TOOL_PEN,      L"画笔",            Tool::Freehand, true, false },
        { ID_TOOL_RECT,     L"矩形框",          Tool::Rect, true, false },
        { ID_TOOL_ROUND,    L"圆角矩形框",       Tool::RoundRect, true, false },
        { ID_TOOL_ELLIPSE,  L"椭圆",            Tool::Ellipse, true, false },
        { ID_TOOL_FRECT,    L"实心矩形",         Tool::FilledRect, true, false },
        { ID_TOOL_FROUND,   L"实心圆角矩形",     Tool::FilledRoundRect, true, false },
        { ID_TOOL_FELLIPSE, L"实心椭圆",         Tool::FilledEllipse, true, false },
        { ID_TOOL_NUMBER,   L"序号（点击选择 1-20）", Tool::Number, true, true },
        { ID_CMD_COLOR,     L"颜色",            Tool::Select, false, false },
    };
    for (auto& item : left) {
        ToolButton b;
        b.id = item.id;
        b.tip = item.tip;
        b.text = item.num ? std::to_wstring(numberIndex_ + 1) : item.tip;
        b.tool = item.tool;
        b.toggle = item.toggle;
        b.isLeft = true;
        b.showNumber = item.num;
        leftBtns_.push_back(b);
    }
}

void App::DrawToolIcon(Graphics& g, const ToolButton& b, const RECT& rc,
                       COLORREF iconColor, COLORREF accent) const {
    const REAL cx = (rc.left + rc.right) * 0.5f;
    const REAL cy = (rc.top + rc.bottom) * 0.5f;
    Color ink = ToGpColor(iconColor);
    Color acc = ToGpColor(accent);
    Pen pen(ink, 1.8f);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    Pen penThin(ink, 1.4f);
    SolidBrush br(ink);
    SolidBrush brAcc(acc);

    switch (b.id) {
    case ID_TOOL_SELECT: {
        Pen dash(ink, 1.6f);
        dash.SetDashStyle(DashStyleDash);
        g.DrawRectangle(&dash, cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        g.DrawLine(&pen, cx + 2.0f, cy + 2.0f, cx + 9.0f, cy + 12.0f);
        g.DrawLine(&pen, cx + 2.0f, cy + 2.0f, cx + 12.0f, cy + 4.0f);
        g.DrawLine(&pen, cx + 4.0f, cy + 8.0f, cx + 9.0f, cy + 12.0f);
        break;
    }
    case ID_TOOL_BRUSH: {
        g.DrawLine(&pen, cx - 8.0f, cy + 8.0f, cx + 2.0f, cy - 2.0f);
        g.FillEllipse(&br, cx + 2.0f, cy - 8.0f, 8.0f, 8.0f);
        break;
    }
    case ID_TOOL_VIEW: {
        g.DrawEllipse(&penThin, cx - 11.0f, cy - 7.0f, 22.0f, 14.0f);
        g.FillEllipse(&br, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        break;
    }
    case ID_TOOL_TEXT: {
        FontFamily family(L"Segoe UI");
        Font font(&family, 16.0f, FontStyleBold, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(L"T", 1, &font, RectF(cx - 12.0f, cy - 12.0f, 24.0f, 24.0f), &fmt, &tb);
        break;
    }
    case ID_TOOL_ARROW: {
        g.DrawLine(&pen, cx - 9.0f, cy + 7.0f, cx + 8.0f, cy - 7.0f);
        PointF head[3] = {
            PointF(cx + 9.0f, cy - 8.0f),
            PointF(cx + 2.0f, cy - 6.0f),
            PointF(cx + 6.0f, cy - 1.0f)
        };
        g.FillPolygon(&br, head, 3);
        break;
    }
    case ID_TOOL_LINE: {
        g.DrawLine(&pen, cx - 9.0f, cy + 8.0f, cx + 9.0f, cy - 8.0f);
        break;
    }
    case ID_TOOL_PEN: {
        g.DrawLine(&pen, cx - 9.0f, cy + 9.0f, cx + 5.0f, cy - 5.0f);
        g.DrawLine(&penThin, cx + 4.0f, cy - 4.0f, cx + 9.0f, cy - 9.0f);
        g.DrawLine(&penThin, cx + 5.0f, cy - 8.0f, cx + 8.0f, cy - 5.0f);
        break;
    }
    case ID_TOOL_RECT: {
        g.DrawRectangle(&pen, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_ROUND: {
        GraphicsPath path;
        path.AddArc(cx - 10.0f, cy - 8.0f, 12.0f, 12.0f, 180.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy - 8.0f, 12.0f, 12.0f, 270.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy + 2.0f, 12.0f, 12.0f, 0.0f, 90.0f);
        path.AddArc(cx - 10.0f, cy + 2.0f, 12.0f, 12.0f, 90.0f, 90.0f);
        path.CloseFigure();
        g.DrawPath(&pen, &path);
        break;
    }
    case ID_TOOL_ELLIPSE: {
        g.DrawEllipse(&pen, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_FRECT: {
        g.FillRectangle(&br, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_FROUND: {
        GraphicsPath path;
        path.AddArc(cx - 10.0f, cy - 8.0f, 12.0f, 12.0f, 180.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy - 8.0f, 12.0f, 12.0f, 270.0f, 90.0f);
        path.AddArc(cx + 2.0f, cy + 2.0f, 12.0f, 12.0f, 0.0f, 90.0f);
        path.AddArc(cx - 10.0f, cy + 2.0f, 12.0f, 12.0f, 90.0f, 90.0f);
        path.CloseFigure();
        g.FillPath(&br, &path);
        break;
    }
    case ID_TOOL_FELLIPSE: {
        g.FillEllipse(&br, cx - 10.0f, cy - 8.0f, 20.0f, 16.0f);
        break;
    }
    case ID_TOOL_NUMBER: {
        g.DrawEllipse(&penThin, cx - 10.0f, cy - 10.0f, 20.0f, 20.0f);
        FontFamily family(L"Segoe UI");
        Font font(&family, 12.0f, FontStyleBold, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        std::wstring n = std::to_wstring(numberIndex_ + 1);
        g.DrawString(n.c_str(), -1, &font, RectF(cx - 12.0f, cy - 12.0f, 24.0f, 24.0f), &fmt, &tb);
        break;
    }
    case ID_CMD_COLOR: {
        COLORREF c = Canvas::Instance().GetDrawColor();
        SolidBrush sw(ToGpColor(c));
        g.FillRectangle(&sw, cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        g.DrawRectangle(&penThin, cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        g.DrawLine(&penThin, cx + 4.0f, cy + 4.0f, cx + 10.0f, cy - 4.0f);
        break;
    }
    default: {
        FontFamily family(L"Microsoft YaHei");
        Font font(&family, 12.0f, FontStyleRegular, UnitPixel);
        SolidBrush tb(ink);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(b.text.c_str(), -1, &font,
                     RectF(static_cast<REAL>(rc.left), static_cast<REAL>(rc.top),
                           static_cast<REAL>(rc.right - rc.left),
                           static_cast<REAL>(rc.bottom - rc.top)),
                     &fmt, &tb);
        break;
    }
    }
    (void)brAcc;
}

void App::ShowTooltip(int x, int y, const std::wstring& text) {
    if (text.empty()) return;
    HINSTANCE hi = hi_ ? hi_ : GetModuleHandleW(nullptr);
    EnsureTipClass(hi);
    if (!tipHwnd_) {
        tipHwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_TRANSPARENT,
                                    kTipClass, L"",
                                    WS_POPUP | WS_BORDER,
                                    0, 0, 10, 10,
                                    hwnd_, nullptr, hi, nullptr);
    }
    if (!tipHwnd_) return;
    tipText_ = text;
    SetWindowTextW(tipHwnd_, text.c_str());

    HDC hdc = GetDC(tipHwnd_);
    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HGDIOBJ old = SelectObject(hdc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(hdc, old);
    DeleteObject(font);
    ReleaseDC(tipHwnd_, hdc);

    int w = sz.cx + 16;
    int h = sz.cy + 12;
    POINT pt = { x + 16, y + 8 };
    ClientToScreen(hwnd_, &pt);
    SetWindowPos(tipHwnd_, HWND_TOPMOST, pt.x, pt.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(tipHwnd_, nullptr, TRUE);
    // simple paint via WM_PAINT default - draw text ourselves
    HDC tdc = GetDC(tipHwnd_);
    RECT rc; GetClientRect(tipHwnd_, &rc);
    FillRect(tdc, &rc, GetSysColorBrush(COLOR_INFOBK));
    SetBkMode(tdc, TRANSPARENT);
    SetTextColor(tdc, GetSysColor(COLOR_INFOTEXT));
    HFONT f2 = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
    HGDIOBJ o2 = SelectObject(tdc, f2);
    DrawTextW(tdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(tdc, o2);
    DeleteObject(f2);
    ReleaseDC(tipHwnd_, tdc);
}

void App::HideTooltip() {
    if (tipHwnd_) ShowWindow(tipHwnd_, SW_HIDE);
}

void App::ShowNumberMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    for (int i = 1; i <= 20; ++i) {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", i);
        UINT flags = MF_STRING | (i == numberIndex_ + 1 ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, ID_NUM_BASE + i - 1, buf);
    }
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);
    SelectTool(Tool::Number);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::LayoutChildren() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    topH_ = util::Scale(48, dpi_);
    leftW_ = util::Scale(56, dpi_);
    tabH_ = util::Scale(32, dpi_);
    if (status_) {
        SendMessageW(status_, WM_SIZE, 0, 0);
        RECT sr;
        GetWindowRect(status_, &sr);
        statusH_ = sr.bottom - sr.top;
    }

    // top buttons
    int pad = util::Scale(6, dpi_);
    int bx = pad;
    int by = (topH_ - util::Scale(30, dpi_)) / 2;
    int bw = util::Scale(72, dpi_);
    int bh = util::Scale(30, dpi_);
    int gap = util::Scale(6, dpi_);
    for (auto& b : topBtns_) {
        b.rc = { bx, by, bx + bw, by + bh };
        bx += bw + gap;
    }

    // left buttons
    int lx = pad;
    int ly = topH_ + pad;
    int lw = leftW_ - pad * 2;
    int lh = util::Scale(36, dpi_);
    int lgap = util::Scale(3, dpi_);
    for (auto& b : leftBtns_) {
        b.rc = { lx, ly, lx + lw, ly + lh };
        ly += lh + lgap;
    }

    int canvasTop = topH_;
    int canvasLeft = leftW_;
    int canvasRight = rc.right;
    int canvasBottom = rc.bottom - tabH_ - statusH_;
    if (canvasBottom < canvasTop + 40) canvasBottom = canvasTop + 40;

    HWND canvas = Canvas::Instance().Hwnd();
    if (canvas) {
        MoveWindow(canvas, canvasLeft, canvasTop,
                   canvasRight - canvasLeft,
                   canvasBottom - canvasTop, TRUE);
        Canvas::Instance().UpdateScrollBars();
        Canvas::Instance().Refresh();
    }
}

void App::OnSize() {
    LayoutChildren();
    if (status_) {
        int parts[] = { 220, 400, 520, -1 };
        SendMessageW(status_, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
        UpdateStatus();
    }
}

void App::ApplyTheme() {
    if (!hwnd_) return;
    InvalidateRect(hwnd_, nullptr, TRUE);
    Canvas::Instance().Refresh();
}

Document* App::ActiveDoc() {
    if (activeIdx_ < 0 || activeIdx_ >= static_cast<int>(docs_.size())) return nullptr;
    return docs_[activeIdx_].get();
}

void App::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) { EndPaint(hwnd_, &ps); return; }

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bm = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bm);

    Graphics g(mem);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    auto& s = Settings();
    COLORREF bg = s.BgColor();
    COLORREF panel = s.PanelColor();
    COLORREF text = s.TextColor();
    COLORREF border = s.BorderColor();

    // top bar
    SolidBrush panelBr(ToGpColor(panel));
    SolidBrush bgBr(ToGpColor(bg));
    g.FillRectangle(&bgBr, 0, 0, w, h);
    g.FillRectangle(&panelBr, 0, 0, w, topH_);
    // left bar
    g.FillRectangle(&panelBr, 0, topH_, leftW_, h - topH_);

    FontFamily family(L"Microsoft YaHei");
    Font font(&family, 13, FontStyleRegular, UnitPixel);
    Font fontSmall(&family, 12, FontStyleRegular, UnitPixel);
    SolidBrush textBr(ToGpColor(text));
    Pen borderPen(ToGpColor(border), 1);

    auto drawBtn = [&](const ToolButton& b, bool active, bool hover, bool leftRail) {
        int bw = b.rc.right - b.rc.left;
        int bh = b.rc.bottom - b.rc.top;

        if (leftRail) {
            // Default: no filled background — only icon on panel
            COLORREF iconColor = s.TextColor();
            if (active) {
                SolidBrush hi(ToGpColor(s.themeColor, 40));
                g.FillRectangle(&hi, b.rc.left, b.rc.top, bw, bh);
                Pen edge(ToGpColor(s.themeColor), 2);
                g.DrawLine(&edge, b.rc.left + 1, b.rc.top + 4, b.rc.left + 1, b.rc.bottom - 4);
                iconColor = s.themeColor;
            } else if (hover) {
                SolidBrush hi(ToGpColor(s.IsDarkTheme() ? RGB(70, 70, 70) : RGB(230, 230, 235)));
                g.FillRectangle(&hi, b.rc.left, b.rc.top, bw, bh);
            }
            DrawToolIcon(g, b, b.rc, iconColor, s.themeColor);
            return;
        }

        // top bar buttons: keep text
        Color fill = hover
            ? ToGpColor(s.IsDarkTheme() ? RGB(70, 70, 70) : RGB(230, 230, 235))
            : ToGpColor(s.IsDarkTheme() ? RGB(50, 50, 50) : RGB(245, 245, 245));
        if (active) fill = ToGpColor(s.themeColor);
        SolidBrush br(fill);
        g.FillRectangle(&br, b.rc.left, b.rc.top, bw, bh);
        Pen p(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&p, b.rc.left, b.rc.top, bw - 1, bh - 1);
        Color tc = active ? Color(255, 255, 255, 255) : ToGpColor(s.TextColor());
        SolidBrush tbr(tc);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF layout(static_cast<REAL>(b.rc.left), static_cast<REAL>(b.rc.top),
                     static_cast<REAL>(bw), static_cast<REAL>(bh));
        g.DrawString(b.text.c_str(), -1, &font, layout, &fmt, &tbr);
    };

    Tool cur = Canvas::Instance().GetTool();
    for (size_t i = 0; i < topBtns_.size(); ++i)
        drawBtn(topBtns_[i], false, static_cast<int>(i) == hoverTop_, false);
    for (size_t i = 0; i < leftBtns_.size(); ++i) {
        bool active = leftBtns_[i].toggle && leftBtns_[i].tool == cur;
        drawBtn(leftBtns_[i], active, static_cast<int>(i) == hoverLeft_, true);
    }

    // tab bar background
    int tabTop = h - statusH_ - tabH_;
    SolidBrush tabBg(ToGpColor(s.IsDarkTheme() ? RGB(40, 40, 40) : RGB(230, 230, 230)));
    g.FillRectangle(&tabBg, 0, tabTop, w, tabH_);
    g.DrawLine(&borderPen, 0, tabTop, w, tabTop);

    // tabs
    int tx = leftW_ + 8;
    int ty = tabTop + 4;
    int th = tabH_ - 8;
    for (size_t i = 0; i < docs_.size(); ++i) {
        int tw = util::Scale(72, dpi_);
        RECT trc = { tx, ty, tx + tw, ty + th };
        bool active = static_cast<int>(i) == activeIdx_;
        Color tc = active ? ToGpColor(s.themeColor) : ToGpColor(s.IsDarkTheme() ? RGB(55, 55, 55) : RGB(250, 250, 250));
        SolidBrush tb(tc);
        g.FillRectangle(&tb, trc.left, trc.top, tw, th);
        Pen tp(ToGpColor(s.BorderColor()), 1);
        g.DrawRectangle(&tp, trc.left, trc.top, tw - 1, th - 1);
        Color fc = active ? Color(255, 255, 255) : ToGpColor(text);
        SolidBrush fb(fc);
        StringFormat fmt;
        fmt.SetAlignment(StringAlignmentCenter);
        fmt.SetLineAlignment(StringAlignmentCenter);
        RectF lr(static_cast<REAL>(trc.left), static_cast<REAL>(trc.top),
                 static_cast<REAL>(tw), static_cast<REAL>(th));
        g.DrawString(docs_[i]->name.c_str(), -1, &fontSmall, lr, &fmt, &fb);
        tx += tw + 4;
    }

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void App::OnCommand(int id) {
    // number selection menu
    if (id >= ID_NUM_BASE && id < ID_NUM_BASE + 20) {
        numberIndex_ = id - ID_NUM_BASE;
        Canvas::Instance().SetNumber(numberIndex_ + 1);
        for (auto& b : leftBtns_) {
            if (b.id == ID_TOOL_NUMBER) b.text = std::to_wstring(numberIndex_ + 1);
        }
        SelectTool(Tool::Number);
        InvalidateRect(hwnd_, nullptr, FALSE);
        ShowStatusMessage(L"序号：" + std::to_wstring(numberIndex_ + 1));
        return;
    }

    if (id >= ID_TOOL_SELECT && id <= ID_TOOL_NUMBER) {
        for (auto& b : leftBtns_) {
            if (b.id == id) {
                if (id == ID_TOOL_NUMBER) {
                    // open 1-20 dropdown menu near the button
                    ShowNumberMenu(b.rc.left, b.rc.bottom);
                    return;
                }
                SelectTool(b.tool);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return;
            }
        }
    }

    switch (id) {
    case ID_CMD_CAPTURE: StartCapture(); break;
    case ID_CMD_MOSAIC: Canvas::Instance().ApplyMosaicToSelection(); break;
    case ID_CMD_SETTINGS: OpenSettings(); break;
    case ID_CMD_SAVE_ALL: SaveAllDocs(); break;
    case ID_CMD_UNDO: Canvas::Instance().Undo(); break;
    case ID_CMD_REDO: Canvas::Instance().Redo(); break;
    case ID_CMD_SAVE: if (activeIdx_ >= 0) SaveDoc(activeIdx_); break;
    case ID_CMD_COLOR: OpenColorPicker(); break;
    case ID_CMD_COPY: Canvas::Instance().CopySelection(); break;
    case ID_CMD_PASTE: Canvas::Instance().PasteFromBuffer(); break;
    case ID_MENU_CLOSE: if (activeIdx_ >= 0) CloseDoc(activeIdx_); break;
    case ID_MENU_SAVE: if (activeIdx_ >= 0) SaveDoc(activeIdx_); break;
    case ID_MENU_SAVE_ALL: SaveAllDocs(); break;
    case ID_MENU_CLOSE_OTHERS: {
        int keep = activeIdx_;
        for (int i = static_cast<int>(docs_.size()) - 1; i >= 0; --i) {
            if (i != keep) CloseDoc(i);
        }
        break;
    }
    default:
        if (id >= ID_TAB_BASE && id < ID_TAB_BASE + static_cast<int>(docs_.size()) + 8) {
            int idx = id - ID_TAB_BASE;
            if (idx >= 0 && idx < static_cast<int>(docs_.size())) ActivateDoc(idx);
        }
        break;
    }
}

void App::OnKeyDown(WPARAM vk) {
    if (vk == VK_ESCAPE) {
        if (CaptureOverlay::Instance().IsOpen()) {
            CaptureOverlay::Instance().Cancel();
            return;
        }
    }
    if (vk == VK_DELETE) {
        Canvas::Instance().DeleteSelection();
    }
}

void App::OnHotkey() {
    StartCapture();
}

void App::StartCapture() {
    if (CaptureOverlay::Instance().IsOpen()) return;
    CaptureOverlay::Instance().Start(hwnd_);
}

void App::OnCaptureFinished() {
    auto bmp = CaptureOverlay::Instance().TakeResult();
    if (!bmp) return;
    AddDocument(std::move(bmp));
    ShowStatusMessage(L"截图完成，已新建页签");
}

void App::AddDocument(std::unique_ptr<Bitmap> bmp) {
    if (!bmp) return;
    wchar_t name[16];
    swprintf_s(name, L"P%d", nextDocId_++);
    auto doc = std::make_unique<Document>(std::move(bmp),
                                          nextDocId_ - 1,
                                          std::wstring(name));
    docs_.push_back(std::move(doc));
    activeIdx_ = static_cast<int>(docs_.size()) - 1;
    Canvas::Instance().SetDocument(docs_[activeIdx_].get());
    Canvas::Instance().ResetZoom();
    UpdateTabBar();
    UpdateTitle();
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ActivateDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;
    activeIdx_ = idx;
    Canvas::Instance().SetDocument(docs_[idx].get());
    UpdateTabBar();
    UpdateTitle();
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::CloseDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;
    docs_.erase(docs_.begin() + idx);
    if (activeIdx_ >= static_cast<int>(docs_.size()))
        activeIdx_ = static_cast<int>(docs_.size()) - 1;
    if (activeIdx_ >= 0)
        Canvas::Instance().SetDocument(docs_[activeIdx_].get());
    else
        Canvas::Instance().SetDocument(nullptr);
    UpdateTabBar();
    UpdateTitle();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::SaveDoc(int idx) {
    if (idx < 0 || idx >= static_cast<int>(docs_.size())) return;
    Document* d = docs_[idx].get();
    std::wstring def = d->name + L".png";
    std::wstring path = util::OpenSaveDialog(
        hwnd_, true,
        L"PNG 图片\0*.png\0JPG 图片\0*.jpg\0所有文件\0*.*\0",
        L"png", def.c_str(), L"保存截图");
    if (path.empty()) return;
    bool jpg = path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".jpg") == 0;
    if (d->SaveAs(path, jpg)) {
        ShowStatusMessage(L"已保存：" + path);
    } else {
        MessageBoxW(hwnd_, L"保存失败", APP_NAME, MB_ICONERROR);
    }
}

void App::SaveAllDocs() {
    if (docs_.empty()) {
        ShowStatusMessage(L"没有可保存的页签");
        return;
    }
    std::wstring dir = util::BrowseFolder(hwnd_, L"选择保存目录");
    if (dir.empty()) return;

    int choice = MessageBoxW(hwnd_, L"是否使用 JPG 格式？\n（否 = PNG）", APP_NAME,
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    if (choice == IDCANCEL) return;
    bool jpg = (choice == IDYES);

    int n = 0;
    for (auto& d : docs_) {
        std::wstring path = dir + L"\\" + d->name + (jpg ? L".jpg" : L".png");
        if (d->SaveAs(path, jpg)) ++n;
    }
    ShowStatusMessage(util::Format(L"已保存 %d 张到 %s", n, dir.c_str()));
}

void App::SelectTool(Tool t) {
    Canvas::Instance().SetTool(t);
    if (t == Tool::Number) Canvas::Instance().SetNumber(numberIndex_ + 1);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateStatus();
}

void App::OpenColorPicker() {
    COLORREF c = Canvas::Instance().GetDrawColor();
    BYTE a = Settings().drawAlpha;
    auto r = ColorPicker::Show(hwnd_, c, a);
    if (r.ok) {
        Canvas::Instance().SetDrawColor(r.color);
        Canvas::Instance().SetAlpha(r.alpha);
        Settings().drawColor = r.color;
        Settings().drawAlpha = r.alpha;
        Settings().Save();
        // update color button text
        for (auto& b : leftBtns_) {
            if (b.id == ID_CMD_COLOR) {
                b.text = L"色";
            }
        }
        ShowStatusMessage(L"当前颜色：" + util::ToHex(r.color));
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::OpenSettings() {
    SettingsDialog::Show(hwnd_);
}

void App::OnSettingsChanged() {
    UpdateHotkey();
    ApplyTheme();
    BuildToolbars();
    LayoutChildren();
    InvalidateRect(hwnd_, nullptr, TRUE);
    UpdateTitle();
}

void App::UpdateHotkey() {
    UnregisterHotKey(hwnd_, kHotkeyId);
    UINT mods = Settings().hotkeyModifiers | 0x4000; // MOD_NOREPEAT
    if (!RegisterHotKey(hwnd_, kHotkeyId, mods, Settings().hotkeyVk)) {
        RegisterHotKey(hwnd_, kHotkeyId, Settings().hotkeyModifiers, Settings().hotkeyVk);
    }
}

void App::UpdateTitle() {
    std::wstring title = std::wstring(APP_NAME) + L" " + APP_VERSION;
    if (Document* d = ActiveDoc()) {
        title += L" — " + d->name + L" (" + std::to_wstring(d->Width()) + L"×" + std::to_wstring(d->Height()) + L")";
    }
    SetWindowTextW(hwnd_, title.c_str());
}

void App::UpdateTabBar() {
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::UpdateStatus() {
    if (!status_) return;
    Document* d = ActiveDoc();
    std::wstring p1 = statusMsg_.empty() ? L"就绪" : statusMsg_;
    std::wstring p2;
    std::wstring p3;
    std::wstring p4;
    if (d) {
        float z = d->zoom * 100.0f;
        p2 = util::Format(L"页签 %s  ·  %d×%d  ·  缩放 %.0f%%",
                          d->name.c_str(), d->Width(), d->Height(), z);
        int rx, ry, rw, rh;
        if (d->GetRegion(rx, ry, rw, rh)) {
            p3 = util::Format(L"框选 %d×%d  ·  可马赛克/复制", rw, rh);
        } else if (d->GetSelected()) {
            RectF b;
            d->GetSelected()->GetBounds(b);
            p3 = util::Format(L"选中 %d×%d  ·  %s",
                              static_cast<int>(b.Width), static_cast<int>(b.Height),
                              AnnTypeLabel(d->GetSelected()->type));
        } else if (hasHover_) {
            p3 = util::Format(L"坐标 (%.0f, %.0f)", hoverIx_, hoverIy_);
        }
        p4 = L"颜色 " + util::ToHex(Canvas::Instance().GetDrawColor());
    } else {
        p2 = L"无文档";
        p4 = L"颜色 " + util::ToHex(Canvas::Instance().GetDrawColor());
    }
    SendMessageW(status_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(p1.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(p2.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(p3.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(p4.c_str()));
}

void App::ShowStatusMessage(const std::wstring& msg) {
    statusMsg_ = msg;
    UpdateStatus();
}

void App::SetCanvasHover(float ix, float iy) {
    hoverIx_ = ix;
    hoverIy_ = iy;
    hasHover_ = true;
    // throttle status updates lightly
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last > 80) {
        last = now;
        UpdateStatus();
    }
}

int App::HitTopButton(int x, int y) const {
    POINT pt = { x, y };
    for (size_t i = 0; i < topBtns_.size(); ++i) {
        if (PtInRect(&topBtns_[i].rc, pt)) return static_cast<int>(i);
    }
    return -1;
}

int App::HitLeftButton(int x, int y) const {
    POINT pt = { x, y };
    for (size_t i = 0; i < leftBtns_.size(); ++i) {
        if (PtInRect(&leftBtns_[i].rc, pt)) return static_cast<int>(i);
    }
    return -1;
}

int App::HitTab(int x, int y) const {
    RECT rc;
    GetClientRect(hwnd_, const_cast<RECT*>(&rc));
    int h = rc.bottom;
    int tabTop = h - statusH_ - tabH_;
    if (y < tabTop || y >= tabTop + tabH_) return -1;
    int tx = leftW_ + 8;
    int tw = util::Scale(72, dpi_);
    for (size_t i = 0; i < docs_.size(); ++i) {
        RECT trc = { tx, tabTop + 4, tx + tw, tabTop + 4 + tabH_ - 8 };
        if (PtInRect(&trc, { x, y })) return static_cast<int>(i);
        tx += tw + 4;
    }
    return -1;
}

void App::OnLButtonDown(int x, int y) {
    int top = HitTopButton(x, y);
    if (top >= 0) {
        OnCommand(topBtns_[top].id);
        return;
    }
    int left = HitLeftButton(x, y);
    if (left >= 0) {
        OnCommand(leftBtns_[left].id);
        return;
    }
    int tab = HitTab(x, y);
    if (tab >= 0) {
        ActivateDoc(tab);
        return;
    }
}

void App::OnRButtonDown(int x, int y) {
    int tab = HitTab(x, y);
    if (tab >= 0) {
        ActivateDoc(tab);
        CreateTabMenu(tab, x, y);
        return;
    }
}

void App::OnMouseMove(int x, int y) {
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    TrackMouseEvent(&tme);

    int oldLeft = hoverLeft_;
    int oldTop = hoverTop_;
    hoverLeft_ = HitLeftButton(x, y);
    hoverTop_ = HitTopButton(x, y);

    if (hoverLeft_ >= 0) {
        ShowTooltip(x, y, leftBtns_[hoverLeft_].tip);
    } else if (hoverTop_ >= 0) {
        ShowTooltip(x, y, topBtns_[hoverTop_].tip);
    } else {
        HideTooltip();
    }

    if (oldLeft != hoverLeft_ || oldTop != hoverTop_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::CreateTabMenu(int tabIdx, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_MENU_SAVE, L"保存");
    AppendMenuW(menu, MF_STRING, ID_MENU_SAVE_ALL, L"全部保存");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_MENU_CLOSE, L"关闭");
    AppendMenuW(menu, MF_STRING, ID_MENU_CLOSE_OTHERS, L"关闭其他");
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}
