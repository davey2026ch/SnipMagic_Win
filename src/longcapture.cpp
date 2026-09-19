#include "longcapture.h"
#include "settings.h"
#include "version.h"
#include "overlay.h"
#include <cmath>

using namespace Gdiplus;

namespace {

const wchar_t* kBarClass = L"ScreenshotToolLongCaptureBar";
const wchar_t* kBorderClass = L"ScreenshotToolLongCaptureBorder";
const wchar_t* kTimerClass = L"ScreenshotToolLongCaptureTimer";
bool g_lcClassReg = false;

const int kIdDone = 4001;
const int kIdCancel = 4002;
const UINT kTimerCapture = 10;
constexpr UINT kCaptureIntervalMs = 150; // 更密采样：快滚时重叠更多，便于对齐
constexpr UINT kFirstFrameDelayMs = 350;
constexpr int kMaxFailStreak = 3;
// 两行控制条：上排状态+按钮，下排整行提示，避免被按钮遮挡
constexpr int kBarW = 640;
constexpr int kBarH = 64;
constexpr int kStatusX = 18;
constexpr int kStatusY = 4;
constexpr int kStatusW = 340;
constexpr int kStatusH = 26;
constexpr int kTipX = 18;
constexpr int kTipY = 32;
constexpr int kTipW = 604;
constexpr int kTipH = 26;
constexpr int kBtnDoneX = 370;
constexpr int kBtnCancelX = 490;
constexpr int kBtnW = 120;
constexpr int kBtnY = 3;
constexpr int kBtnH = 28;

void EnsureLcClass(HINSTANCE hi) {
    if (g_lcClassReg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    wc.lpfnWndProc = LongCapture::WndProc;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
    wc.lpszClassName = kBarClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = LongCapture::BorderProc;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kBorderClass;
    RegisterClassExW(&wc);

    wc.lpfnWndProc = LongCapture::WndProc;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kTimerClass;
    RegisterClassExW(&wc);

    g_lcClassReg = true;
}

void SetExcludeFromCapture(HWND hwnd, bool exclude) {
    if (!hwnd || !IsWindow(hwnd)) return;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    typedef BOOL(WINAPI* Fn)(HWND, DWORD);
    auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "SetWindowDisplayAffinity"));
    if (fn) fn(hwnd, exclude ? 0x11 : 0x00);
}

LRESULT CALLBACK SessionKeyProc(int nCode, WPARAM wParam, LPARAM lParam) {
    LongCapture& self = LongCapture::Instance();
    if (nCode == HC_ACTION && self.IsActive() &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && lParam) {
        const auto* ks = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (ks->vkCode == VK_ESCAPE) {
            self.Cancel();
            return 1;
        }
        if (ks->vkCode == VK_RETURN) {
            self.Finish();
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

double NowSec() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

void PlaceOnRegionMonitor(const RECT& region, int& x, int& y, int w, int h) {
    HMONITOR mon = MonitorFromRect(&region, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    x = region.left;
    y = region.bottom + 10;
    if (y + h > mi.rcWork.bottom) {
        y = region.top - h - 10;
        if (y < mi.rcWork.top) y = mi.rcWork.top + 8;
    }
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w - 8;
    if (x < mi.rcWork.left) x = mi.rcWork.left + 8;
}

} // namespace

LongCapture& LongCapture::Instance() {
    static LongCapture inst;
    return inst;
}

LRESULT CALLBACK LongCapture::BorderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const int bw = 3;
        HPEN pen = CreatePen(PS_SOLID, bw, RGB(0, 145, 255));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, bw / 2, bw / 2, rc.right - bw / 2, rc.bottom - bw / 2);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT CALLBACK LongCapture::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LongCapture& self = LongCapture::Instance();
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        LongCapture& self = LongCapture::Instance();
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, self.barFg_);
        static HBRUSH brLight = CreateSolidBrush(RGB(232, 243, 255));
        static HBRUSH brDark = CreateSolidBrush(RGB(40, 44, 52));
        return reinterpret_cast<LRESULT>(self.darkTheme_ ? brDark : brLight);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kIdDone) {
            self.Finish();
            return 0;
        }
        if (LOWORD(wParam) == kIdCancel) {
            self.Cancel();
            return 0;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kTimerCapture) self.OnTimer();
        return 0;
    case WM_PAINT:
        self.OnPaint(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { self.Cancel(); return 0; }
        if (wParam == VK_RETURN) { self.Finish(); return 0; }
        return 0;
    case WM_CLOSE:
        self.Cancel();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

HWND LongCapture::CreateBar(HINSTANCE hi) {
    HWND bar = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kBarClass, L"长截图",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, kBarW, kBarH,
        nullptr, nullptr, hi, nullptr);
    if (!bar) return nullptr;

    HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                             L"Microsoft YaHei");
    HFONT tipFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                L"Microsoft YaHei");
    statusH_ = CreateWindowW(L"STATIC", statusText_.c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                             kStatusX, kStatusY, kStatusW, kStatusH,
                             bar, nullptr, hi, nullptr);
    if (statusH_) {
        SendMessageW(statusH_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    tipH_ = CreateWindowW(L"STATIC", tipText_.c_str(),
                          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                          kTipX, kTipY, kTipW, kTipH,
                          bar, nullptr, hi, nullptr);
    if (tipH_) {
        SendMessageW(tipH_, WM_SETFONT, reinterpret_cast<WPARAM>(tipFont), TRUE);
    }

    btnDone_ = CreateWindowW(L"BUTTON", L"完成",
                             WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                             kBtnDoneX, kBtnY, kBtnW, kBtnH, bar,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDone)), hi, nullptr);
    btnCancel_ = CreateWindowW(L"BUTTON", L"取消",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               kBtnCancelX, kBtnY, kBtnW, kBtnH, bar,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)), hi, nullptr);
    HFONT btnFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                                L"Microsoft YaHei");
    if (btnDone_) SendMessageW(btnDone_, WM_SETFONT, reinterpret_cast<WPARAM>(btnFont), TRUE);
    if (btnCancel_) SendMessageW(btnCancel_, WM_SETFONT, reinterpret_cast<WPARAM>(btnFont), TRUE);
    (void)font;
    (void)tipFont;
    return bar;
}

void LongCapture::Start(HWND owner, const RECT& regionScreen) {
    if (active_) return;
    const int w = regionScreen.right - regionScreen.left;
    const int h = regionScreen.bottom - regionScreen.top;
    if (w < 20 || h < 20) {
        if (owner && IsWindow(owner)) {
            CaptureOverlay::UncloakAndShow(owner);
            SetForegroundWindow(owner);
            MessageBoxW(owner, L"长截图区域过小，请重新框选（至少 20×20）。",
                        APP_NAME, MB_ICONWARNING);
        }
        return;
    }

    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureLcClass(hi);

    owner_ = owner;
    region_ = regionScreen;
    result_.reset();
    lastGaps_ = 0;
    lastSuspects_ = 0;
    alignFailHint_ = false;
    stitcher_ = std::make_unique<longstitch::Stitcher>(w);
    captureInFlight_ = false;
    finishing_ = false;
    failStreak_ = 0;
    firstTickDone_ = false;
    active_ = true;
    startTick_ = GetTickCount();
    lastTick_ = startTick_;
    ApplyBarTheme();
    statusText_ = L"长截图进行中";
    tipText_ = L"滚动页面截长图（建议匀速、稍慢）";

    if (bar_) {
        // 控制条创建后按主题重绘
        InvalidateRect(bar_, nullptr, TRUE);
    }

    // 边框：镂空，只描选区外圈
    const int bw = 3;
    border_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kBorderClass, L"",
        WS_POPUP | WS_VISIBLE,
        region_.left - bw, region_.top - bw, w + bw * 2, h + bw * 2,
        nullptr, nullptr, hi, nullptr);
    if (border_) {
        const int ow = w + bw * 2;
        const int oh = h + bw * 2;
        HRGN outer = CreateRectRgn(0, 0, ow, oh);
        HRGN inner = CreateRectRgn(bw, bw, ow - bw, oh - bw);
        if (outer && inner) {
            CombineRgn(outer, outer, inner, RGN_DIFF);
            SetWindowRgn(border_, outer, TRUE);
            outer = nullptr;
        }
        if (outer) DeleteObject(outer);
        if (inner) DeleteObject(inner);
        SetExcludeFromCapture(border_, true);
        ShowWindow(border_, SW_SHOWNA);
        SetWindowPos(border_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    // 控制条：必须可见，放在选区所在显示器
    bar_ = CreateBar(hi);
    if (bar_) {
        LayoutUi();
        SetExcludeFromCapture(bar_, true);
        ShowWindow(bar_, SW_SHOW);
        SetWindowPos(bar_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        // 提到前台，确保用户看得到完成/取消
        SetForegroundWindow(bar_);
        UpdateWindow(bar_);
        InvalidateRect(bar_, nullptr, TRUE);
    }

    // 定时器宿主：优先控制条；失败则建隐藏窗，保证抓帧循环仍运行
    timerHwnd_ = bar_;
    if (!timerHwnd_) {
        timerHwnd_ = CreateWindowExW(
            0, kTimerClass, L"",
            WS_POPUP, -32000, -32000, 8, 8,
            nullptr, nullptr, hi, nullptr);
    }
    if (timerHwnd_) {
        SetTimer(timerHwnd_, kTimerCapture, kFirstFrameDelayMs, nullptr);
    }

    kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, SessionKeyProc, hi, 0);

    // 主窗口保持隐藏
    if (owner_ && IsWindow(owner_)) {
        ShowWindow(owner_, SW_HIDE);
    }

    if (statusH_) SetWindowTextW(statusH_, statusText_.c_str());
    if (tipH_) SetWindowTextW(tipH_, tipText_.c_str());
}

void LongCapture::LayoutUi() {
    int x = 0, y = 0;
    PlaceOnRegionMonitor(region_, x, y, kBarW, kBarH);
    if (bar_) {
        SetWindowPos(bar_, HWND_TOPMOST, x, y, kBarW, kBarH,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (statusH_) MoveWindow(statusH_, kStatusX, kStatusY, kStatusW, kStatusH, TRUE);
        if (tipH_) MoveWindow(tipH_, kTipX, kTipY, kTipW, kTipH, TRUE);
        if (btnDone_) MoveWindow(btnDone_, kBtnDoneX, kBtnY, kBtnW, kBtnH, TRUE);
        if (btnCancel_) MoveWindow(btnCancel_, kBtnCancelX, kBtnY, kBtnW, kBtnH, TRUE);
        InvalidateRect(bar_, nullptr, TRUE);
    }
}

void LongCapture::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    Graphics g(hdc);
    SolidBrush bg(ToGpColor(barBg_));
    g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
    Pen border(ToGpColor(barBorder_), 1.0f);
    g.DrawRectangle(&border, 0, 0, rc.right - 1, rc.bottom - 1);
    SolidBrush accent(ToGpColor(barAccent_));
    g.FillRectangle(&accent, 0, 0, rc.right, 2);
    // 异常提示点（文字由 STATIC 显示）
    if (stitcher_ && (stitcher_->GapCount() > 0 || stitcher_->SuspectSeamCount() > 0)) {
        SolidBrush warn(darkTheme_ ? Color(255, 255, 170, 60) : Color(255, 230, 140, 30));
        g.FillEllipse(&warn, 8.0f, 17.0f, 8.0f, 8.0f);
    }
    EndPaint(hwnd, &ps);
}

void LongCapture::ApplyBarTheme() {
    darkTheme_ = Settings().IsDarkTheme();
    const COLORREF accent = Settings().themeColor;
    if (darkTheme_) {
        barBg_ = RGB(40, 44, 52);       // 深蓝灰
        barFg_ = RGB(230, 234, 240);
        barBorder_ = RGB(70, 78, 92);
        barAccent_ = accent;
    } else {
        // 浅蓝，避免死白
        barBg_ = RGB(232, 243, 255);
        barFg_ = RGB(40, 48, 60);
        barBorder_ = RGB(170, 205, 240);
        barAccent_ = accent;
    }
}

std::wstring LongCapture::BuildStatusText() const {
    if (!stitcher_) return L"长截图进行中";
    return util::Format(L"已拼 %d 行", stitcher_->CanvasRows());
}

std::wstring LongCapture::BuildTipText() const {
    if (!stitcher_) return L"滚动页面截长图（建议匀速、稍慢）";
    const int gaps = stitcher_->GapCount();
    const int suspects = stitcher_->SuspectSeamCount();
    // 完整提示单独占一整行，不与「完成/取消」抢位置
    if (alignFailHint_) {
        return L"未对齐：请上滚一点再向下慢滚（对齐成功后此提示会消失）";
    }
    if (gaps > 0 && suspects > 0) {
        return util::Format(L"缺失 %d 处 · 接缝异常 %d 处 · 建议放慢滚动", gaps, suspects);
    }
    if (gaps > 0) {
        return util::Format(L"缺失 %d 处 · 建议放慢滚动后继续", gaps);
    }
    if (suspects > 0) {
        return util::Format(L"接缝异常 %d 处 · 建议放慢滚动", suspects);
    }
    return L"对齐正常，继续向下滚动即可";
}

void LongCapture::ApplyStatusToUi() {
    statusText_ = BuildStatusText();
    tipText_ = BuildTipText();
    if (statusH_) SetWindowTextW(statusH_, statusText_.c_str());
    if (tipH_) SetWindowTextW(tipH_, tipText_.c_str());
    if (bar_) InvalidateRect(bar_, nullptr, FALSE);
}

void LongCapture::UpdateStatusText() {
    ApplyStatusToUi();
}

bool LongCapture::CaptureRegionFrame() {
    const int x = region_.left;
    const int y = region_.top;
    const int w = region_.right - region_.left;
    const int h = region_.bottom - region_.top;
    if (w <= 0 || h <= 0 || !stitcher_) return false;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbm || !bits) {
        if (hbm) DeleteObject(hbm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }
    HGDIOBJ old = SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, w, h, hdcScreen, x, y, SRCCOPY);
    GdiFlush();

    const int stride = w * 4;
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (int row = 0; row < h; ++row) {
        memcpy(buf.data() + static_cast<size_t>(row) * stride,
               src + static_cast<size_t>(row) * stride,
               static_cast<size_t>(stride));
    }

    SelectObject(hdcMem, old);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    auto frame = longstitch::MakeFrame(buf.data(), w, h);
    const auto ev = stitcher_->Process(frame, NowSec());
    switch (ev) {
    case longstitch::Event::Capped:
        statusText_ = BuildStatusText();
        tipText_ = L"已达长度上限，自动完成…";
        if (statusH_) SetWindowTextW(statusH_, statusText_.c_str());
        if (tipH_) SetWindowTextW(tipH_, tipText_.c_str());
        finishing_ = true;
        break;
    case longstitch::Event::NeedOverlap:
        alignFailHint_ = true;
        ApplyStatusToUi();
        break;
    case longstitch::Event::ScrolledUp:
        // 用户按提示回滚后，改成可执行指令；成功拼接前保持提示
        alignFailHint_ = true;
        statusText_ = BuildStatusText();
        tipText_ = L"已回滚 · 请继续向下慢滚以重新对齐";
        if (statusH_) SetWindowTextW(statusH_, statusText_.c_str());
        if (tipH_) SetWindowTextW(tipH_, tipText_.c_str());
        if (bar_) InvalidateRect(bar_, nullptr, FALSE);
        break;
    case longstitch::Event::Appended:
    case longstitch::Event::Gap:
    case longstitch::Event::Started:
        alignFailHint_ = false;
        ApplyStatusToUi();
        break;
    case longstitch::Event::Invalid:
        return false;
    default:
        // 未对齐期间不要被 UpdateStatusText 盖掉回滚提示
        if (!alignFailHint_) {
            ApplyStatusToUi();
        } else {
            statusText_ = BuildStatusText();
            if (statusH_) SetWindowTextW(statusH_, statusText_.c_str());
        }
        break;
    }
    return true;
}

void LongCapture::OnTimer() {
    if (!active_) return;
    if (captureInFlight_) return;
    if (finishing_) {
        Finish();
        return;
    }

    const DWORD now = GetTickCount();
    if (firstTickDone_ && (now - lastTick_) < kCaptureIntervalMs) {
        return;
    }

    captureInFlight_ = true;
    const bool ok = CaptureRegionFrame();
    captureInFlight_ = false;
    lastTick_ = now;
    firstTickDone_ = true;

    if (!ok) {
        ++failStreak_;
        if (failStreak_ >= kMaxFailStreak) {
            MessageBoxW(bar_ ? bar_ : owner_, L"连续抓帧失败，长截图已取消。",
                        APP_NAME, MB_ICONWARNING);
            Cancel();
            return;
        }
    } else {
        failStreak_ = 0;
    }

    if (timerHwnd_ && !finishing_ && active_) {
        SetTimer(timerHwnd_, kTimerCapture, kCaptureIntervalMs, nullptr);
    }
}

void LongCapture::Finish() {
    if (!active_ || !stitcher_) return;
    if (captureInFlight_) {
        finishing_ = true;
        return;
    }

    captureInFlight_ = true;
    finishing_ = true;

    std::vector<uint8_t> lastBuf;
    longstitch::FrameData lastFrame;
    const int w = region_.right - region_.left;
    const int h = region_.bottom - region_.top;
    if (w > 0 && h > 0) {
        HDC hdcScreen = GetDC(nullptr);
        if (hdcScreen) {
            HDC hdcMem = CreateCompatibleDC(hdcScreen);
            BITMAPINFO bi = {};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            HBITMAP hbm = CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (hbm && bits) {
                HGDIOBJ old = SelectObject(hdcMem, hbm);
                BitBlt(hdcMem, 0, 0, w, h, hdcScreen, region_.left, region_.top, SRCCOPY);
                GdiFlush();
                lastBuf.resize(static_cast<size_t>(w) * h * 4);
                memcpy(lastBuf.data(), bits, lastBuf.size());
                SelectObject(hdcMem, old);
                lastFrame = longstitch::MakeFrame(lastBuf.data(), w, h);
            }
            if (hbm) DeleteObject(hbm);
            if (hdcMem) DeleteDC(hdcMem);
            ReleaseDC(nullptr, hdcScreen);
        }
    }

    std::vector<uint8_t> out;
    int ow = 0, oh = 0;
    const longstitch::FrameData* lastPtr = lastFrame.w > 0 ? &lastFrame : nullptr;
    const bool ok = stitcher_->Finish(lastPtr, out, ow, oh);
    lastGaps_ = stitcher_->GapCount();
    lastSuspects_ = stitcher_->SuspectSeamCount();
    captureInFlight_ = false;

    if (ok && ow > 0 && oh > 0 && out.size() >= static_cast<size_t>(ow) * oh * 4) {
        auto bmp = std::make_unique<Bitmap>(ow, oh, PixelFormat32bppARGB);
        BitmapData data;
        Rect rc(0, 0, ow, oh);
        if (bmp->LockBits(&rc, ImageLockModeWrite, PixelFormat32bppARGB, &data) == Ok) {
            for (int row = 0; row < oh; ++row) {
                memcpy(static_cast<uint8_t*>(data.Scan0) + static_cast<size_t>(row) * data.Stride,
                       out.data() + static_cast<size_t>(row) * ow * 4,
                       static_cast<size_t>(ow) * 4);
            }
            bmp->UnlockBits(&data);
        }
        result_ = std::move(bmp);
        Cleanup(true, true);
    } else {
        Cleanup(true, false);
    }
}

void LongCapture::Cancel() {
    if (!active_) return;
    if (stitcher_) {
        lastGaps_ = stitcher_->GapCount();
        lastSuspects_ = stitcher_->SuspectSeamCount();
    }
    Cleanup(true, false);
}

std::unique_ptr<Bitmap> LongCapture::TakeResult() {
    return std::move(result_);
}

void LongCapture::Cleanup(bool restoreOwner, bool success) {
    if (timerHwnd_ && IsWindow(timerHwnd_)) {
        KillTimer(timerHwnd_, kTimerCapture);
    }
    if (bar_) {
        DestroyWindow(bar_);
        bar_ = nullptr;
    }
    if (border_) {
        DestroyWindow(border_);
        border_ = nullptr;
    }
    // 独立定时器窗（若不是 bar）
    if (timerHwnd_ && IsWindow(timerHwnd_)) {
        DestroyWindow(timerHwnd_);
    }
    timerHwnd_ = nullptr;
    statusH_ = nullptr;
    tipH_ = nullptr;
    btnDone_ = nullptr;
    btnCancel_ = nullptr;

    if (kbHook_) {
        UnhookWindowsHookEx(reinterpret_cast<HHOOK>(kbHook_));
        kbHook_ = nullptr;
    }

    stitcher_.reset();
    active_ = false;
    captureInFlight_ = false;
    finishing_ = false;

    HWND owner = owner_;
    owner_ = nullptr;

    if (!success) result_.reset();

    if (restoreOwner && owner && IsWindow(owner)) {
        CaptureOverlay::UncloakAndShow(owner);
        ShowWindow(owner, SW_SHOW);
        SetForegroundWindow(owner);
    }

    if (owner && IsWindow(owner)) {
        PostMessageW(owner, WM_APP + 4, success ? 1 : 0, 0);
    }
}
