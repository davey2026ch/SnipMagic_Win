#include "gifrecord.h"
#include "settings.h"
#include "app.h"
#include "version.h"
#include <mmsystem.h>
#include <cstring>

using namespace Gdiplus;

namespace {

const wchar_t* kRecClass = L"SnipMagicGifRecorder";
bool g_reg = false;

// 内容区外圈的边框粗细（这一圈会一直盖在桌面上，所以尽量窄）
const int kBorder = 2;

void EnsureClass(HINSTANCE hi) {
    if (g_reg) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = GifRecorder::WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // 自绘：内容区本来就是空的
    wc.lpszClassName = kRecClass;
    RegisterClassExW(&wc);
    g_reg = true;
}

void RectToScreen(HWND hwnd, RECT& r) {
    POINT tl = { r.left, r.top };
    POINT br = { r.right, r.bottom };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    r = { tl.x, tl.y, br.x, br.y };
}

} // namespace

GifRecorder& GifRecorder::Instance() {
    static GifRecorder inst;
    return inst;
}

bool GifRecorder::IsBusy() const {
    return hwnd_ && (state_ == State::Recording || state_ == State::Paused);
}

int GifRecorder::BarHeight() const {
    return util::Scale(46, dpi_);
}

void GifRecorder::Open(HWND owner) {
    if (hwnd_ && IsWindow(hwnd_)) {
        SetForegroundWindow(hwnd_);
        return;
    }
    owner_ = owner;
    // 先藏主窗口再建录制窗：这样框选/录制期间画面里不会出现软件自己
    if (owner_ && IsWindow(owner_)) {
        WINDOWPLACEMENT wp = { sizeof(wp) };
        if (GetWindowPlacement(owner_, &wp)) {
            ownerShowCmd_ = (wp.showCmd == SW_SHOWMINIMIZED) ? SW_SHOWMINIMIZED : wp.showCmd;
        }
        ShowWindow(owner_, SW_HIDE);
    }
    BuildWindow(owner);
    if (!hwnd_) {
        // 建窗失败就别把主窗口留在隐藏状态
        RestoreOwner();
        owner_ = nullptr;
    }
}

void GifRecorder::BuildWindow(HWND owner) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    EnsureClass(hi);
    dpi_ = owner ? util::GetDpiForWindowSafe(owner) : 96;

    // 默认选区：700×400（逻辑像素，与界面其它尺寸同一套 DPI 缩放），居中。
    // 只做起步大小，打开后仍可拖边框随意放大；屏幕太小则收到工作区的 90%。
    POINT cur = {0, 0};
    GetCursorPos(&cur);
    HMONITOR mon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    RECT work = mi.rcWork;
    const int workW = static_cast<int>(work.right - work.left);
    const int workH = static_cast<int>(work.bottom - work.top);
    int rw = util::Scale(700, dpi_);
    int rh = util::Scale(400, dpi_);
    const int maxW = static_cast<int>(workW * 0.9);
    const int maxH = static_cast<int>(workH * 0.9);
    if (rw > maxW) rw = maxW;
    if (rh > maxH) rh = maxH;
    region_ = { work.left + ((work.right - work.left) - rw) / 2,
                work.top + ((work.bottom - work.top) - rh) / 2,
                work.left + ((work.right - work.left) - rw) / 2 + rw,
                work.top + ((work.bottom - work.top) - rh) / 2 + rh };

    // 标题栏三个按钮都要：最小化 / 最大化 / 关闭。
    // WS_EX_APPWINDOW：让这个弹出窗在任务栏也有按钮，最小化后还能点回来（否则就"消失"了）。
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                  WS_THICKFRAME | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_APPWINDOW;

    // 先按「选区 + 边框 + 底栏」算出含标题栏的窗口尺寸
    int cw = (region_.right - region_.left) + kBorder * 2;
    int ch = (region_.bottom - region_.top) + kBorder * 2 + BarHeight();
    RECT wr = { 0, 0, cw, ch };
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;

    hwnd_ = CreateWindowExW(exStyle, kRecClass, L"GIF 录制 · 拖动标题栏移动，拖边框调整大小",
                            style, region_.left, region_.top, winW, winH,
                            owner, nullptr, hi, nullptr);
    if (!hwnd_) return;

    font_ = CreateFontW(-util::Scale(14, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    const int btnW = util::Scale(104, dpi_);
    const int btnH = util::Scale(32, dpi_);
    const int gap = util::Scale(8, dpi_);
    const int padR = util::Scale(12, dpi_);
    int cwNow = cw, chNow = ch;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    cwNow = cr.right;
    chNow = cr.bottom;

    btnSecond_ = CreateWindowW(L"BUTTON", L"取消",
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                               cwNow - padR - btnW, chNow - BarHeight() + util::Scale(7, dpi_),
                               btnW, btnH, hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSecond)), hi, nullptr);
    btnMain_ = CreateWindowW(L"BUTTON", L"录制",
                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                             cwNow - padR - btnW * 2 - gap, chNow - BarHeight() + util::Scale(7, dpi_),
                             btnW, btnH, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdMain)), hi, nullptr);
    status_ = CreateWindowW(L"STATIC", L"",
                            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                            util::Scale(12, dpi_), chNow - BarHeight(),
                            cwNow - padR * 2 - btnW * 2 - gap - util::Scale(16, dpi_),
                            BarHeight(), hwnd_, nullptr, hi, nullptr);
    if (font_) {
        if (status_) SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (btnMain_) SendMessageW(btnMain_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (btnSecond_) SendMessageW(btnSecond_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }

    ApplyGeometry();
    // 让「选区」正好落在计划的位置
    RECT cr2;
    GetClientRect(hwnd_, &cr2);
    RECT hole = { kBorder, kBorder, cr2.right - kBorder, cr2.bottom - BarHeight() };
    RectToScreen(hwnd_, hole);
    int dx = region_.left - hole.left;
    int dy = region_.top - hole.top;
    RECT wrNow;
    GetWindowRect(hwnd_, &wrNow);
    SetWindowPos(hwnd_, HWND_TOPMOST, wrNow.left + dx, wrNow.top + dy, 0, 0,
                 SWP_NOSIZE | SWP_SHOWWINDOW);

    state_ = State::Idle;
    UpdateButtons();
    UpdateStatusText();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void GifRecorder::ApplyGeometry() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) return;
    RECT wr;
    GetWindowRect(hwnd_, &wr);

    RECT hole = { kBorder, kBorder, cr.right - kBorder, cr.bottom - BarHeight() };
    if (hole.right - hole.left < 32 || hole.bottom - hole.top < 32) return;

    // 记下真实录制区域（屏幕坐标）
    RECT holeScreen = hole;
    RectToScreen(hwnd_, holeScreen);
    region_ = holeScreen;

    // 窗口区域 = 整窗 - 内容区（内容区留空，直接透出桌面）
    POINT org = { 0, 0 };
    ClientToScreen(hwnd_, &org);
    RECT holeWin = { org.x - wr.left + hole.left, org.y - wr.top + hole.top,
                     org.x - wr.left + hole.right, org.y - wr.top + hole.bottom };
    HRGN outer = CreateRectRgn(0, 0, wr.right - wr.left, wr.bottom - wr.top);
    HRGN inner = CreateRectRgn(holeWin.left, holeWin.top, holeWin.right, holeWin.bottom);
    if (outer && inner) {
        CombineRgn(outer, outer, inner, RGN_DIFF);
        SetWindowRgn(hwnd_, outer, TRUE);   // 成功后 outer 归系统所有
        outer = nullptr;
    }
    if (outer) DeleteObject(outer);
    if (inner) DeleteObject(inner);

    LayoutBar();
    if (state_ == State::Idle) UpdateStatusText();   // 拖动/缩放后区域尺寸要跟着变
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void GifRecorder::LayoutBar() {
    if (!hwnd_) return;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    const int barH = BarHeight();
    const int btnW = util::Scale(104, dpi_);
    const int btnH = util::Scale(32, dpi_);
    const int gap = util::Scale(8, dpi_);
    const int padR = util::Scale(12, dpi_);
    const int y = cr.bottom - barH + (barH - btnH) / 2;

    if (btnSecond_) MoveWindow(btnSecond_, cr.right - padR - btnW, y, btnW, btnH, TRUE);
    if (btnMain_) MoveWindow(btnMain_, cr.right - padR - btnW * 2 - gap, y, btnW, btnH, TRUE);
    if (status_) {
        MoveWindow(status_, util::Scale(12, dpi_), cr.bottom - barH,
                   (std::max)(40, static_cast<int>(cr.right) - padR * 2 - btnW * 2 - gap -
                                      util::Scale(20, dpi_)),
                   barH, TRUE);
    }
}

void GifRecorder::UpdateButtons() {
    if (!btnMain_ || !btnSecond_) return;
    switch (state_) {
    case State::Idle:
        SetWindowTextW(btnMain_, L"录制");
        SetWindowTextW(btnSecond_, L"取消");
        EnableWindow(btnMain_, TRUE);
        break;
    case State::Recording:
        SetWindowTextW(btnMain_, L"暂停");
        SetWindowTextW(btnSecond_, L"停止");
        break;
    case State::Paused:
        SetWindowTextW(btnMain_, L"继续");
        SetWindowTextW(btnSecond_, L"停止");
        break;
    }
    InvalidateRect(btnMain_, nullptr, TRUE);
    InvalidateRect(btnSecond_, nullptr, TRUE);
}

void GifRecorder::UpdateStatusText() {
    if (!status_) return;
    std::wstring txt;
    const int frames = frameCount_.load();
    if (state_ == State::Idle) {
        txt = util::Format(L"录制区域 %d×%d · %d fps · 点「录制」开始",
                           region_.right - region_.left, region_.bottom - region_.top,
                           Settings().gifFps);
    } else {
        DWORD now = GetTickCount();
        DWORD elapsedMs = (state_ == State::Paused)
                              ? (pauseStart_ - startTick_ - pausedAccum_)
                              : (now - startTick_ - pausedAccum_);
        const DWORD sec = elapsedMs / 1000;
        txt = util::Format(L"%s %02u:%02u · %d 帧 · %d fps",
                           state_ == State::Paused ? L"已暂停" : L"录制中",
                           sec / 60, sec % 60, frames, Settings().gifFps);
    }
    SetWindowTextW(status_, txt.c_str());
}

void GifRecorder::StartRecording() {
    if (!hwnd_ || state_ != State::Idle) return;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    RECT hole = { kBorder, kBorder, cr.right - kBorder, cr.bottom - BarHeight() };
    RectToScreen(hwnd_, hole);

    const int w = hole.right - hole.left;
    const int h = hole.bottom - hole.top;
    if (w < 16 || h < 16) {
        MessageBoxW(hwnd_, L"录制区域太小，请拖动边框放大后再录制。", APP_NAME, MB_ICONWARNING);
        return;
    }
    region_ = hole;
    // 录制区域放宽到偶数像素，避免编码时的半像素抖动
    if (w & 1) region_.right -= 1;
    if (h & 1) region_.bottom -= 1;

    {
        std::lock_guard<std::mutex> lk(mu_);
        anim_ = gifcodec::Anim();
        anim_.width = region_.right - region_.left;
        anim_.height = region_.bottom - region_.top;
        anim_.loop = true;
    }
    frameCount_.store(0);
    stopFlag_.store(false);
    bool paused = false;
    (void)paused;

    const int fps = (std::max)(1, (std::min)(60, Settings().gifFps));
    stopFlag_ = false;
    paused_ = false;
    state_ = State::Recording;
    dragResize_ = false;
    startTick_ = GetTickCount();
    pausedAccum_ = 0;
    pauseStart_ = 0;

    RECT r = region_;
    worker_ = std::thread([this, r, fps]() {
        WorkerLoop(r.left, r.top, r.right - r.left, r.bottom - r.top, fps);
    });

    UpdateButtons();
    UpdateStatusText();
    SetTimer(hwnd_, kTimerStatus, 100, nullptr);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void GifRecorder::PauseRecording() {
    if (state_ != State::Recording) return;
    state_ = State::Paused;
    paused_ = true;
    pauseStart_ = GetTickCount();
    UpdateButtons();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void GifRecorder::ResumeRecording() {
    if (state_ != State::Paused) return;
    pausedAccum_ += GetTickCount() - pauseStart_;
    paused_ = false;
    state_ = State::Recording;
    UpdateButtons();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void GifRecorder::StopRecording() {
    if (state_ != State::Recording && state_ != State::Paused) return;
    stopFlag_ = true;
    paused_ = false;
    if (worker_.joinable()) worker_.join();

    gifcodec::Anim result;
    {
        std::lock_guard<std::mutex> lk(mu_);
        result = std::move(anim_);
    }
    const int n = static_cast<int>(result.frames.size());
    // 先把录制窗口收掉（主窗口仍藏着），把动图页签建好，再恢复主窗口——
    // 这样主界面一出现就是成品页签，不会先闪一下空白/旧画面
    CloseWindowOnly();
    if (result.Empty() || n == 0) {
        RestoreOwner();
        MessageBoxW(owner_ && IsWindow(owner_) ? owner_ : nullptr,
                    L"这次没有抓到任何画面，请重新录制。", APP_NAME, MB_ICONWARNING);
        return;
    }
    App::Instance().AddGifDocument(std::make_shared<gifcodec::Anim>(std::move(result)));
    RestoreOwner();
}

void GifRecorder::Cancel() {
    stopFlag_ = true;
    paused_ = false;
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(mu_);
        anim_ = gifcodec::Anim();
    }
    frameCount_.store(0);
    CloseAll();
}

void GifRecorder::CloseAll() {
    CloseWindowOnly();
    RestoreOwner();
}

void GifRecorder::CloseWindowOnly() {
    if (hwnd_ && IsWindow(hwnd_)) KillTimer(hwnd_, kTimerStatus);
    state_ = State::Idle;
    HWND w = hwnd_;
    hwnd_ = nullptr;
    btnMain_ = nullptr;
    btnSecond_ = nullptr;
    status_ = nullptr;
    if (w && IsWindow(w)) DestroyWindow(w);
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void GifRecorder::RestoreOwner() {
    if (!owner_ || !IsWindow(owner_)) return;
    // 恢复显示状态（原来最大化就还原最大化，最小化就还原最小化）
    if (ownerShowCmd_ == SW_SHOWMINIMIZED) {
        ShowWindow(owner_, SW_SHOWMINIMIZED);
    } else if (ownerShowCmd_ == SW_SHOWMAXIMIZED) {
        ShowWindow(owner_, SW_SHOWMAXIMIZED);
    } else {
        ShowWindow(owner_, SW_SHOW);
    }
    // 前台锁：本进程此刻不一定还持有前台窗口，直接 SetForegroundWindow 会被系统忽略。
    // 临时把本线程的输入挂到当前前台线程上，切完再解挂。
    HWND fg = GetForegroundWindow();
    DWORD tidFg = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD tidMe = GetCurrentThreadId();
    bool attached = false;
    if (tidFg && tidFg != tidMe) attached = AttachThreadInput(tidMe, tidFg, TRUE) != 0;
    SetForegroundWindow(owner_);
    BringWindowToTop(owner_);
    if (attached) AttachThreadInput(tidMe, tidFg, FALSE);
    ownerShowCmd_ = SW_SHOWNORMAL;
}

void GifRecorder::WorkerLoop(int x, int y, int w, int h, int fps) {
    if (w <= 0 || h <= 0) return;
    timeBeginPeriod(1);

    HDC screen = GetDC(nullptr);
    HDC mem = screen ? CreateCompatibleDC(screen) : nullptr;
    if (screen && mem) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;      // 自上而下
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib && bits) {
            HGDIOBJ old = SelectObject(mem, dib);
            std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
            const int interval = (std::max)(10, 1000 / (fps > 0 ? fps : 24));
            ULONGLONG lastTick = GetTickCount64();
            bool first = true;

            while (!stopFlag_.load()) {
                if (paused_.load()) {
                    Sleep(10);
                    lastTick = GetTickCount64();
                    first = false;
                    continue;
                }
                const ULONGLONG t0 = GetTickCount64();
                BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
                GdiFlush();
                memcpy(buf.data(), bits, buf.size());

                int delay = first ? interval : static_cast<int>(t0 - lastTick);
                if (delay < 5) delay = 5;
                if (delay > 1000) delay = 1000;

                gifcodec::Frame f;
                if (gifcodec::CompressFrame(buf.data(), w, h, delay, f)) {
                    std::lock_guard<std::mutex> lk(mu_);
                    // 画面没变就并到上一帧的停留时间里，避免 GIF 白白变大
                    if (!anim_.frames.empty() &&
                        anim_.frames.back().data.size() == f.data.size() &&
                        memcmp(anim_.frames.back().data.data(), f.data.data(), f.data.size()) == 0) {
                        int d = anim_.frames.back().delayMs + delay;
                        anim_.frames.back().delayMs = (std::min)(2000, d);
                    } else {
                        anim_.frames.push_back(std::move(f));
                        frameCount_.store(static_cast<int>(anim_.frames.size()));
                    }
                }
                lastTick = t0;
                first = false;

                const int spent = static_cast<int>(GetTickCount64() - t0);
                const int wait = interval - spent;
                if (wait > 0) Sleep(wait);
            }
            SelectObject(mem, old);
        }
        if (dib) DeleteObject(dib);
        DeleteDC(mem);
    }
    if (screen) ReleaseDC(nullptr, screen);
    timeEndPeriod(1);
}

void GifRecorder::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    auto& s = Settings();
    const COLORREF panel = s.PanelColor();
    const int barH = BarHeight();

    // 外圈提示色：录制中红色，待录制/暂停主题色
    COLORREF edge = s.themeColor;
    if (state_ == State::Recording) edge = RGB(216, 48, 48);
    else if (state_ == State::Paused) edge = RGB(224, 150, 32);

    SolidBrush edgeBr(ToGpColor(edge));
    g.FillRectangle(&edgeBr, 0, 0, rc.right, kBorder);                       // 上
    g.FillRectangle(&edgeBr, 0, rc.bottom - barH - kBorder, rc.right, kBorder); // 下（贴着录区底部）
    g.FillRectangle(&edgeBr, 0, 0, kBorder, rc.bottom - barH);               // 左
    g.FillRectangle(&edgeBr, rc.right - kBorder, 0, kBorder, rc.bottom - barH); // 右

    // 底部控制条
    SolidBrush panelBr(ToGpColor(panel));
    g.FillRectangle(&panelBr, 0, rc.bottom - barH, rc.right, barH);
    Pen line(ToGpColor(s.BorderColor()), 1);
    g.DrawLine(&line, 0, rc.bottom - barH, rc.right, rc.bottom - barH);

    EndPaint(hwnd_, &ps);
}

void GifRecorder::DrawBarButton(DRAWITEMSTRUCT* dis, bool primary) {
    if (!dis) return;
    Graphics g(dis->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    RECT rc = dis->rcItem;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const bool dark = Settings().IsDarkTheme();
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    // 底色
    COLORREF bg = dark ? RGB(58, 58, 58) : RGB(250, 250, 250);
    if (pressed) bg = dark ? RGB(80, 80, 80) : RGB(226, 226, 226);
    if (primary) bg = dark ? RGB(48, 48, 48) : RGB(255, 255, 255);
    SolidBrush bgBr(ToGpColor(bg));
    g.FillRectangle(&bgBr, 0, 0, w, h);
    Pen border(ToGpColor(Settings().BorderColor()), 1);
    g.DrawRectangle(&border, 0, 0, w - 1, h - 1);

    // 左侧图标
    wchar_t text[64] = {};
    GetWindowTextW(dis->hwndItem, text, 64);
    const REAL cy = h / 2.0f;
    REAL iconX = 14.0f;
    const REAL iconS = 14.0f;
    const bool isStop = (wcscmp(text, L"停止") == 0);
    const bool isPause = (wcscmp(text, L"暂停") == 0);
    const bool isCancel = (wcscmp(text, L"取消") == 0);
    const bool isRecord = (wcscmp(text, L"录制") == 0) || (wcscmp(text, L"继续") == 0);

    if (isRecord) {
        SolidBrush dot(ToGpColor(RGB(214, 40, 40)));
        g.FillEllipse(&dot, iconX, cy - iconS / 2, iconS, iconS);
        iconX += iconS + 8;
    } else if (isPause) {
        SolidBrush b(ToGpColor(RGB(224, 150, 32)));
        g.FillRectangle(&b, iconX + 2, cy - iconS / 2, 4.0f, iconS);
        g.FillRectangle(&b, iconX + 9, cy - iconS / 2, 4.0f, iconS);
        iconX += iconS + 8;
    } else if (isStop) {
        SolidBrush b(ToGpColor(RGB(0, 120, 215)));
        g.FillRectangle(&b, iconX, cy - iconS / 2 + 1, iconS - 2, iconS - 2);
        iconX += iconS + 8;
    } else if (isCancel) {
        iconX = 8;
    }

    // 文字
    FontFamily fam(L"Microsoft YaHei");
    Font font(&fam, static_cast<REAL>(util::Scale(14, dpi_)), FontStyleRegular, UnitPixel);
    SolidBrush fg(ToGpColor(disabled ? (dark ? RGB(120, 120, 120) : RGB(170, 170, 170))
                                     : Settings().TextColor()));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF layout(iconX, 0, static_cast<REAL>(w) - iconX - 4.0f, static_cast<REAL>(h));
    if (isCancel) {
        fmt.SetAlignment(StringAlignmentCenter);
        layout = RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(h));
    }
    g.DrawString(text, -1, &font, layout, &fmt, &fg);
}

LRESULT CALLBACK GifRecorder::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GifRecorder& self = GifRecorder::Instance();
    switch (msg) {
    case WM_SIZE:
        if (self.hwnd_ == hwnd) self.ApplyGeometry();
        return 0;
    case WM_MOVE:
        if (self.hwnd_ == hwnd) self.ApplyGeometry();
        return 0;
    case WM_ERASEBKGND: {
        // 只有边框与控制条可见，直接填面板色
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = CreateSolidBrush(Settings().PanelColor());
        FillRect(hdc, &rc, br);
        DeleteObject(br);
        return 1;
    }
    case WM_PAINT:
        if (self.hwnd_ == hwnd) self.OnPaint();
        return 0;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (self.state_ != State::Idle && r != HTCLIENT) return HTNOWHERE; // 录制中锁定位置/大小
        return r;
    }
    case WM_GETMINMAXINFO: {
        // 下限：保证底部控制条（两个按钮 + 状态文字）永远放得下
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT need = { 0, 0, util::Scale(430, self.dpi_), util::Scale(210, self.dpi_) };
        DWORD st = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        DWORD ex = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectEx(&need, st, FALSE, ex);
        mmi->ptMinTrackSize.x = need.right - need.left;
        mmi->ptMinTrackSize.y = need.bottom - need.top;
        return 0;
    }
    case WM_SYSCOMMAND: {
        const UINT cmd = static_cast<UINT>(wParam) & 0xFFF0;
        if (self.state_ != State::Idle &&
            (cmd == SC_MINIMIZE || cmd == SC_MAXIMIZE || cmd == SC_RESTORE ||
             cmd == SC_MOVE || cmd == SC_SIZE)) {
            // 录制中把这些藏起来会导致停不掉，直接忽略
            self.UpdateStatusText();
            return 0;
        }
        // 空闲时交给系统处理，才会真的最小化/最大化
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kIdMain) {
            if (self.state_ == State::Idle) self.StartRecording();
            else if (self.state_ == State::Recording) self.PauseRecording();
            else if (self.state_ == State::Paused) self.ResumeRecording();
            return 0;
        }
        if (id == kIdSecond) {
            if (self.state_ == State::Idle) self.Cancel();
            else self.StopRecording();
            return 0;
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == static_cast<WPARAM>(kTimerStatus)) self.UpdateStatusText();
        return 0;
    case WM_DPICHANGED:
        // 拖到另一块不同缩放比例的显示器：重新按新 DPI 排布（客户区/录区随之更新）
        if (self.hwnd_ == hwnd) {
            self.dpi_ = HIWORD(wParam);
            self.ApplyGeometry();
        }
        return 0;
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (!dis || (dis->CtlID != kIdMain && dis->CtlID != kIdSecond)) return 0;
        self.DrawBarButton(dis, dis->CtlID == kIdMain);
        return TRUE;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            self.Cancel();
            return 0;
        }
        return 0;
    case WM_CLOSE:
        self.Cancel();
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);   // 兜底：任何 break 都要回到系统默认处理
}
