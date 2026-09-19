#include "overlay.h"
#include "settings.h"
#include "version.h"

using namespace Gdiplus;

namespace {
const wchar_t* kOverlayClass = L"ScreenshotToolCaptureOverlay";
bool g_classRegistered = false;

void EnsureClass(HINSTANCE hi) {
    if (g_classRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CaptureOverlay::WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);
    g_classRegistered = true;
}
} // namespace

CaptureOverlay& CaptureOverlay::Instance() {
    static CaptureOverlay inst;
    return inst;
}

LRESULT CALLBACK CaptureOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CaptureOverlay& self = CaptureOverlay::Instance();
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        self.OnPaint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        self.OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        self.OnLButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        self.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        InvalidateRect(hwnd, nullptr, FALSE); // 刷新自定义光标
        return 0;
    case WM_KEYDOWN:
        self.OnKey(wParam);
        return 0;
    case WM_RBUTTONDOWN:
        self.Cancel();
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != hwnd) self.Cancel();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void CaptureOverlay::CaptureVirtualScreen() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
        x = 0; y = 0;
    }

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ old = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    screen_ = std::make_unique<Bitmap>(hbm, nullptr);
    DeleteObject(hbm);

    // store origin offset in sel_ temporarily via window placement
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, MAKELPARAM(static_cast<WORD>(static_cast<short>(x)),
                                                       static_cast<WORD>(static_cast<short>(y))));
    // Better: keep offsets as members. Use member vars.
    // We'll store via screen origin on window pos itself (overlay covers virtual screen at x,y)
}

void CaptureOverlay::Start(HWND owner) {
    if (hwnd_) return;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
        x = 0; y = 0;
    }

    // Hide owner so it's not in the capture
    bool wasVisible = owner && IsWindowVisible(owner);
    if (wasVisible) ShowWindow(owner, SW_HIDE);

    // Hide our tooltip / other tool windows that may still be on screen
    HWND tip = FindWindowW(L"ScreenshotToolTooltip", nullptr);
    if (tip && IsWindowVisible(tip)) ShowWindow(tip, SW_HIDE);

    // Brief delay so hide completes
    Sleep(80);

    // Capture screen without cursor
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, w, h);
    HGDIOBJ old = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
    SelectObject(hdcMem, old);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    screen_ = std::make_unique<Bitmap>(hbm, nullptr);
    DeleteObject(hbm);

    owner_ = owner;
    hasResult_ = false;
    result_.reset();
    dragging_ = false;
    memset(&sel_, 0, sizeof(sel_));

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kOverlayClass, L"",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, hi, this);

    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        SetFocus(hwnd_);
        SetCapture(hwnd_);
        // 保持系统光标可见，便于框选
        while (ShowCursor(TRUE) < 0) {}
    } else if (owner && wasVisible) {
        ShowWindow(owner, SW_SHOW);
    }
}

void CaptureOverlay::Cancel() {
    if (!hwnd_) return;
    ReleaseCapture();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    dragging_ = false;
    screen_.reset();
    if (owner_ && IsWindow(owner_)) {
        ShowWindow(owner_, SW_SHOW);
        SetForegroundWindow(owner_);
    }
}

std::unique_ptr<Bitmap> CaptureOverlay::TakeResult() {
    hasResult_ = false;
    return std::move(result_);
}

void CaptureOverlay::OnPaint(HDC hdc) {
    if (!hwnd_ || !screen_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right, h = rc.bottom;

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(hdcMem, hbm);

    Graphics g(hdcMem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);

    // Dark mask
    g.DrawImage(screen_.get(), 0, 0, w, h);
    SolidBrush mask(Color(120, 0, 0, 0));
    g.FillRectangle(&mask, 0, 0, w, h);

    if (dragging_ || (sel_.right > sel_.left || sel_.bottom > sel_.top)) {
        int sx = (std::min)(sel_.left, sel_.right);
        int sy = (std::min)(sel_.top, sel_.bottom);
        int sw = std::abs(sel_.right - sel_.left);
        int sh = std::abs(sel_.bottom - sel_.top);
        if (sw > 0 && sh > 0) {
            // Show selected region undimmed
            g.DrawImage(screen_.get(),
                        Rect(sx, sy, sw, sh),
                        sx, sy, sw, sh, UnitPixel);
            Pen pen(Color(255, 0, 174, 255), 1.5f);
            g.DrawRectangle(&pen, sx, sy, sw, sh);

            // Size tip
            wchar_t tip[64];
            swprintf_s(tip, L"%d × %d", sw, sh);
            FontFamily family(L"Segoe UI");
            Font font(&family, 14, FontStyleBold, UnitPixel);
            RectF layout;
            g.MeasureString(tip, -1, &font, PointF(0, 0), &layout);
            float tx = static_cast<float>(sx);
            float ty = static_cast<float>(sy) - layout.Height - 8.0f;
            if (ty < 4) ty = static_cast<float>(sy) + 8.0f;
            if (tx + layout.Width + 16 > w) tx = static_cast<float>(w) - layout.Width - 16;
            if (tx < 4) tx = 4;
            SolidBrush tipBg(Color(220, 20, 20, 20));
            g.FillRectangle(&tipBg, tx, ty, layout.Width + 16, layout.Height + 10);
            SolidBrush tipFg(Color(255, 255, 255, 255));
            g.DrawString(tip, -1, &font, PointF(tx + 8, ty + 5), &tipFg);

            // Crosshair coordinates
            wchar_t pos[64];
            swprintf_s(pos, L"(%d, %d)", sx, sy);
            g.MeasureString(pos, -1, &font, PointF(0, 0), &layout);
            float px = static_cast<float>(sx);
            float py = ty + layout.Height + 14;
            g.FillRectangle(&tipBg, px, py, layout.Width + 16, layout.Height + 10);
            g.DrawString(pos, -1, &font, PointF(px + 8, py + 5), &tipFg);
        }
    }

    // Hint
    {
        FontFamily family(L"Microsoft YaHei");
        Font font(&family, 13, FontStyleRegular, UnitPixel);
        const wchar_t* hint = L"拖动鼠标框选截图区域  ·  Esc 取消";
        RectF layout;
        g.MeasureString(hint, -1, &font, PointF(0, 0), &layout);
        float hx = (w - layout.Width) * 0.5f;
        float hy = static_cast<float>(h) - layout.Height - 28.0f;
        SolidBrush bg(Color(200, 20, 20, 20));
        g.FillRectangle(&bg, hx - 16, hy - 8, layout.Width + 32, layout.Height + 16);
        SolidBrush fg(Color(255, 240, 240, 240));
        g.DrawString(hint, -1, &font, PointF(hx, hy), &fg);
    }

    // 高对比自定义十字光标，暗色遮罩下也能看清
    {
        POINT cpt;
        GetCursorPos(&cpt);
        ScreenToClient(hwnd_, &cpt);
        const REAL cx = static_cast<REAL>(cpt.x);
        const REAL cy = static_cast<REAL>(cpt.y);
        const REAL arm = 14.0f;
        Pen shadow(Color(255, 0, 0, 0), 4.0f);
        Pen light(Color(255, 255, 255, 255), 2.0f);
        Pen accent(Color(255, 0, 200, 255), 1.0f);
        g.DrawLine(&shadow, cx - arm, cy, cx + arm, cy);
        g.DrawLine(&shadow, cx, cy - arm, cx, cy + arm);
        g.DrawLine(&light, cx - arm, cy, cx + arm, cy);
        g.DrawLine(&light, cx, cy - arm, cx, cy + arm);
        g.DrawLine(&accent, cx - 4, cy, cx + 4, cy);
        g.DrawLine(&accent, cx, cy - 4, cx, cy + 4);
        g.DrawEllipse(&light, cx - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }

    BitBlt(hdc, 0, 0, w, h, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
}

void CaptureOverlay::OnLButtonDown(int x, int y) {
    dragging_ = true;
    startX_ = curX_ = x;
    startY_ = curY_ = y;
    sel_.left = x; sel_.top = y; sel_.right = x; sel_.bottom = y;
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CaptureOverlay::OnMouseMove(int x, int y) {
    if (!dragging_) return;
    curX_ = x; curY_ = y;
    sel_.left = startX_; sel_.top = startY_;
    sel_.right = x; sel_.bottom = y;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void CaptureOverlay::OnLButtonUp(int x, int y) {
    if (!dragging_) return;
    dragging_ = false;
    sel_.left = startX_; sel_.top = startY_;
    sel_.right = x; sel_.bottom = y;

    int sx = (std::min)(sel_.left, sel_.right);
    int sy = (std::min)(sel_.top, sel_.bottom);
    int sw = std::abs(sel_.right - sel_.left);
    int sh = std::abs(sel_.bottom - sel_.top);
    if (sw < 3 || sh < 3) {
        // too small — cancel
        Cancel();
        return;
    }
    FinishCapture();
}

void CaptureOverlay::OnKey(WPARAM vk) {
    if (vk == VK_ESCAPE) Cancel();
}

void CaptureOverlay::FinishCapture() {
    if (!screen_) { Cancel(); return; }

    int sx = (std::min)(sel_.left, sel_.right);
    int sy = (std::min)(sel_.top, sel_.bottom);
    int sw = std::abs(sel_.right - sel_.left);
    int sh = std::abs(sel_.bottom - sel_.top);

    result_ = util::CropBitmap(screen_.get(), sx, sy, sw, sh);
    hasResult_ = result_ != nullptr;

    HWND owner = owner_;
    ReleaseCapture();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    screen_.reset();

    if (owner && IsWindow(owner)) {
        ShowWindow(owner, SW_SHOW);
        SetForegroundWindow(owner);
        if (hasResult_) {
            PostMessageW(owner, WM_APP + 1, 0, 0); // capture finished
        }
    }
}
