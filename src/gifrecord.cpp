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

// 单帧最长停留时间：画面长时间不动时，超过这个值就复制一帧继续累计，
// 保证 GIF 总时长与真实录制时长一致（旧版卡在 2000ms，长静止画面会变成"快进"）
const int kMaxFrameDelayMs = 5000;

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

// 按指定 DPI 把「客户区尺寸」换算成「窗口尺寸」。
// AdjustWindowRectEx 用的是线程 DPI，在多显示器（不同缩放）下会算错边框/标题栏，
// 于是窗口会比实际需要小一截（表现就是右上角按钮被裁）。这里优先用 ForDpi 版本。
void AdjustWindowRectForDpi(RECT& rc, DWORD style, DWORD exStyle, int dpi) {
    typedef BOOL(WINAPI* Fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static Fn fn = nullptr;
    static bool inited = false;
    if (!inited) {
        inited = true;
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (u) fn = reinterpret_cast<Fn>(GetProcAddress(u, "AdjustWindowRectExForDpi"));
    }
    if (fn) fn(&rc, style, FALSE, exStyle, static_cast<UINT>(dpi));
    else AdjustWindowRectEx(&rc, style, FALSE, exStyle);
}

// 目标监视器的 DPI（GetDpiForMonitor 是 Win8.1+，取不到就退回窗口 DPI）
int MonitorDpi(HMONITOR mon, HWND fallback) {
    if (mon) {
        typedef HRESULT(WINAPI* Fn)(HMONITOR, int, UINT*, UINT*);
        static Fn fn = nullptr;
        static bool inited = false;
        if (!inited) {
            inited = true;
            HMODULE sh = LoadLibraryW(L"shcore.dll");
            if (sh) fn = reinterpret_cast<Fn>(GetProcAddress(sh, "GetDpiForMonitor"));
        }
        if (fn) {
            UINT dx = 0, dy = 0;
            if (SUCCEEDED(fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy)) && dx > 0)
                return static_cast<int>(dx);
        }
    }
    return util::GetDpiForWindowSafe(fallback);
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

int GifRecorder::TitleHeight() const {
    return util::Scale(34, dpi_);
}

// 客户区里的"录制内容"矩形：上面让出自绘标题栏，下面让出控制条，四周各留边框
RECT GifRecorder::ContentHole() const {
    RECT cr = {};
    if (hwnd_) GetClientRect(hwnd_, &cr);
    const LONG r = (std::max)(static_cast<LONG>(kBorder + 8), static_cast<LONG>(cr.right - kBorder));
    const LONG b = (std::max)(static_cast<LONG>(TitleHeight() + kBorder + 8),
                              static_cast<LONG>(cr.bottom - BarHeight()));
    RECT hole = { kBorder, TitleHeight() + kBorder, r, b };
    return hole;
}

RECT GifRecorder::TitleButtonRect(int id) const {
    RECT rc = {};
    if (!hwnd_ || id < 1 || id > 3) return rc;
    RECT cr;
    GetClientRect(hwnd_, &cr);
    const int w = util::Scale(46, dpi_);
    rc.right = cr.right - w * (3 - id);     // id=3 贴右边缘
    rc.left = rc.right - w;
    rc.top = 0;
    rc.bottom = TitleHeight();
    return rc;
}

int GifRecorder::HitTitleButton(int x, int y) const {
    if (y < 0 || y >= TitleHeight()) return 0;
    POINT pt = { x, y };
    for (int id = 1; id <= 3; ++id) {
        RECT r = TitleButtonRect(id);
        if (PtInRect(&r, pt)) return id;
    }
    return 0;
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
    // DPI 用"录制窗将要落到的那块显示器"的，不能拿主窗口的：
    // 多显示器缩放比例不同时，拿主窗口的 DPI 算边框会导致窗口偏小（标题栏被裁）。
    dpi_ = MonitorDpi(mon, owner);
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

    // 不要系统标题栏：标题栏和三个按钮都由我们自己画。
    // 系统标题栏在"带窗口区域(hollow)"的窗口上会退化成经典样式、且 DPI 不同会被裁，
    // 自绘就不存在这些问题（按钮永远按我们的 DPI 尺寸画在客户区里）。
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN;
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_APPWINDOW;

    // 客户区 = 边框 + 标题栏 + 录制内容 + 底栏
    int cw = (region_.right - region_.left) + kBorder * 2;
    int ch = (region_.bottom - region_.top) + kBorder * 2 + BarHeight() + TitleHeight();
    RECT wr = { 0, 0, cw, ch };
    AdjustWindowRectForDpi(wr, style, exStyle, dpi_);
    const int winW = wr.right - wr.left;
    const int winH = wr.bottom - wr.top;

    hwnd_ = CreateWindowExW(exStyle, kRecClass, L"GIF 录制", style,
                            region_.left, region_.top, winW, winH,
                            owner, nullptr, hi, nullptr);
    if (!hwnd_) return;

    // 建好后再核对一次真实 DPI：创建前后窗口所属显示器可能不同，不一致就按真实值重算窗口尺寸
    const int realDpi = util::GetDpiForWindowSafe(hwnd_);
    if (realDpi > 0 && realDpi != dpi_) {
        dpi_ = realDpi;
        RECT need = { 0, 0, cw, ch };
        AdjustWindowRectForDpi(need, style, exStyle, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, need.right - need.left, need.bottom - need.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    font_ = CreateFontW(-util::Scale(14, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");

    const int btnW = util::Scale(104, dpi_);
    const int btnH = util::Scale(32, dpi_);
    const int gap = util::Scale(8, dpi_);
    const int padR = util::Scale(12, dpi_);
    RECT cr;
    GetClientRect(hwnd_, &cr);
    const int cwNow = cr.right;
    const int chNow = cr.bottom;

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
    // 让「录制内容」正好落在计划的位置，并整体夹进工作区（标题栏/按钮不能跑到屏幕外）
    {
        RECT cr2;
        GetClientRect(hwnd_, &cr2);
        RECT hole = ContentHole();
        RectToScreen(hwnd_, hole);
        RECT wrNow;
        GetWindowRect(hwnd_, &wrNow);
        int nx = wrNow.left + (region_.left - hole.left);
        int ny = wrNow.top + (region_.top - hole.top);
        const int ww = wrNow.right - wrNow.left;
        const int wh = wrNow.bottom - wrNow.top;
        RECT wk = work;
        HMONITOR m2 = MonitorFromRect(&wrNow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi2 = { sizeof(mi2) };
        if (m2 && GetMonitorInfoW(m2, &mi2)) wk = mi2.rcWork;
        if (nx + ww > wk.right) nx = wk.right - ww;
        if (ny + wh > wk.bottom) ny = wk.bottom - wh;
        if (nx < wk.left) nx = wk.left;
        if (ny < wk.top) ny = wk.top;
        SetWindowPos(hwnd_, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    }

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

    RECT hole = ContentHole();
    if (hole.right - hole.left < 32 || hole.bottom - hole.top < 32) return;

    // 记下真实录制区域（屏幕坐标）
    RECT holeScreen = hole;
    RectToScreen(hwnd_, holeScreen);
    region_ = holeScreen;

    // 窗口区域 = 整窗 - 内容区（内容区留空，直接透出桌面）
    RECT wr;
    GetWindowRect(hwnd_, &wr);
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
        // 抓帧跟不上设定帧率时把实测值也显示出来（大区域 + 高帧率很容易达不到）
        const int target = (std::max)(1, Settings().gifFps);
        const int measured = measuredFps_.load();
        if (measured > 0 && state_ == State::Recording &&
            (measured * 10 < target * 9 || measured * 10 > target * 11)) {
            txt += util::Format(L"（实测 %d）", measured);
        }
    }
    SetWindowTextW(status_, txt.c_str());
}

void GifRecorder::StartRecording() {
    if (!hwnd_ || state_ != State::Idle) return;
    RECT hole = ContentHole();
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
    measuredFps_.store(0);
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
    measuredFps_.store(0);
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
            const ULONGLONG statStart = lastTick;
            int statCount = 0;

            while (!stopFlag_.load()) {
                if (paused_.load()) {
                    Sleep(10);
                    lastTick = GetTickCount64();
                    first = false;
                    continue;
                }
                const ULONGLONG t0 = GetTickCount64();
                BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);
                // 鼠标箭头：BitBlt 抓不到光标，按设置补画（GIF 默认画，便于引导视线）
                if (Settings().captureCursorGif) {
                    POINT org = { x, y };
                    util::DrawCursorOnDC(mem, org);
                }
                GdiFlush();
                memcpy(buf.data(), bits, buf.size());

                int delay = first ? interval : static_cast<int>(t0 - lastTick);
                if (delay < 5) delay = 5;
                if (delay > 1000) delay = 1000;

                gifcodec::Frame f;
                if (gifcodec::CompressFrame(buf.data(), w, h, delay, f)) {
                    std::lock_guard<std::mutex> lk(mu_);
                    // 画面没变就并到上一帧的停留时间里，避免 GIF 白白变大。
                    // 单帧不要积太久（很多播放器对超长延迟处理不一致），
                    // 超过 kMaxFrameDelayMs 就复制一帧接着累计——总时长才不会缩水。
                    if (!anim_.frames.empty() &&
                        anim_.frames.back().data.size() == f.data.size() &&
                        memcmp(anim_.frames.back().data.data(), f.data.data(), f.data.size()) == 0) {
                        int total = anim_.frames.back().delayMs + delay;
                        while (total > kMaxFrameDelayMs) {
                            anim_.frames.back().delayMs = kMaxFrameDelayMs;
                            gifcodec::Frame dup = anim_.frames.back();  // 压缩数据相同，直接复制
                            dup.delayMs = 0;
                            anim_.frames.push_back(std::move(dup));
                            frameCount_.store(static_cast<int>(anim_.frames.size()));
                            total -= kMaxFrameDelayMs;
                        }
                        anim_.frames.back().delayMs = total;
                    } else {
                        anim_.frames.push_back(std::move(f));
                        frameCount_.store(static_cast<int>(anim_.frames.size()));
                    }
                }
                lastTick = t0;
                first = false;

                // 实测抓帧速率（用于状态栏显示；达不到设定值时要让用户看见）
                if (++statCount >= 8) {
                    const ULONGLONG el = GetTickCount64() - statStart;
                    if (el > 0) {
                        int mf = static_cast<int>((statCount * 1000 + el / 2) / el);
                        measuredFps_.store(mf);
                    }
                }

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

void GifRecorder::DrawTitleBar(Graphics& g, const RECT& rc) {
    auto& s = Settings();
    const bool dark = s.IsDarkTheme();
    const int titleH = TitleHeight();
    const int w = rc.right;

    // 标题栏底色（比内容面板略深一点，和下方控制条区分开）
    SolidBrush bg(ToGpColor(dark ? RGB(52, 52, 52) : RGB(240, 240, 240)));
    g.FillRectangle(&bg, 0, 0, w, titleH);
    Pen line(ToGpColor(s.BorderColor()), 1);
    g.DrawLine(&line, 0, titleH - 1, w, titleH - 1);

    // 左侧标题文字（按钮区之前，超出就裁掉）
    const int btnW = util::Scale(46, dpi_);
    const int textRight = w - btnW * 3 - util::Scale(8, dpi_);
    FontFamily fam(L"Microsoft YaHei");
    Font f(&fam, static_cast<REAL>(util::Scale(13, dpi_)), FontStyleRegular, UnitPixel);
    SolidBrush fg(ToGpColor(s.TextColor()));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentNear);
    fmt.SetLineAlignment(StringAlignmentCenter);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF layout(static_cast<REAL>(util::Scale(12, dpi_)), 0,
                 static_cast<REAL>((std::max)(0, textRight)),
                 static_cast<REAL>(titleH));
    g.DrawString(IsBusy() ? L"GIF 录制中 · 拖边框可调整大小"
                          : L"GIF 录制 · 拖动标题栏移动，拖边框调整大小",
                 -1, &f, layout, &fmt, &fg);

    // 右侧三个按钮：最小化 / 最大化(还原) / 关闭
    const Color glyph = ToGpColor(s.TextColor());
    const REAL gs = static_cast<REAL>(util::Scale(9, dpi_));
    for (int id = 1; id <= 3; ++id) {
        RECT br = TitleButtonRect(id);
        const REAL bw = static_cast<REAL>(br.right - br.left);
        const REAL bh = static_cast<REAL>(br.bottom - br.top);
        const bool hot = (titleHot_ == id);
        const bool pressed = (titlePressed_ == id && hot);
        if (hot) {
            COLORREF hb = (id == 3) ? RGB(216, 48, 48)
                                    : (dark ? RGB(78, 78, 78) : RGB(224, 224, 224));
            SolidBrush hbr(ToGpColor(hb));
            g.FillRectangle(&hbr, static_cast<REAL>(br.left), 0.0f, bw, bh);
        }
        if (pressed) {
            SolidBrush pbr(Color(40, 0, 0, 0));
            g.FillRectangle(&pbr, static_cast<REAL>(br.left), 0.0f, bw, bh);
        }
        const Color col = (hot && id == 3) ? Color(255, 255, 255, 255) : glyph;
        Pen pen(col, static_cast<REAL>(dpi_) * 1.4f / 96.0f);
        const REAL cx = br.left + bw / 2;
        const REAL cy = bh / 2;
        if (id == 1) {
            g.DrawLine(&pen, cx - gs / 2, cy, cx + gs / 2, cy);
        } else if (id == 2) {
            if (maximized_) {
                // 还原：两个错开的小方框
                g.DrawRectangle(&pen, cx - gs / 2 + 1, cy - gs / 2, gs - 2, gs - 2);
                g.DrawRectangle(&pen, cx - gs / 2, cy - gs / 2 + 1, gs - 2, gs - 2);
            } else {
                g.DrawRectangle(&pen, cx - gs / 2, cy - gs / 2, gs, gs);
            }
        } else {
            g.DrawLine(&pen, cx - gs / 2, cy - gs / 2, cx + gs / 2, cy + gs / 2);
            g.DrawLine(&pen, cx + gs / 2, cy - gs / 2, cx - gs / 2, cy + gs / 2);
        }
    }
}

void GifRecorder::OnTitleButton(int id) {
    if (!hwnd_) return;
    switch (id) {
    case 1:
        ShowWindow(hwnd_, SW_MINIMIZE);
        break;
    case 2:
        ToggleMaximize();
        break;
    case 3:
        Cancel();
        break;
    default:
        break;
    }
}

void GifRecorder::ToggleMaximize() {
    if (!hwnd_) return;
    if (!maximized_) {
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        HMONITOR mon = MonitorFromRect(&wr, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (mon && GetMonitorInfoW(mon, &mi)) {
            restoreRect_ = wr;
            maximized_ = true;
            SetWindowPos(hwnd_, HWND_TOPMOST, mi.rcWork.left, mi.rcWork.top,
                         mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top,
                         SWP_NOACTIVATE);
            ApplyGeometry();
            InvalidateRect(hwnd_, nullptr, TRUE);
        }
    } else {
        maximized_ = false;
        if (restoreRect_.right > restoreRect_.left) {
            SetWindowPos(hwnd_, HWND_TOPMOST, restoreRect_.left, restoreRect_.top,
                         restoreRect_.right - restoreRect_.left,
                         restoreRect_.bottom - restoreRect_.top, SWP_NOACTIVATE);
        }
        ApplyGeometry();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
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
    const int titleH = TitleHeight();

    // 自绘标题栏
    DrawTitleBar(g, rc);

    // 外圈提示色：录制中红色，待录制/暂停主题色（标题栏以下、底栏以上那一段）
    COLORREF edge = s.themeColor;
    if (state_ == State::Recording) edge = RGB(216, 48, 48);
    else if (state_ == State::Paused) edge = RGB(224, 150, 32);

    SolidBrush edgeBr(ToGpColor(edge));
    g.FillRectangle(&edgeBr, 0, titleH, rc.right, kBorder);                       // 上
    g.FillRectangle(&edgeBr, 0, rc.bottom - barH - kBorder, rc.right, kBorder);   // 下（贴着录区底部）
    g.FillRectangle(&edgeBr, 0, titleH, kBorder, rc.bottom - barH - titleH);      // 左
    g.FillRectangle(&edgeBr, rc.right - kBorder, titleH, kBorder,
                    rc.bottom - barH - titleH);                                   // 右

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
        // 没有系统标题栏：边框/四角交给系统做缩放；自绘标题栏返回 HTCAPTION 就能拖动
        if (self.state_ != State::Idle) return HTNOWHERE;   // 录制中锁定位置/大小
        LRESULT r = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (r != HTCLIENT) return r;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < self.TitleHeight())
            return self.HitTitleButton(pt.x, pt.y) ? HTCLIENT : HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCLBUTTONDBLCLK:
        if (wParam == HTCAPTION) {
            self.ToggleMaximize();      // 双击标题栏 = 最大化/还原
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        int id = self.HitTitleButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (id != self.titleHot_) {
            self.titleHot_ = id;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!self.titleHot_ && !self.titlePressed_) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self.titleHot_ != -1) {
            self.titleHot_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        int id = self.HitTitleButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (id) {
            self.titlePressed_ = id;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (self.titlePressed_) {
            const int pressed = self.titlePressed_;
            self.titlePressed_ = -1;
            ReleaseCapture();
            int id = self.HitTitleButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
            if (id == pressed) self.OnTitleButton(pressed);   // 在按钮上松开才算点中
            return 0;
        }
        return 0;
    }
    case WM_GETMINMAXINFO: {
        // 下限：保证标题栏 + 底部控制条（两个按钮 + 状态文字）永远放得下
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT need = { 0, 0, util::Scale(430, self.dpi_),
                      util::Scale(210, self.dpi_) + self.TitleHeight() };
        DWORD st = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
        DWORD ex = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
        AdjustWindowRectForDpi(need, st, ex, self.dpi_);
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
        // 拖到另一块不同缩放比例的显示器：按新 DPI 重算窗口尺寸（否则边框/标题栏会被裁），
        // 录制内容等比缩放，子控件重排
        if (self.hwnd_ == hwnd) {
            RECT oldHole = self.ContentHole();
            const int holeW = oldHole.right - oldHole.left;
            const int holeH = oldHole.bottom - oldHole.top;
            const int oldDpi = (self.dpi_ > 0) ? self.dpi_ : 96;
            const int newDpi = static_cast<int>(HIWORD(wParam));
            self.dpi_ = (newDpi > 0) ? newDpi : oldDpi;
            const int nw = MulDiv(holeW, self.dpi_, oldDpi);
            const int nh = MulDiv(holeH, self.dpi_, oldDpi);
            RECT need = { 0, 0, nw + kBorder * 2,
                          nh + kBorder * 2 + self.BarHeight() + self.TitleHeight() };
            DWORD st = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
            DWORD ex = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
            AdjustWindowRectForDpi(need, st, ex, self.dpi_);
            const RECT* sug = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                         need.right - need.left, need.bottom - need.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            // 字体也按新 DPI 重建
            if (self.font_) {
                DeleteObject(self.font_);
                self.font_ = CreateFontW(-util::Scale(14, self.dpi_), 0, 0, 0, FW_NORMAL,
                                         FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                         CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
                if (self.font_) {
                    if (self.status_)
                        SendMessageW(self.status_, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(self.font_), TRUE);
                    if (self.btnMain_)
                        SendMessageW(self.btnMain_, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(self.font_), TRUE);
                    if (self.btnSecond_)
                        SendMessageW(self.btnSecond_, WM_SETFONT,
                                     reinterpret_cast<WPARAM>(self.font_), TRUE);
                }
            }
            self.ApplyGeometry();
            InvalidateRect(hwnd, nullptr, TRUE);
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
