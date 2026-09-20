#include "compareview.h"
#include "settings.h"
#include "app.h"

using namespace Gdiplus;

namespace {
const wchar_t* kCompareClass = L"ScreenshotToolCompareView";
bool g_compareClassReg = false;

void RegCompareClass(HINSTANCE hi) {
    if (g_compareClassReg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CompareView::WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCompareClass;
    RegisterClassExW(&wc);
    g_compareClassReg = true;
}

const int kMargin = 16;
} // namespace

CompareView& CompareView::Instance() {
    static CompareView v;
    return v;
}

bool CompareView::Create(HWND parent, HINSTANCE hi) {
    RegCompareClass(hi);
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        kCompareClass, L"",
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 100, 100,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)), hi, this);
    return hwnd_ != nullptr;
}

void CompareView::SetDocument(Document* doc) {
    doc_ = doc;
    Refresh();
}

void CompareView::ShowPane(bool show) {
    visible_ = show;
    if (hwnd_) {
        ShowWindow(hwnd_, show ? SW_SHOW : SW_HIDE);
        if (show) UpdateScrollBars();
    }
}

void CompareView::Refresh() {
    if (hwnd_ && visible_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CompareView::EnsureBackbuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (memDc_ && memW_ == w && memH_ == h) return;
    if (memDc_) {
        SelectObject(memDc_, nullptr);
        DeleteObject(memBm_);
        DeleteDC(memDc_);
        memDc_ = nullptr;
        memBm_ = nullptr;
        bits_ = nullptr;
    }
    HDC hdc = GetDC(hwnd_);
    memDc_ = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memBm_ = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
    SelectObject(memDc_, memBm_);
    memW_ = w;
    memH_ = h;
    ReleaseDC(hwnd_, hdc);
}

void CompareView::UpdateScrollBars() {
    if (!hwnd_ || !visible_) return;

    auto hideBar = [&](int bar) {
        ShowScrollBar(hwnd_, bar, FALSE);
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = 0;
        si.nPos = 0;
        si.nPage = 0;
        SetScrollInfo(hwnd_, bar, &si, TRUE);
    };

    if (!doc_ || !doc_->base) {
        hideBar(SB_HORZ);
        hideBar(SB_VERT);
        return;
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    float z = doc_->zoom;
    int contentW = static_cast<int>(doc_->Width() * z) + kMargin * 2;
    int contentH = static_cast<int>(doc_->Height() * z) + kMargin * 2;

    if (contentW <= rc.right) {
        doc_->scrollX = 0;
        hideBar(SB_HORZ);
    } else {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = contentW - 1;
        si.nPage = static_cast<UINT>(rc.right);
        si.nPos = doc_->scrollX;
        SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd_, SB_HORZ, &si);
        doc_->scrollX = si.nPos;
    }

    if (contentH <= rc.bottom) {
        doc_->scrollY = 0;
        hideBar(SB_VERT);
    } else {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        si.nMin = 0;
        si.nMax = contentH - 1;
        si.nPage = static_cast<UINT>(rc.bottom);
        si.nPos = doc_->scrollY;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd_, SB_VERT, &si);
        doc_->scrollY = si.nPos;
    }
}

void CompareView::ApplyScroll(int scrollX, int scrollY) {
    if (!doc_) return;
    doc_->scrollX = scrollX;
    doc_->scrollY = scrollY;
    UpdateScrollBars();
    Refresh();
}

void CompareView::NotifyAppScrolled() {
    App::Instance().OnComparePaneScrolled();
}

void CompareView::OnCustomScroll(int bar, int pos) {
    SCROLLINFO si = { sizeof(si), SIF_POS };
    si.nPos = pos;
    SetScrollInfo(hwnd_, bar, &si, TRUE);
    GetScrollInfo(hwnd_, bar, &si);
    if (doc_) {
        if (bar == SB_VERT) doc_->scrollY = si.nPos;
        else doc_->scrollX = si.nPos;
    }
    Refresh();
    NotifyAppScrolled();
}

void CompareView::OnScroll(int bar, WPARAM wParam) {
    SCROLLINFO si = { sizeof(si), SIF_ALL };
    GetScrollInfo(hwnd_, bar, &si);
    int pos = si.nPos;
    int code = LOWORD(wParam);
    // SB_LINELEFT==SB_LINEUP==0 etc.; branch by bar axis
    if (bar == SB_HORZ) {
        switch (code) {
        case SB_LINELEFT: pos -= 20; break;
        case SB_LINERIGHT: pos += 20; break;
        case SB_PAGELEFT: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGERIGHT: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = si.nMin; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
        }
    } else {
        switch (code) {
        case SB_LINEUP: pos -= 20; break;
        case SB_LINEDOWN: pos += 20; break;
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = si.nMin; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: break;
        }
    }
    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(hwnd_, bar, &si, TRUE);
    GetScrollInfo(hwnd_, bar, &si);
    if (doc_) {
        if (bar == SB_HORZ) doc_->scrollX = si.nPos;
        else doc_->scrollY = si.nPos;
    }
    Refresh();
    NotifyAppScrolled();
}

void CompareView::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) {
        EndPaint(hwnd_, &ps);
        return;
    }

    EnsureBackbuffer(w, h);
    Graphics g(memDc_);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    COLORREF bg = Settings().CanvasBg();
    SolidBrush bgBrush(ToGpColor(bg));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    FontFamily family(L"Microsoft YaHei");
    Font font(&family, 14, FontStyleRegular, UnitPixel);

    if (!doc_ || !doc_->base) {
        SolidBrush fg(ToGpColor(Settings().TextColor()));
        const wchar_t* msg = L"对比页签内容";
        RectF layout;
        g.MeasureString(msg, -1, &font, PointF(0, 0), &layout);
        g.DrawString(msg, -1, &font,
                     PointF((w - layout.Width) / 2, (h - layout.Height) / 2),
                     &fg);
        BitBlt(hdc, 0, 0, w, h, memDc_, 0, 0, SRCCOPY);
        EndPaint(hwnd_, &ps);
        return;
    }

    float z = doc_->zoom;
    int ix = kMargin - doc_->scrollX;
    int iy = kMargin - doc_->scrollY;
    int iw = static_cast<int>(doc_->Width() * z);
    int ih = static_cast<int>(doc_->Height() * z);

    g.SetInterpolationMode(z >= 1.0f ? InterpolationModeNearestNeighbor
                                      : InterpolationModeHighQualityBicubic);
    g.DrawImage(doc_->base.get(), Rect(ix, iy, iw, ih),
                0, 0, doc_->Width(), doc_->Height(), UnitPixel);

    GraphicsState st = g.Save();
    g.TranslateTransform(static_cast<REAL>(ix), static_cast<REAL>(iy));
    g.ScaleTransform(z, z);
    doc_->DrawAnnotations(g, false);
    g.Restore(st);

    // pane header tag
    SolidBrush tagBr(Color(180, 20, 20, 20));
    SolidBrush tagFg(Color(255, 255, 255, 255));
    Font tagFont(&family, 12, FontStyleRegular, UnitPixel);
    std::wstring tag = L"对比 · " + doc_->name;
    g.FillRectangle(&tagBr, 0, 0, 96, 22);
    g.DrawString(tag.c_str(), -1, &tagFont, PointF(8, 3), &tagFg);

    // 自绘滚动条（替代系统原生条，支持暗色主题）
    scrollui::Draw(g, scrollui::Compute(hwnd_, rc), vsb_, hsb_, Settings().IsDarkTheme());

    BitBlt(hdc, 0, 0, w, h, memDc_, 0, 0, SRCCOPY);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK CompareView::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CompareView* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<CompareView*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<CompareView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CompareView::Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: OnPaint(); return 0;
    case WM_SIZE: UpdateScrollBars(); Refresh(); return 0;
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        auto gm = scrollui::Compute(hwnd, rc);
        if (scrollui::Down(hwnd, gm, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                           vsb_, hsb_,
                           [this](int bar, int pos) { OnCustomScroll(bar, pos); })) {
            return 0;
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        scrollui::Move(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                       vsb_, hsb_,
                       [this](int bar, int pos) { OnCustomScroll(bar, pos); });
        return 0;
    case WM_LBUTTONUP:
        if (scrollui::Up(vsb_, hsb_)) ReleaseCapture();
        return 0;
    case WM_HSCROLL: OnScroll(SB_HORZ, wParam); return 0;
    case WM_VSCROLL: OnScroll(SB_VERT, wParam); return 0;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        // Shift+wheel → horizontal
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
            SendMessageW(hwnd, WM_HSCROLL,
                         MAKEWPARAM(delta > 0 ? SB_LINELEFT : SB_LINERIGHT, 0), 0);
        } else {
            SendMessageW(hwnd, WM_VSCROLL,
                         MAKEWPARAM(delta > 0 ? SB_LINEUP : SB_LINEDOWN, 0), 0);
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
